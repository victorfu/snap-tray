@echo off
REM Build debug version and run SnapTray

setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%SCRIPT_DIR%.."
set "BUILD_DIR=%PROJECT_DIR%\build"

cd /d "%PROJECT_DIR%"

REM Configure if needed
if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo Configuring project...
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
)

REM Build all targets (including tests)
echo Building all targets...
cmake --build build --parallel

REM Run (Debug builds use SnapTray-Debug.exe)
echo.
echo Launching SnapTray-Debug...
"%BUILD_DIR%\bin\SnapTray-Debug.exe"
