param(
    [string[]]$Tests = @(),
    [int]$BuildTimeoutSeconds = 0,
    [int]$QemuTimeoutSeconds = 0,
    [string]$QemuMemory = '',
    [int]$QemuSmp = 0,
    [string]$QemuPath = '',
    [string]$TccSource = 'user/hello.c',
    [string]$TccName = 'hello',
    [string]$TccInOsSource = '',
    [string]$TccInOsName = '',
    [string]$TinyCcWslPath = ''
)

$ErrorActionPreference = 'Stop'
$Project = Split-Path -Parent $PSScriptRoot
$Build = Join-Path $Project 'tools\build-native.cmd'
$ConfigPath = Join-Path $PSScriptRoot 'ltp-config.psd1'
$Config = if (Test-Path -LiteralPath $ConfigPath) {
    Import-PowerShellDataFile -LiteralPath $ConfigPath
} else { @{} }

if ($Tests.Count -eq 0) {
    $Tests = @($Config.Tests)
}
if ($Tests.Count -eq 0) {
    throw "No tests configured; populate tools/ltp-config.psd1 or pass -Tests."
}
if ($BuildTimeoutSeconds -le 0) {
    $BuildTimeoutSeconds = [int]$Config.BuildTimeoutSeconds
}
if ($QemuTimeoutSeconds -le 0) {
    $QemuTimeoutSeconds = [int]$Config.QemuTimeoutSeconds
}
if ([string]::IsNullOrWhiteSpace($QemuMemory)) {
    $QemuMemory = [string]$Config.QemuMemory
}
if ($QemuSmp -le 0) {
    $QemuSmp = [int]$Config.QemuSmp
}
$Qemu = if (-not [string]::IsNullOrWhiteSpace($QemuPath)) {
    $QemuPath
} elseif (-not [string]::IsNullOrWhiteSpace($env:UCORE_QEMU)) {
    $env:UCORE_QEMU
} elseif (-not [string]::IsNullOrWhiteSpace([string]$Config.QemuPath)) {
    [string]$Config.QemuPath
} else {
    'qemu-system-i386.exe'
}
$NativeBin = Join-Path $Project 'target\native\bin'
$LogDir = Join-Path $Project 'target\native\ltp'
$TccElfRelative = ''

if ($Tests -contains 'tcc_elf') {
    $tccBuilder = Join-Path $Project 'tools\build-tcc-elf.ps1'
    $tccArgs = @('-Source', $TccSource, '-Name', $TccName, '-BuildRuntime')
    if (-not [string]::IsNullOrWhiteSpace($TinyCcWslPath)) {
        $tccArgs += @('-TinyCcWslPath', $TinyCcWslPath)
    }
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $tccBuilder @tccArgs
    if ($LASTEXITCODE -ne 0) {
        throw "TinyCC ELF preparation failed: $LASTEXITCODE"
    }
    $TccElfRelative = "target/native/tcc/$TccName-tcc.elf"
}

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

function Stop-ProcessTree([System.Diagnostics.Process]$Process) {
    if ($null -ne $Process -and -not $Process.HasExited) {
        # QEMU may exit between HasExited and taskkill, and Windows can report
        # an already-reaped child as a native nonzero exit.  Cleanup must not
        # turn an otherwise recorded PASS/TIMEOUT into a PowerShell exception.
        $previousErrorAction = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            & taskkill.exe /PID $Process.Id /T /F *> $null
        }
        finally {
            $ErrorActionPreference = $previousErrorAction
        }
    }
}

function Wait-FileMarker([string]$Path, [string]$Marker,
                         [System.Diagnostics.Process]$Process,
                         [int]$TimeoutSeconds) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if (Test-Path -LiteralPath $Path) {
            $text = Get-Content -LiteralPath $Path -Raw -ErrorAction SilentlyContinue
            if ($text -match [regex]::Escape($Marker)) {
                return $true
            }
            if ($text -match 'Triple fault|kernel panic at|user panic at') {
                return $false
            }
        }
        if ($null -ne $Process -and $Process.HasExited) {
            break
        }
        Start-Sleep -Milliseconds 250
    }
    return $false
}

