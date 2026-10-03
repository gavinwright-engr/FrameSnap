[CmdletBinding()]
param(
    [string]$ExecutablePath = '',
    [string]$ShortcutHotkey = 'CTRL+ALT+S',
    [switch]$StartWithWindows,
    [switch]$DesktopShortcut
)
$ErrorActionPreference = 'Stop'
# Windows PowerShell -File can bind parameter defaults before PSScriptRoot is
# initialized. Resolve the packaged executable only after entering the script.
if ([string]::IsNullOrWhiteSpace($ExecutablePath)) {
    $ExecutablePath = Join-Path $PSScriptRoot 'FrameSnap.exe'
}
$source = (Resolve-Path -LiteralPath $ExecutablePath).Path
$installDir = Join-Path $env:LOCALAPPDATA 'Programs\FrameSnap'
$target = Join-Path $installDir 'FrameSnap.exe'
$programs = Join-Path ([Environment]::GetFolderPath('Programs')) 'FrameSnap'
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\FrameSnap'
$marker = Join-Path $installDir 'installation.json'
$old = if (Test-Path -LiteralPath $marker) { Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json } else { $null }
if ((Test-Path -LiteralPath $target) -and -not $old) {
    throw 'The destination already contains an unmanaged FrameSnap.exe. Move it aside before installing.'
}
if ($old -and $old.Product -ne 'FrameSnap') { throw 'The destination belongs to an unrecognized installation.' }
foreach ($name in @('Uninstall.ps1')) {
    if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot $name))) { throw "Missing installer companion: $name" }
}
$shell = New-Object -ComObject WScript.Shell
$plannedLinks = @('Capture.lnk', 'Settings.lnk', 'Run in background.lnk', 'Quit.lnk') | ForEach-Object { Join-Path $programs $_ }
if ($DesktopShortcut) { $plannedLinks += Join-Path ([Environment]::GetFolderPath('Desktop')) 'FrameSnap.lnk' }
foreach ($link in $plannedLinks) {
    if ((Test-Path -LiteralPath $link) -and $shell.CreateShortcut($link).TargetPath -ne $target) {
        throw "A shortcut belonging to another installation exists at $link"
    }
}
$existingStartup = (Get-ItemProperty -Path $runKey -Name FrameSnap -ErrorAction SilentlyContinue).FrameSnap
$ownedStartupCommands = @(
    ('"' + $source + '"'), ('"' + $source + '" --background'),
    ('"' + $target + '"'), ('"' + $target + '" --background')
)
$ownsStartup = $existingStartup -and $ownedStartupCommands -contains $existingStartup
$enableStartup = [bool]$StartWithWindows
if (-not $PSBoundParameters.ContainsKey('StartWithWindows') -and $ownsStartup) {
    $enableStartup = $true
}
if ($enableStartup -and ('"' + $target + '" --background').Length -gt 260) {
    throw 'This installation path exceeds the Windows startup command limit. Install without -StartWithWindows.'
}
# Ask the existing instance to finish saves and quit; never terminate it forcibly.
$process = Start-Process -FilePath $source -ArgumentList '--quit' -Wait -PassThru
if ($process.ExitCode -ne 0) { throw 'FrameSnap is still closing or saving. Try again when it has finished.' }
New-Item -ItemType Directory -Force -Path $installDir, $programs | Out-Null
if ($source -ne $target) {
    $staged = Join-Path $installDir ('FrameSnap.' + [Guid]::NewGuid().ToString('N') + '.new')
    try {
        Copy-Item -LiteralPath $source -Destination $staged
        if ((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $staged).Hash) { throw 'The copied executable failed its integrity check.' }
        Move-Item -LiteralPath $staged -Destination $target -Force
    } finally {
        if (Test-Path -LiteralPath $staged) { Remove-Item -LiteralPath $staged }
    }
}
foreach ($name in @('Uninstall.ps1')) {
    $file = Join-Path $PSScriptRoot $name
    if (-not (Test-Path -LiteralPath $file)) { throw "Missing installer companion: $name" }
    $destination = Join-Path $installDir $name
    if ([IO.Path]::GetFullPath($file) -ne [IO.Path]::GetFullPath($destination)) { Copy-Item -LiteralPath $file -Destination $destination -Force }
}
# The optional batch helper stays in the downloaded package, outside the files
# the uninstaller removes. Retire the installed helper from older versions.
if ($old -and (Test-Path -LiteralPath (Join-Path $installDir 'Uninstall.cmd'))) {
    Remove-Item -LiteralPath (Join-Path $installDir 'Uninstall.cmd')
}
$links = @()
function Add-FrameSnapShortcut([string]$Path, [string]$Arguments, [string]$Hotkey = '') {
    if (Test-Path -LiteralPath $Path) {
        $existing = $shell.CreateShortcut($Path)
        if ($existing.TargetPath -ne $target) { throw "A shortcut belonging to another installation exists at $Path" }
    }
    $shortcut = $shell.CreateShortcut($Path)
    $shortcut.TargetPath = $target
    $shortcut.Arguments = $Arguments
    $shortcut.WorkingDirectory = $installDir
    $shortcut.Description = 'FrameSnap - capture a region and copy it'
    $shortcut.Hotkey = $Hotkey
    $shortcut.Save()
}
# A shell shortcut receives the hotkey when FrameSnap is not running. Do not
# reserve the same shortcut in Explorer when opting into resident hotkey mode.
$hotkey = if ($enableStartup) { '' } else { $ShortcutHotkey }
foreach ($entry in @(
    @('Capture.lnk', '--capture', $hotkey),
    @('Settings.lnk', '--settings', ''),
    @('Run in background.lnk', '--background', ''),
    @('Quit.lnk', '--quit', '')
)) {
    $link = Join-Path $programs $entry[0]
    Add-FrameSnapShortcut $link $entry[1] $entry[2]
    $links += $link
}
if ($DesktopShortcut) {
    $link = Join-Path ([Environment]::GetFolderPath('Desktop')) 'FrameSnap.lnk'
    Add-FrameSnapShortcut $link '--capture'
    $links += $link
}
# Retain ownership of shortcuts created by earlier installs for safe removal.
if ($old) { $links += @($old.Shortcuts) }
@{ Product = 'FrameSnap'; Version = '0.2.1'; Shortcuts = @($links | Select-Object -Unique) } |
    ConvertTo-Json | Set-Content -LiteralPath $marker -Encoding UTF8
