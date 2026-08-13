@echo off
setlocal

cd /d "%~dp0"
cmake --build build-obs --config Release --parallel 4
if errorlevel 1 (
    echo.
    echo OBS Release build failed.
    pause
    exit /b 1
)

echo.
echo OBS Release build completed successfully.
pause