$results = @()
foreach ($Test in $Tests) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $buildLog = Join-Path $LogDir "$Test-$stamp-build.log"
    $buildErr = Join-Path $LogDir "$Test-$stamp-build.err"
    $serialLog = Join-Path $LogDir "$Test-$stamp-serial.log"
    $qemuOut = Join-Path $LogDir "$Test-$stamp-qemu.out"
    $qemuErr = Join-Path $LogDir "$Test-$stamp-qemu.err"
    $buildProcess = $null
    $qemuProcess = $null
    $buildOk = $false
    $qemuOk = $false
    $status = 'FAIL'
    $detail = ''

    try {
        $buildArgs = @("DEFS+=-DTEST=$Test")
        if ($Test -eq 'tcc_elf') {
            $buildArgs += @("TCC_ELF=$TccElfRelative", 'TCC_ELF_NAME=tcc-program')
        }
        if ($Test -eq 'tcc_run' -and -not [string]::IsNullOrWhiteSpace($TccInOsSource)) {
            $buildArgs += @("TCC_DEMO_SOURCE=$TccInOsSource")
            if (-not [string]::IsNullOrWhiteSpace($TccInOsName)) {
                $buildArgs += @("TCC_DEMO_SOURCE_NAME=$TccInOsName")
            }
        }
        $buildProcess = Start-Process -FilePath $Build `
            -ArgumentList $buildArgs `
            -RedirectStandardOutput $buildLog -RedirectStandardError $buildErr `
            -PassThru -WindowStyle Hidden
        $buildDeadline = (Get-Date).AddSeconds($BuildTimeoutSeconds)
        while (-not $buildProcess.HasExited -and (Get-Date) -lt $buildDeadline) {
            Start-Sleep -Milliseconds 250
        }
        if (-not $buildProcess.HasExited) {
            Stop-ProcessTree $buildProcess
            throw "build timeout"
        }
        $buildProcess.Refresh()
        $buildExit = $buildProcess.ExitCode
        if ($null -eq $buildExit) {
            # .cmd launchers on some PowerShell versions expose a null exit
            # code even after completion; inspect the log for hard failures.
            $buildExit = 0
        }
        $buildText = if (Test-Path -LiteralPath $buildLog) {
            Get-Content -LiteralPath $buildLog -Raw -ErrorAction SilentlyContinue
        } else { '' }
        $buildErrText = if (Test-Path -LiteralPath $buildErr) {
            Get-Content -LiteralPath $buildErr -Raw -ErrorAction SilentlyContinue
        } else { '' }
        if ($buildExit -ne 0 -or
            $buildText -match '(?m)^make: \*\*\*' -or
            $buildText -match '(?m)fatal error:' -or
            $buildErrText -match '(?m)^make: \*\*\*' -or
            $buildErrText -match '(?m)fatal error:') {
            throw "build exit $buildExit"
        }
        $buildOk = $true

        $qemuArgs = @(
            '-display', 'none', '-monitor', 'none', '-no-reboot', '-snapshot',
            '-m', $QemuMemory, '-smp', "$QemuSmp",
            '-serial', "file:$serialLog",
            '-drive', "format=raw,file=$NativeBin\ucore.img",
            '-drive', "format=raw,file=$NativeBin\swap.img,media=disk,cache=writeback",
            '-drive', "format=raw,file=$NativeBin\sfs.img,media=disk,cache=writeback"
        )
        $qemuProcess = Start-Process -FilePath $Qemu -ArgumentList $qemuArgs `
            -RedirectStandardOutput $qemuOut -RedirectStandardError $qemuErr `
            -PassThru -WindowStyle Hidden

        $qemuOk = Wait-FileMarker $serialLog 'user-test-result: status=' `
            $qemuProcess $QemuTimeoutSeconds
        $serial = if (Test-Path -LiteralPath $serialLog) {
            Get-Content -LiteralPath $serialLog -Raw -ErrorAction SilentlyContinue
        } else { '' }
        $statusMatch = [regex]::Match($serial, 'user-test-result: status=([^\r\n]+)')
        $statusValue = if ($statusMatch.Success) {
            $statusMatch.Groups[1].Value.TrimEnd('.')
        } else { '' }
        if ($statusMatch.Success -and $statusValue -eq '0') {
            $status = 'PASS'
            $detail = 'status=0'
        } elseif ($statusMatch.Success) {
            $status = 'FAIL'
            $detail = "status=$statusValue"
        } elseif ($serial -match 'Triple fault|kernel panic at|user panic at|killed by kernel') {
            $status = 'FAIL'
            $detail = 'panic/fault before result marker'
        } elseif ($qemuOk) {
            $status = 'FAIL'
            $detail = 'marker unreadable'
        } else {
            $status = 'TIMEOUT'
            $detail = 'result marker not observed'
        }
    } catch {
        $detail = $_.Exception.Message
        if (-not $buildOk) {
            $status = 'BUILD_FAIL'
        } elseif (-not $qemuOk) {
            $status = 'TIMEOUT'
        }
    } finally {
        Stop-ProcessTree $qemuProcess
        Stop-ProcessTree $buildProcess
    }

    $results += [pscustomobject]@{
        Test = $Test
        Build = if ($buildOk) { 'PASS' } else { 'FAIL' }
        Qemu = if ($qemuOk) { 'MARKER' } else { 'STOPPED' }
        Result = $status
        Detail = $detail
        SerialLog = $serialLog
    }
    $results[-1] | Format-Table -AutoSize | Out-String | Write-Host
}

Write-Host 'LTP-style summary:'
$results | Format-Table Test, Build, Qemu, Result, Detail -AutoSize
$summaryPath = Join-Path $LogDir 'summary.csv'
$results | Export-Csv -NoTypeInformation -Encoding ASCII -Path $summaryPath
Write-Host "summary: $summaryPath"

if ($results.Result -contains 'FAIL' -or
    $results.Result -contains 'BUILD_FAIL' -or
    $results.Result -contains 'TIMEOUT') {
    exit 1
}
exit 0
