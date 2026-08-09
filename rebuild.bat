@echo off
echo ============================================================
echo  MediaSwitcher - CLEAN REBUILD
echo ============================================================

rem Kill running process
taskkill /F /IM MediaSwitcher.exe 2>nul

rem Setup MSVC environment
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

rem Clean all compiled object files (preserve CMake config & downloaded deps)
echo.
echo [1/4] Cleaning compiled objects...
if exist "build\src\MediaSwitcher.dir" (
    rmdir /S /Q "build\src\MediaSwitcher.dir"
    echo     Cleaned: build\src\MediaSwitcher.dir
)
if exist "build\bin" (
    rmdir /S /Q "build\bin"
    echo     Cleaned: build\bin
)

rem CMake configure
echo.
echo [2/4] CMake configure...
cmake -S . -B build -A x64 -Thost=x64 -DCMAKE_PREFIX_PATH="C:\Qt\6.11.1\msvc2022_64"
if errorlevel 1 exit /b %errorlevel%

rem Build
echo.
echo [3/4] Building...
cmake --build build --config Release -j 1
if errorlevel 1 exit /b %errorlevel%

rem Deploy
echo.
echo [4/4] Deploying...
if exist "build\bin\Release\MediaSwitcher.exe" (
    C:\Qt\6.11.1\msvc2022_64\bin\windeployqt.exe --no-system-d3d-compiler --no-compiler-runtime build\bin\Release\MediaSwitcher.exe
    if not exist "build\bin\Release\plugins\multimedia" mkdir "build\bin\Release\plugins\multimedia"
    copy /Y "C:\Qt\6.11.1\msvc2022_64\plugins\multimedia\ffmpegmediaplugin.dll" "build\bin\Release\plugins\multimedia\" >nul
    copy /Y "C:\Qt\6.11.1\msvc2022_64\plugins\multimedia\windowsmediaplugin.dll" "build\bin\Release\plugins\multimedia\" >nul
    echo     Multimedia plugins deployed.
)

echo.
echo ============================================================
echo  BUILD COMPLETE: build\bin\Release\MediaSwitcher.exe
echo ============================================================
