@echo off
setlocal
rem Let Windows PowerShell build its own module path, even when called from pwsh.
set "PSModulePath="
rem Parse the remaining commands before the uninstaller deletes this batch file.
(
    "%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0Uninstall.ps1" %*
    if errorlevel 1 (
        if not defined CI pause
        exit /b 1
    )
    exit /b 0
)
