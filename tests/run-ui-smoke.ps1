param(
    [string]$WindhawkRoot = (Join-Path $env:ProgramFiles 'Windhawk')
)
$ErrorActionPreference = 'Stop'
$compilerRoot = Join-Path $WindhawkRoot 'Compiler'
$compiler = Join-Path $compilerRoot 'bin\clang++.exe'
$outputDirectory = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\build-ui-smoke'))
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$output = Join-Path $outputDirectory 'ui-smoke.exe'
$source = Join-Path $PSScriptRoot 'ui-smoke.cpp'
Push-Location $compilerRoot
try {
    & $compiler '-std=c++23' '-O1' '-static' '-municode' '-DUNICODE' '-D_UNICODE' `
        '-DWH_MOD' '-DWH_EDITING' '-DWIN32_LEAN_AND_MEAN' `
        '-DWINVER=0x0A00' '-D_WIN32_WINNT=0x0A00' '-D_WIN32_IE=0x0A00' `
        '-target' 'x86_64-w64-mingw32' $source '-o' $output `
        '-lole32' '-loleaut32' '-lruntimeobject' '-lpdh' '-ldxgi' '-lcomctl32' '-lgdi32' '-lgdiplus' '-liphlpapi'
    if ($LASTEXITCODE -ne 0) { throw "UI smoke build failed: $LASTEXITCODE" }
} finally {
    Pop-Location
}
# Embed the Windows compatibility manifest into this freshly built test exe.
# This does not edit system settings or the Windhawk/Explorer process.
if (-not ('TaskbarUiSmokeManifest' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class TaskbarUiSmokeManifest {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern IntPtr BeginUpdateResource(string path, bool deleteExisting);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern bool UpdateResource(IntPtr update, IntPtr type, IntPtr name,
                                             ushort language, byte[] data, uint size);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool EndUpdateResource(IntPtr update, bool discard);
}
'@
}
$manifestBytes = [System.IO.File]::ReadAllBytes((Join-Path $PSScriptRoot 'ui-smoke.manifest'))
$update = [TaskbarUiSmokeManifest]::BeginUpdateResource($output, $false)
if ($update -eq [IntPtr]::Zero) { throw 'Opening manifest resource failed' }
$success = [TaskbarUiSmokeManifest]::UpdateResource(
    $update, [IntPtr]24, [IntPtr]1, 0, $manifestBytes, [uint32]$manifestBytes.Length)
$ended = [TaskbarUiSmokeManifest]::EndUpdateResource($update, -not $success)
if (-not $success -or -not $ended) { throw 'Embedding manifest failed' }
& $output $outputDirectory
if ($LASTEXITCODE -ne 0) { throw "UI smoke failed: $LASTEXITCODE" }
