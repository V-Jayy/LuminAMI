@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\install-drivers.ps1" %*
exit /b %errorlevel%
