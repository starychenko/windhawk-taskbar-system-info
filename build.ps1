param(
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'build'),
    [string]$WindhawkRoot = (Join-Path $env:ProgramFiles 'Windhawk')
)

$ErrorActionPreference = 'Stop'

$compilerPath = Join-Path $WindhawkRoot 'Compiler\bin\clang++.exe'
$compilerWorkingDirectory = Join-Path $WindhawkRoot 'Compiler'
$sourcePath = Join-Path $PSScriptRoot 'taskbar-system-info.wh.cpp'

foreach ($requiredPath in @($compilerPath, $sourcePath)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required path not found: $requiredPath"
    }
}

$engineRoot = Join-Path $WindhawkRoot 'Engine'
$engineDirectory = Get-ChildItem -LiteralPath $engineRoot -Directory |
    Where-Object {
        $parsedVersion = $null
        [version]::TryParse($_.Name, [ref]$parsedVersion)
    } |
    Sort-Object { [version]$_.Name } -Descending |
    Select-Object -First 1

if (-not $engineDirectory) {
    throw "No Windhawk engine installation found in: $engineRoot"
}

$engineLibrary = Join-Path $engineDirectory.FullName '64\windhawk.lib'
if (-not (Test-Path -LiteralPath $engineLibrary)) {
    throw "Required path not found: $engineLibrary"
}

$source = Get-Content -LiteralPath $sourcePath -Raw
$idMatch = [regex]::Match($source, '(?m)^// @id\s+(\S+)\s*$')
$versionMatch = [regex]::Match($source, '(?m)^// @version\s+(\S+)\s*$')
if (-not $idMatch.Success -or -not $versionMatch.Success) {
    throw 'Mod id or version metadata was not found.'
}

$modId = $idMatch.Groups[1].Value
$modVersion = $versionMatch.Groups[1].Value
$engineVersion = [version]$engineDirectory.Name
$windhawkVersion = '0x{0:X2}{1:X2}{2:X2}00' -f `
    $engineVersion.Major, $engineVersion.Minor, $engineVersion.Build
$outputPath = Join-Path $OutputDirectory "${modId}_${modVersion}.dll"

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$compilerArguments = @(
    '-std=c++23'
    '-O2'
    '-Wall'
    '-Wextra'
    '-ffp-exception-behavior=maytrap'
    '-shared'
    '-DUNICODE'
    '-D_UNICODE'
    '-DWIN32_LEAN_AND_MEAN'
    '-Wno-unneeded-internal-declaration'
    '-DWINVER=0x0A00'
    '-D_WIN32_WINNT=0x0A00'
    '-D_WIN32_IE=0x0A00'
    '-DNTDDI_VERSION=0x0A000008'
    '-D__USE_MINGW_ANSI_STDIO=0'
    '-DWH_MOD'
    ('-DWH_MOD_ID=L"{0}"' -f $modId)
    ('-DWH_MOD_VERSION=L"{0}"' -f $modVersion)
    ("-DWH_WINDHAWK_VERSION=$windhawkVersion")
    $engineLibrary
    '-x'
    'c++'
    $sourcePath
    '-include'
    'windhawk_api.h'
    '-target'
    'x86_64-w64-mingw32'
    '-Wl,--export-all-symbols'
    '-o'
    $outputPath
    '-lole32'
    '-loleaut32'
    '-lruntimeobject'
    '-lpdh'
    '-ldxgi'
    '-lversion'
)

Push-Location $compilerWorkingDirectory
try {
    $compilerOutput = & $compilerPath @compilerArguments 2>&1
    $compilerExitCode = $LASTEXITCODE
    $compilerOutput | ForEach-Object { $_.ToString() }
    if ($compilerExitCode -ne 0) {
        throw "Windhawk compiler failed with exit code $compilerExitCode"
    }
} finally {
    Pop-Location
}

Get-Item -LiteralPath $outputPath
