@echo off
taskkill /F /IM MediaSwitcher.exe 2>nul
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.11.1\msvc2022_64"
if errorlevel 1 exit /b %errorlevel%
cmake --build build --config Release
if errorlevel 1 exit /b %errorlevel%
if exist "build\bin\Release\MediaSwitcher.exe" (
    C:\Qt\6.11.1\msvc2022_64\bin\windeployqt.exe --no-system-d3d-compiler --no-compiler-runtime build\bin\Release\MediaSwitcher.exe
    rem Deploy Qt Multimedia backend plugins (windeployqt misses these)
    if not exist "build\bin\Release\plugins\multimedia" mkdir "build\bin\Release\plugins\multimedia"
    copy /Y "C:\Qt\6.11.1\msvc2022_64\plugins\multimedia\ffmpegmediaplugin.dll" "build\bin\Release\plugins\multimedia\" >nul
    copy /Y "C:\Qt\6.11.1\msvc2022_64\plugins\multimedia\windowsmediaplugin.dll" "build\bin\Release\plugins\multimedia\" >nul
    echo Multimedia plugins deployed.
)
