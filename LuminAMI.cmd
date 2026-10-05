@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\menu.ps1" %*
exit /b %errorlevel%
