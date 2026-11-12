@echo off
rem Builds ItemXL.dll (the TW3SE plugin) and itemxl-cli.exe (the same core for tests) with Zig's C++ compiler.
rem   python -m pip install --user ziglang      (once)
rem   build.bat
setlocal
cd /d "%~dp0"
set "SDK=..\third_party\TW3SE-SDK\include"
set "SRC=src\util.cpp src\bundle.cpp src\xml.cpp src\yaml.cpp src\model.cpp src\pack.cpp src\reddlc.cpp src\w3strings.cpp src\build.cpp src\script.cpp src\browse.cpp src\cr2w.cpp src\texture.cpp src\appearance.cpp"
set "FLAGS=-std=c++17 -O2 -target x86_64-windows-gnu -Isrc -I%SDK% -Wall -static"
if not exist out mkdir out

if not defined ZIG_GLOBAL_CACHE_DIR set "ZIG_GLOBAL_CACHE_DIR=%CD%\.zig-cache"
if not defined ZIG_LOCAL_CACHE_DIR set "ZIG_LOCAL_CACHE_DIR=%CD%\.zig-cache"

set "PY=python"
if exist "%USERPROFILE%\.local\bin\python3.14.exe" set "PY=%USERPROFILE%\.local\bin\python3.14.exe"
if exist "%APPDATA%\Python\Python314\site-packages\ziglang\zig.exe" (
    set "ZIG=%APPDATA%\Python\Python314\site-packages\ziglang\zig.exe"
) else (
    set "ZIG=%PY% -m ziglang"
)

echo building ItemXL.dll
%ZIG% c++ %FLAGS% -shared -o out\ItemXL.dll %SRC% src\plugin.cpp -lshell32 -lole32 || exit /b 1

echo building itemxl-cli.exe
%ZIG% c++ %FLAGS% -o out\itemxl-cli.exe %SRC% src\cli.cpp || exit /b 1

echo done: out\ItemXL.dll, out\itemxl-cli.exe
