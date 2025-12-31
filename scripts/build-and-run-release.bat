@echo off
REM Build release version and run SnapTray

setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%SCRIPT_DIR%.."
set "BUILD_DIR=%PROJECT_DIR%\release"

cd /d "%PROJECT_DIR%"

REM Configure if needed
if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo Configuring project...
    cmake -S . -B release -G Ninja -DCMAKE_BUILD_TYPE=Release
)

REM Build all targets
echo Building all targets...
cmake --build release

REM Run (Release builds use SnapTray.exe)
echo.
echo Launching SnapTray...
"%BUILD_DIR%\bin\SnapTray.exe"
