[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [string]$OutputDir = '',
    [switch]$Package,
    [switch]$Test
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'out\build-msvc'
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $candidate = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' | Select-Object -First 1
        if ($candidate) { $cmake = Get-Item -LiteralPath $candidate }
    }
}
if (-not $cmake) { throw 'Install Visual Studio 2022 or later with Desktop development with C++ and CMake tools, or add CMake 3.25+ to PATH.' }
$cmakeExe = if ($cmake.Source) { $cmake.Source } else { $cmake.FullName }
& $cmakeExe -S $root -B $build -A x64 -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed. Use the Visual Studio C++ toolchain with the Windows SDK.' }
& $cmakeExe --build $build --config $Configuration --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
if ($Test) {
    $ctest = Join-Path (Split-Path $cmakeExe) 'ctest.exe'
    & $ctest --test-dir $build -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
}
if (-not $OutputDir) { $OutputDir = Join-Path 'out' $Configuration }
$output = if ([IO.Path]::IsPathRooted($OutputDir)) { $OutputDir } else { Join-Path $root $OutputDir }
New-Item -ItemType Directory -Force -Path $output | Out-Null
Copy-Item -LiteralPath (Join-Path $build "$Configuration\FrameSnap.exe") -Destination $output -Force
if ($Package) {
    $cpack = Join-Path (Split-Path $cmakeExe) 'cpack.exe'
    & $cpack --config (Join-Path $build 'CPackConfig.cmake') -C $Configuration -B (Join-Path $root 'out\packages')
    if ($LASTEXITCODE -ne 0) { throw 'Packaging failed.' }
    Get-ChildItem -LiteralPath (Join-Path $root 'out\packages') -Filter '*.zip' | ForEach-Object {
        $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        [IO.File]::WriteAllText(($_.FullName + '.sha256'), ($hash + '  ' + $_.Name + "`n"))
    }
}
Write-Host "Built $(Join-Path $output 'FrameSnap.exe')"
