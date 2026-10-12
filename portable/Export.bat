@echo off
setlocal EnableExtensions
title LuminAMI portable export
set "HERE=%~dp0"
if not exist "%HERE%LuminAMI.exe" (
    echo LuminAMI.exe is not next to this file.
    echo Extract the whole ZIP into a folder first, then run Export.bat from there.
    pause
    exit /b 1
)
"%SystemRoot%\System32\fltmc.exe" >nul 2>&1
if errorlevel 1 (
    echo Asking Windows for administrator rights...
    set "LUMINAMI_PORTABLE_BAT=%~f0"
    powershell.exe -NoProfile -Command "Start-Process -FilePath $env:LUMINAMI_PORTABLE_BAT -Verb RunAs"
    if errorlevel 1 (
        echo Administrator rights were not granted, so nothing was read.
        pause
    )
    exit /b
)
rem Everything stays in this folder: drivers, driver choice and results.
set "LUMINAMI_CONFIG_DIR=%HERE%state"
for /f "usebackq delims=" %%i in (`powershell.exe -NoProfile -Command "Get-Date -Format yyyyMMdd-HHmmss"`) do set "STAMP=%%i"
set "OUT=%HERE%export-%STAMP%"
mkdir "%OUT%"
if errorlevel 1 (
    echo Could not create "%OUT%". Move this folder somewhere writable and try again.
    pause
    exit /b 1
)
"%HERE%LuminAMI.exe" --version > "%OUT%\export-log.txt" 2>&1
"%HERE%LuminAMI.exe" probe > "%OUT%\probe.json" 2>&1
echo Reading BIOS settings. This is read only: nothing is written to the BIOS.
"%HERE%LuminAMI.exe" export --install-drivers --non-interactive --capture "%OUT%\capture" --script "%OUT%\BIOSSettings.txt" --dupes "%OUT%\Dupes.txt" --report "%OUT%\export.json" >> "%OUT%\export-log.txt" 2>&1
set "RESULT=%errorlevel%"
type "%OUT%\export-log.txt"
echo.
if "%RESULT%"=="0" (
    echo Export worked. Your settings are in:
    echo   "%OUT%\BIOSSettings.txt"
) else (
    echo Export failed. Send these two files from "%OUT%":
    echo   export-log.txt
    echo   probe.json
)
pause
exit /b %RESULT%
