@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.11.1\msvc2022_64"
cmake --build build
C:\Qt\6.11.1\msvc2022_64\bin\windeployqt.exe --no-system-d3d-compiler --no-compiler-runtime build\bin\Debug\MediaSwitcher.exe
