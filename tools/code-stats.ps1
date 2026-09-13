[CmdletBinding()]
param(
    [string]$ProjectPath = '',
    [string]$BaselinePath = '',
    [string]$ConfigPath = ''
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectPath)) {
    $ProjectPath = Split-Path -Parent $PSScriptRoot
}
$ProjectPath = (Resolve-Path -LiteralPath $ProjectPath).Path

if ([string]::IsNullOrWhiteSpace($BaselinePath)) {
    $answerRoot = Split-Path -Parent $ProjectPath
    $BaselinePath = Join-Path $answerRoot 'lab8_result_copy'
}
$BaselinePath = (Resolve-Path -LiteralPath $BaselinePath).Path

if ([string]::IsNullOrWhiteSpace($ConfigPath)) {
    $ConfigPath = Join-Path $PSScriptRoot 'code-stats-config.psd1'
}
$Config = Import-PowerShellDataFile -LiteralPath $ConfigPath
$CodeExtensions = @($Config.CodeExtensions | ForEach-Object { $_.ToLowerInvariant() })
$ExcludedDirectories = @($Config.ExcludedDirectories)

function Test-IncludedSource([System.IO.FileInfo]$File, [string]$Root) {
    $relative = $File.FullName.Substring($Root.Length + 1).Replace('\', '/')
    $normalized = $relative.ToLowerInvariant()
    foreach ($excluded in $ExcludedDirectories) {
        $excludedPrefix = (Format-PatternPath $excluded).TrimEnd('/') + '/'
        if ($normalized.StartsWith($excludedPrefix)) {
            return $false
        }
    }
    return ($CodeExtensions -contains $File.Extension.ToLowerInvariant()) -or
           ($File.Name -eq 'Makefile' -and $File.Directory.FullName -eq $Root)
}

function Get-SourceRows([string]$Root) {
    $rows = @()
    Get-ChildItem -LiteralPath $Root -Recurse -File | ForEach-Object {
        if (-not (Test-IncludedSource $_ $Root)) {
            return
        }
        $lines = @(Get-Content -LiteralPath $_.FullName)
        $relative = $_.FullName.Substring($Root.Length + 1).Replace('\', '/')
        $rows += [pscustomobject]@{
            Path = $relative
            Lines = $lines.Count
            NonBlank = @($lines | Where-Object { $_.Trim().Length -gt 0 }).Count
        }
    }
    return $rows
}

function Sum-Rows($Rows, [string]$Property) {
    $value = ($Rows | Measure-Object -Property $Property -Sum).Sum
    if ($null -eq $value) { return 0 }
    return [int]$value
}

function Format-PatternPath([string]$Path) {
    return $Path.Replace('\', '/').Trim('/').ToLowerInvariant()
}

function Test-CategoryPath([string]$Path, [string[]]$Patterns) {
    $normalized = Format-PatternPath $Path
    foreach ($pattern in $Patterns) {
        if ($normalized -like (Format-PatternPath $pattern)) { return $true }
    }
    return $false
}

function Get-CategoryRows($Rows, [string[]]$Patterns) {
    return @($Rows | Where-Object { Test-CategoryPath $_.Path $Patterns })
}

function Get-DiffCounts([string]$OldRoot, [string]$NewRoot) {
    $added = 0
    $deleted = 0
    $fileCount = 0
    # Compare only source-bearing directories. Comparing the two project
    # roots directly would also traverse generated images, object files, and
    # QEMU logs below target/native.
    $relativeRoots = @('boot', 'kern', 'libs', 'tools', 'user',
                       'target/native/compat')
    foreach ($relativeRoot in $relativeRoots) {
        $oldRootArg = Join-Path $OldRoot $relativeRoot
        $newRootArg = Join-Path $NewRoot $relativeRoot
        $temporaryOldRoot = $null
        if (-not (Test-Path -LiteralPath $oldRootArg)) {
            $temporaryOldRoot = Join-Path ([IO.Path]::GetTempPath()) `
                ('code-stats-' + [Guid]::NewGuid().ToString('N'))
            New-Item -ItemType Directory -Path $temporaryOldRoot | Out-Null
            $oldRootArg = $temporaryOldRoot
        }
        if (-not (Test-Path -LiteralPath $newRootArg)) {
            if ($null -ne $temporaryOldRoot) {
                Remove-Item -LiteralPath $temporaryOldRoot -Recurse -Force
            }
            continue
        }

        # no-index returns 1 when differences exist. Its numstat output is
        # still the useful result; stderr is suppressed because Git emits
        # benign line-ending advice for this Windows working tree.
        $previousErrorActionPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            $diffLines = @(& git diff --no-index --numstat --no-ext-diff -- $oldRootArg $newRootArg 2>$null)
        }
        finally {
            $ErrorActionPreference = $previousErrorActionPreference
        }
        foreach ($line in $diffLines) {
            $fields = $line -split "`t", 3
            if ($fields.Count -lt 3 -or
                $fields[0] -notmatch '^\d+$' -or
                $fields[1] -notmatch '^\d+$') {
                continue
            }
            $pathInfo = $fields[2]
            $candidate = $pathInfo.Trim('"')
            if ($pathInfo -match '=>\s*"?([^"].*?)"?\s*$') {
                $candidate = $Matches[1]
            }
            $extension = [IO.Path]::GetExtension($candidate).ToLowerInvariant()
            $lineAdded = [int]$fields[0]
            $lineDeleted = [int]$fields[1]
            if ($CodeExtensions -contains $extension) {
                $added += $lineAdded
                $deleted += $lineDeleted
                $fileCount++
            }
        }
        if ($null -ne $temporaryOldRoot) {
            Remove-Item -LiteralPath $temporaryOldRoot -Recurse -Force
        }
    }

    # Include the root Makefile without opening the project root and
    # accidentally including generated artifacts.
    $oldMakefile = Join-Path $OldRoot 'Makefile'
    $newMakefile = Join-Path $NewRoot 'Makefile'
    if (Test-Path -LiteralPath $newMakefile) {
        if (-not (Test-Path -LiteralPath $oldMakefile)) {
            $oldMakefile = [IO.Path]::GetTempFileName()
            Remove-Item -LiteralPath $oldMakefile -Force
        }
        $previousErrorActionPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            $diffLines = @(& git diff --no-index --numstat --no-ext-diff -- $oldMakefile $newMakefile 2>$null)
        }
        finally {
            $ErrorActionPreference = $previousErrorActionPreference
        }
        foreach ($line in $diffLines) {
            $fields = $line -split "`t", 3
            if ($fields.Count -lt 3 -or
                $fields[0] -notmatch '^\d+$' -or
                $fields[1] -notmatch '^\d+$') {
                continue
            }
            $lineAdded = [int]$fields[0]
            $lineDeleted = [int]$fields[1]
            $added += $lineAdded
            $deleted += $lineDeleted
            $fileCount++
        }
    }
    return [pscustomobject]@{
        Files = $fileCount
        Added = $added
        Deleted = $deleted
        Net = $added - $deleted
    }
}

$currentRows = @(Get-SourceRows $ProjectPath)
$baselineRows = @(Get-SourceRows $BaselinePath)
$currentLines = Sum-Rows $currentRows Lines
$baselineLines = Sum-Rows $baselineRows Lines
$currentNonBlank = Sum-Rows $currentRows NonBlank
$baselineNonBlank = Sum-Rows $baselineRows NonBlank
$diff = Get-DiffCounts $BaselinePath $ProjectPath

Write-Output 'Code statistics'
Write-Output ('Project : ' + $ProjectPath)
Write-Output ('Baseline: ' + $BaselinePath)
Write-Output ('Config  : ' + (Resolve-Path -LiteralPath $ConfigPath).Path)
Write-Output ''
Write-Output 'Project source LOC (C/C++ headers, C, assembly, fixtures, and Makefile)'
@(
    [pscustomobject]@{ Tree = 'original'; Files = $baselineRows.Count; Lines = $baselineLines; NonBlank = $baselineNonBlank }
    [pscustomobject]@{ Tree = 'current'; Files = $currentRows.Count; Lines = $currentLines; NonBlank = $currentNonBlank }
    [pscustomobject]@{ Tree = 'net change'; Files = $currentRows.Count - $baselineRows.Count; Lines = $currentLines - $baselineLines; NonBlank = $currentNonBlank - $baselineNonBlank }
) | Format-Table -AutoSize

Write-Output ('Diff additions/deletions for source extensions: files={0}, added={1}, deleted={2}, net={3}' -f
    $diff.Files, $diff.Added, $diff.Deleted, $diff.Net)
Write-Output ''
Write-Output 'Ported or directly adapted source categories'
$categoryRows = @()
foreach ($category in $Config.PortedCategories.Keys) {
    $rows = @(Get-CategoryRows $currentRows @($Config.PortedCategories[$category]))
    $categoryRows += [pscustomobject]@{
        Category = $category
        Files = $rows.Count
        Lines = Sum-Rows $rows Lines
        NonBlank = Sum-Rows $rows NonBlank
    }
}
$categoryRows | Format-Table -AutoSize
$portedLines = Sum-Rows $categoryRows Lines
$portedNonBlank = Sum-Rows $categoryRows NonBlank
Write-Output ('Ported/adapted subtotal (categories above): lines={0}, nonblank={1}' -f
    $portedLines, $portedNonBlank)
Write-Output ('Remaining source LOC after category subtotal: lines={0}, nonblank={1}' -f
    ($currentLines - $portedLines), ($currentNonBlank - $portedNonBlank))
