$ErrorActionPreference = 'Stop'

$compilerRoot = 'C:\Program Files\Windhawk\Compiler'
$compiler = Join-Path $compilerRoot 'bin\clang++.exe'
$source = Join-Path $PSScriptRoot 'metrics-smoke.cpp'
$output = Join-Path $PSScriptRoot 'metrics-smoke.exe'

Push-Location $compilerRoot
try {
    & $compiler `
        '-std=c++23' '-O2' '-DUNICODE' '-D_UNICODE' '-municode' '-static' `
        '-target' 'x86_64-w64-mingw32' `
        $source '-o' $output '-ldxgi' '-lpdh'
    if ($LASTEXITCODE -ne 0) {
        throw "Metrics smoke-test build failed with exit code $LASTEXITCODE"
    }
} finally {
    Pop-Location
}

$savedProcessPath = $env:Path
try {
    $runtimeBin = Join-Path $compilerRoot 'x86_64-w64-mingw32\bin'
    $env:Path = "$runtimeBin;$(Join-Path $compilerRoot 'bin');$savedProcessPath"
    & $output
    if ($LASTEXITCODE -ne 0) {
        throw "Metrics smoke-test failed with exit code $LASTEXITCODE"
    }
} finally {
    $env:Path = $savedProcessPath
}
