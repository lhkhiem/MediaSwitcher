@echo off
echo ============================================================
echo  MediaSwitcher - CREATE INSTALLER (Inno Setup)
echo ============================================================

rem Check Inno Setup installed
set ISCC="C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
if not exist %ISCC% (
    echo [ERROR] Inno Setup 6 chua duoc cai dat!
    echo         Tai tai: https://jrsoftware.org/isdl.php
    pause
    exit /b 1
)

rem Check build exists
if not exist "build\bin\Release\MediaSwitcher.exe" (
    echo [ERROR] Chua build! Chay rebuild.bat truoc.
    pause
    exit /b 1
)

rem Create output folder
if not exist "installer\output" mkdir "installer\output"

echo.
echo Dang tao installer...
%ISCC% "installer\setup.iss"
if errorlevel 1 (
    echo [ERROR] Tao installer that bai!
    pause
    exit /b 1
)

echo.
echo ============================================================
echo  DONE: installer\output\MediaSwitcher-Setup-v1.0.0.exe
echo ============================================================
pause
