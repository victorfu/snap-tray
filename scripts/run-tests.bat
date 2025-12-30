@echo off
REM Run all tests for SnapTray

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

REM Build test targets
echo Building tests...
cmake --build build --parallel --target ColorAndWidthWidget_State ColorAndWidthWidget_Signals ColorAndWidthWidget_HitTest ColorAndWidthWidget_Events

REM Run tests
echo.
echo Running tests...
cd /d "%BUILD_DIR%" && ctest --output-on-failure
