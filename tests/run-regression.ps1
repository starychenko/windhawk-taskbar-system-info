param(
    [string]$WindhawkRoot = (Join-Path $env:ProgramFiles 'Windhawk')
)
$ErrorActionPreference = 'Stop'
$compilerRoot = Join-Path $WindhawkRoot 'Compiler'
$compiler = Join-Path $compilerRoot 'bin\clang++.exe'
$outputDirectory = Join-Path $PSScriptRoot '..\build-regression'
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$output = Join-Path $outputDirectory 'regression.exe'
$source = Join-Path $PSScriptRoot 'regression.cpp'
Push-Location $compilerRoot
try {
    & $compiler '-std=c++23' '-O1' '-g' '-static' '-DUNICODE' '-D_UNICODE' `
        '-DWH_MOD' '-DWH_EDITING' '-DWIN32_LEAN_AND_MEAN' `
        '-DWINVER=0x0A00' '-D_WIN32_WINNT=0x0A00' '-D_WIN32_IE=0x0A00' `
        '-target' 'x86_64-w64-mingw32' $source '-o' $output `
        '-lole32' '-loleaut32' '-lruntimeobject' '-lpdh' '-ldxgi' '-lcomctl32' '-lgdi32' '-lgdiplus' '-liphlpapi'
    if ($LASTEXITCODE -ne 0) { throw "Regression build failed: $LASTEXITCODE" }
} finally {
    Pop-Location
}
& $output
if ($LASTEXITCODE -ne 0) { throw "Regression tests failed: $LASTEXITCODE" }