if ($enableStartup) {
    # CreateSubKey creates missing parents and preserves existing keys/values.
    ([Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('Software\Microsoft\Windows\CurrentVersion\Run')).Dispose()
    New-ItemProperty -Path $runKey -Name FrameSnap -Value ('"' + $target + '" --background') -PropertyType String -Force | Out-Null
} else {
    $existing = (Get-ItemProperty -Path $runKey -Name FrameSnap -ErrorAction SilentlyContinue).FrameSnap
    if ($existing -and $ownedStartupCommands -contains $existing) { Remove-ItemProperty -Path $runKey -Name FrameSnap }
}
([Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('Software\Microsoft\Windows\CurrentVersion\Uninstall\FrameSnap')).Dispose()
$uninstall = '"' + (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe') + '" -NoProfile -ExecutionPolicy Bypass -File "' + (Join-Path $installDir 'Uninstall.ps1') + '"'
@{ DisplayName = 'FrameSnap'; DisplayVersion = '0.2.1'; InstallLocation = $installDir; DisplayIcon = $target; UninstallString = $uninstall } |
    ForEach-Object { foreach ($name in $_.Keys) { New-ItemProperty -Path $uninstallKey -Name $name -Value $_[$name] -PropertyType String -Force | Out-Null } }
New-ItemProperty -Path $uninstallKey -Name NoModify -Value 1 -PropertyType DWord -Force | Out-Null
New-ItemProperty -Path $uninstallKey -Name NoRepair -Value 1 -PropertyType DWord -Force | Out-Null
Write-Host "Installed for this user: $target"
if ($enableStartup) {
    Start-Process -FilePath $target -ArgumentList '--background'
    Write-Host 'Background mode is enabled; startup stays in the tray.'
} else {
    Write-Host "On-demand mode: press $ShortcutHotkey or use Start > FrameSnap > Capture. Nothing runs at sign-in."
}
Write-Host 'Settings and uninstall are available from Start and Windows Installed apps. Captures and settings are kept when uninstalling.'
