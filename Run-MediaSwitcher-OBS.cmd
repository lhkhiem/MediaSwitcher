@echo off
setlocal

set "APP_DIR=%~dp0build-obs\obs-runtime-stage\bin\64bit"
set "APP_EXE=%APP_DIR%\MediaSwitcher.exe"

if not exist "%APP_EXE%" (
    echo MediaSwitcher.exe was not found.
    echo Build it first with:
    echo   cmake --build build-obs --config Release --parallel 4
    pause
    exit /b 1
)

pushd "%APP_DIR%"
start "" "%APP_EXE%"
popd
