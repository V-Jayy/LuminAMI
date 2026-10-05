@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\export.ps1" %*
exit /b %errorlevel%
