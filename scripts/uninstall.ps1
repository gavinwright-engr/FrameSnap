[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$installDir = Join-Path $env:LOCALAPPDATA 'Programs\FrameSnap'
if ([IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\') -ne [IO.Path]::GetFullPath($installDir).TrimEnd('\')) {
    throw 'Run the installed uninstaller from Windows Installed apps or the FrameSnap installation folder.'
}
$target = Join-Path $installDir 'FrameSnap.exe'
$marker = Join-Path $installDir 'installation.json'
if (-not (Test-Path -LiteralPath $marker)) { throw 'Installation metadata is missing; no files were removed.' }
$metadata = Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json
if ($metadata.Product -ne 'FrameSnap') { throw 'Unrecognized installation; no files were removed.' }
if (Test-Path -LiteralPath $target) {
    $process = Start-Process -FilePath $target -ArgumentList '--quit' -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw 'FrameSnap is still saving. Wait for it to finish before uninstalling.' }
}
$shell = New-Object -ComObject WScript.Shell
$programs = Join-Path ([Environment]::GetFolderPath('Programs')) 'FrameSnap'
$allowed = @('Capture.lnk', 'Settings.lnk', 'Run in background.lnk', 'Quit.lnk') | ForEach-Object { Join-Path $programs $_ }
$allowed += Join-Path ([Environment]::GetFolderPath('Desktop')) 'FrameSnap.lnk'
foreach ($link in @($metadata.Shortcuts)) {
    if ($allowed -contains $link -and (Test-Path -LiteralPath $link)) {
        if ($shell.CreateShortcut($link).TargetPath -eq $target) { Remove-Item -LiteralPath $link }
    }
}
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$existing = (Get-ItemProperty -Path $runKey -Name FrameSnap -ErrorAction SilentlyContinue).FrameSnap
if ($existing -eq ('"' + $target + '" --background')) { Remove-ItemProperty -Path $runKey -Name FrameSnap }
$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\FrameSnap'
if ((Get-ItemProperty -Path $uninstallKey -ErrorAction SilentlyContinue).InstallLocation -eq $installDir) {
    Remove-Item -LiteralPath $uninstallKey -Recurse
}
# Remove only our known installation files. Never recursively delete user data.
foreach ($name in @('FrameSnap.exe', 'installation.json', 'Uninstall.cmd', 'Uninstall.ps1')) {
    $file = Join-Path $installDir $name
    if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file }
}
foreach ($directory in @($programs, $installDir)) {
    if ((Test-Path -LiteralPath $directory) -and -not (Get-ChildItem -LiteralPath $directory -Force)) { Remove-Item -LiteralPath $directory }
}
Write-Host 'FrameSnap was removed. Your settings and saved captures were kept.'
