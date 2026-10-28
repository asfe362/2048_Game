@echo off
rem ---------------------------------------------------------------
rem  Build & run: 2048 (Win32 / GDI)
rem  Locates the MinGW toolchain shipped with Dev-C++ automatically.
rem  (Keep this file ASCII-only: cmd.exe parses it with the OEM code page)
rem ---------------------------------------------------------------
setlocal
set "DEVCDIR=C:\Program Files (x86)\Dev-Cpp\MinGW64\bin"
if not exist "%DEVCDIR%\g++.exe" set "DEVCDIR=C:\Program Files\Dev-Cpp\MinGW64\bin"
if not exist "%DEVCDIR%\g++.exe" (
    echo [ERROR] Dev-C++ MinGW toolchain not found.
    exit /b 1
)

set "PATH=%DEVCDIR%;%PATH%"
cd /d "%~dp0"

g++ -std=c++14 -Wall -Wextra -O2 -mwindows -o 2048.exe ^
    main.cpp window.cpp game.cpp render.cpp input.cpp
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo [OK] 2048.exe built. Starting...
start "" "2048.exe"
endlocal
