[CmdletBinding()]
param([Parameter(Mandatory)][string]$ExecutablePath)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$installDir = Join-Path $env:LOCALAPPDATA 'Programs\FrameSnap'
$programs = Join-Path ([Environment]::GetFolderPath('Programs')) 'FrameSnap'
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\FrameSnap'
if ((Test-Path -LiteralPath $installDir) -or (Test-Path -LiteralPath $programs) -or
    (Get-ItemProperty -Path $runKey -Name FrameSnap -ErrorAction SilentlyContinue) -or
    (Test-Path -LiteralPath $uninstallKey)) {
    throw 'Installer tests require an isolated Windows profile without an existing FrameSnap installation.'
}
function Assert-True($Condition, [string]$Description) {
    if (-not $Condition) { throw "FAIL: $Description" }
    Write-Host "PASS: $Description"
}
$stage = Join-Path ([IO.Path]::GetTempPath()) ('FrameSnap package test ' + [Guid]::NewGuid().ToString('N'))
$sentinel = 'FrameSnapTest_' + [Guid]::NewGuid().ToString('N')
$sentinelAdded = $false
try {
    New-Item -ItemType Directory -Path $stage | Out-Null
    Copy-Item -LiteralPath (Resolve-Path -LiteralPath $ExecutablePath).Path -Destination (Join-Path $stage 'FrameSnap.exe')
    foreach ($name in @('install.ps1', 'uninstall.ps1', 'Install.cmd', 'Uninstall.cmd')) {
        Copy-Item -LiteralPath (Join-Path $root "scripts\$name") -Destination $stage
    }
    if (-not (Test-Path -LiteralPath $runKey)) { New-Item -Path $runKey | Out-Null }
    New-ItemProperty -Path $runKey -Name $sentinel -Value 'Unrelated startup entry - do not remove' -PropertyType String | Out-Null
    $sentinelAdded = $true
    & (Join-Path $stage 'Install.ps1')
    $target = Join-Path $installDir 'FrameSnap.exe'
    Assert-True (Test-Path -LiteralPath $target) 'Per-user install from a path containing spaces'
    Assert-True (-not (Get-ItemProperty -Path $runKey -Name FrameSnap -ErrorAction SilentlyContinue)) 'Sign-in startup is off by default'
    $shell = New-Object -ComObject WScript.Shell
    $capture = $shell.CreateShortcut((Join-Path $programs 'Capture.lnk'))
    Assert-True ($capture.TargetPath -eq $target -and $capture.Arguments -eq '--capture') 'Capture shortcut launches one-shot mode'
    Assert-True ($capture.Hotkey -ieq 'Ctrl+Alt+S') 'Explorer owns the on-demand hotkey'
    $userFile = Join-Path $installDir 'user-notes.txt'
    Set-Content -LiteralPath $userFile -Value 'Keep this user-created file'
    & (Join-Path $stage 'Install.ps1')
    Assert-True (Test-Path -LiteralPath $userFile) 'Reinstall preserves user files'
    $legacyCommand = '"' + (Join-Path $stage 'FrameSnap.exe') + '"'
    New-ItemProperty -Path $runKey -Name FrameSnap -Value $legacyCommand -PropertyType String -Force | Out-Null
    & (Join-Path $stage 'Install.ps1')
    $expectedCommand = '"' + $target + '" --background'
    Assert-True ((Get-ItemProperty -Path $runKey -Name FrameSnap).FrameSnap -eq $expectedCommand) 'Legacy startup is migrated to the installed copy with quiet launch'
    & (Join-Path $stage 'Install.ps1') -StartWithWindows
    Assert-True ((Get-ItemProperty -Path $runKey -Name FrameSnap).FrameSnap -eq $expectedCommand) 'Opt-in startup is quoted and quiet'
    Assert-True ($shell.CreateShortcut((Join-Path $programs 'Capture.lnk')).Hotkey -eq '') 'Resident install does not reserve a duplicate Explorer hotkey'
    & (Join-Path $stage 'Install.ps1')
    Assert-True ((Get-ItemProperty -Path $runKey -Name FrameSnap).FrameSnap -eq $expectedCommand) 'Upgrade preserves the startup preference'
    & (Join-Path $stage 'Install.ps1') -StartWithWindows:$false
    Assert-True (-not (Get-ItemProperty -Path $runKey -Name FrameSnap -ErrorAction SilentlyContinue)) 'Explicit switch back to on-demand removes startup'
    Assert-True ((Get-ItemProperty -Path $runKey -Name $sentinel).$sentinel -eq 'Unrelated startup entry - do not remove') 'Unrelated startup values are preserved'
    $settings = Join-Path $env:LOCALAPPDATA 'FrameSnap\settings.ini'
    $hadSettings = Test-Path -LiteralPath $settings
    & (Join-Path $installDir 'Uninstall.ps1')
    Assert-True (-not (Test-Path -LiteralPath $target)) 'Uninstall removes the executable'
    Assert-True (-not (Test-Path -LiteralPath $uninstallKey)) 'Uninstall removes only its Installed apps entry'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $programs 'Capture.lnk'))) 'Uninstall removes owned shortcuts'
    Assert-True (Test-Path -LiteralPath $userFile) 'Uninstall leaves unknown user files alone'
    if ($hadSettings) { Assert-True (Test-Path -LiteralPath $settings) 'Uninstall preserves settings' }
    Remove-Item -LiteralPath $userFile
    if (-not (Get-ChildItem -LiteralPath $installDir -Force)) { Remove-Item -LiteralPath $installDir }
} finally {
    # Cleanup is restricted to resources created by this test in its disposable profile.
    if (Test-Path -LiteralPath (Join-Path $installDir 'Uninstall.ps1')) {
        & (Join-Path $installDir 'Uninstall.ps1')
    }
    if ($sentinelAdded) { Remove-ItemProperty -Path $runKey -Name $sentinel -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse }
}
