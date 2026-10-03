@echo off
setlocal
rem Let Windows PowerShell build its own module path, even when called from pwsh.
set "PSModulePath="
rem This helper stays in the download folder so uninstall can finish deleting its files.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%LOCALAPPDATA%\Programs\FrameSnap\Uninstall.ps1" %*
set "FrameSnapExitCode=%ERRORLEVEL%"
if not "%FrameSnapExitCode%"=="0" if not defined CI pause
exit /b %FrameSnapExitCode%
