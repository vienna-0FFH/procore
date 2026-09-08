param(
    [string]$Source = 'user/hello.c',
    [string]$Name = '',
    [string]$TinyCcWslPath = '',
    [switch]$BuildRuntime
)

$ErrorActionPreference = 'Stop'
$Project = Split-Path -Parent $PSScriptRoot
$ConfigPath = Join-Path $PSScriptRoot 'tcc-config.psd1'
$Config = if (Test-Path -LiteralPath $ConfigPath) {
    Import-PowerShellDataFile -LiteralPath $ConfigPath
} else { @{} }

if ([string]::IsNullOrWhiteSpace($TinyCcWslPath)) {
    $TinyCcWslPath = [string]$Config.TinyCcWslPath
}
if ([string]::IsNullOrWhiteSpace($TinyCcWslPath)) {
    throw 'TinyCC path is empty; set tools/tcc-config.psd1 or pass -TinyCcWslPath.'
}

$sourcePath = if ([IO.Path]::IsPathRooted($Source)) {
    $Source
} else {
    Join-Path $Project $Source
}
if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
    throw "C source not found: $sourcePath"
}

if ([string]::IsNullOrWhiteSpace($Name)) {
    $Name = [IO.Path]::GetFileNameWithoutExtension($sourcePath)
}
$native = Join-Path $Project 'target/native'
$objDir = Join-Path $native 'tcc'
$objPath = Join-Path $objDir "$Name-tcc.o"
$elfPath = Join-Path $objDir "$Name-tcc.elf"
New-Item -ItemType Directory -Force -Path $objDir | Out-Null

function Convert-ToWslPath([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if ($full -match '^([A-Za-z]):\\(.*)$') {
        return "/mnt/$($Matches[1].ToLowerInvariant())/" +
            ($Matches[2] -replace '\\', '/')
    }
    throw "Only Windows drive paths can be mapped to WSL: $Path"
}

if ($BuildRuntime) {
    $nativeBuild = Join-Path $Project 'tools/build-native.cmd'
    & $nativeBuild
    if ($LASTEXITCODE -ne 0) {
        throw "uCore runtime build failed: $LASTEXITCODE"
    }
}

$srcWsl = Convert-ToWslPath $sourcePath
$objWsl = Convert-ToWslPath $objPath
$libsWsl = Convert-ToWslPath (Join-Path $Project 'libs')
$userLibsWsl = Convert-ToWslPath (Join-Path $Project 'user/libs')
$userIncludeWsl = Convert-ToWslPath (Join-Path $Project 'user/include')

& wsl.exe -d Ubuntu -- $TinyCcWslPath `
    '-m32' '-nostdinc' "-I$libsWsl" "-I$userLibsWsl" "-I$userIncludeWsl" `
    '-c' $srcWsl '-o' $objWsl
if ($LASTEXITCODE -ne 0) {
    throw "TinyCC failed to compile ${sourcePath}: $LASTEXITCODE"
}

$ld = 'C:\Program Files (x86)\Android\AndroidNDK\android-ndk-r23c\toolchains\llvm\prebuilt\windows-x86_64\bin\ld.lld.exe'
$readelf = 'C:\Program Files (x86)\Android\AndroidNDK\android-ndk-r23c\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-readelf.exe'
$linkerScript = Join-Path $Project 'target/native/compat/user.ld'
$runtimeObjects = @(
    Get-ChildItem -LiteralPath (Join-Path $native 'obj/user/libs') -Filter '*.o' -File | Sort-Object FullName
    Get-ChildItem -LiteralPath (Join-Path $native 'obj/libs') -Filter '*.o' -File | Sort-Object FullName
)
$linkObjects = @($runtimeObjects.FullName) + $objPath
& $ld '-m' 'elf_i386' '-nostdlib' '-T' $linkerScript '-o' $elfPath @linkObjects
if ($LASTEXITCODE -ne 0) {
    throw "LLD failed to link ${elfPath}: $LASTEXITCODE"
}

$header = & $readelf '-h' $elfPath
$programHeaders = & $readelf '-l' $elfPath
$loadCount = @($programHeaders | Select-String '\bLOAD\b').Count
if ($LASTEXITCODE -ne 0 -or
    ((($header -join "`n") -notmatch 'ELF32')) -or
    ((($header -join "`n") -notmatch 'Intel 80386')) -or
    ($loadCount -lt 2)) {
    throw "Generated file is not a valid uCore ELF32 image: $elfPath"
}

Write-Host "TCC object: $objPath"
Write-Host "uCore ELF32: $elfPath"
$header | Select-String 'Class|Machine|Entry point'
$programHeaders | Select-String 'LOAD'
