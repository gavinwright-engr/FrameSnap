param(
    [string]$Configuration = "Debug",
    [string]$OutputDir = "out\\Debug"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$outputPath = Join-Path $root $OutputDir
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null

$vsDevCmd = "C:\BuildTools\Common7\Tools\VsDevCmd.bat"
$outputExe = Join-Path $outputPath "FrameSnap.exe"

$command = @(
    "call `"$vsDevCmd`" -arch=x64",
    "cl /std:c++20 /EHsc /W4 /permissive- /utf-8 /Zc:__cplusplus /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I src /c src\*.cpp",
    "rc /fo app.res resources\app.rc",
    "link /OUT:`"$outputExe`" /SUBSYSTEM:WINDOWS *.obj app.res user32.lib gdi32.lib comdlg32.lib d3d11.lib dxgi.lib d2d1.lib windowscodecs.lib gdiplus.lib comctl32.lib dwmapi.lib uxtheme.lib shcore.lib shell32.lib ole32.lib advapi32.lib shlwapi.lib winmm.lib",
    "del /q *.obj 2>nul",
    "del /q app.res 2>nul"
) -join " && "

cmd.exe /c $command
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE."
}
