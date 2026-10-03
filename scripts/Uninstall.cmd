@echo off
setlocal
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0Uninstall.ps1" %*
set "FrameSnapExitCode=%ERRORLEVEL%"
if not "%FrameSnapExitCode%"=="0" if not defined CI pause
exit /b %FrameSnapExitCode%
