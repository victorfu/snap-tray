@echo off
REM Run all tests for SnapTray

setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%SCRIPT_DIR%.."
set "BUILD_DIR=%PROJECT_DIR%\build"

cd /d "%PROJECT_DIR%"

REM Detect Qt installation path
if not defined QT_PATH (
    REM Try common Qt installation paths
    if exist "C:\Qt\6.10.1\msvc2022_64" set "QT_PATH=C:\Qt\6.10.1\msvc2022_64"
    if exist "C:\Qt\6.8.0\msvc2022_64" set "QT_PATH=C:\Qt\6.8.0\msvc2022_64"
    if exist "C:\Qt\6.7.0\msvc2022_64" set "QT_PATH=C:\Qt\6.7.0\msvc2022_64"
)

if not defined QT_PATH (
    echo Error: Qt installation not found.
    echo Please set QT_PATH environment variable or install Qt to a standard location.
    echo Example: set QT_PATH=C:\Qt\6.10.1\msvc2022_64
    exit /b 1
)

echo Using Qt from: %QT_PATH%
echo.

REM Configure if needed (check for both CMakeCache.txt and build.ninja)
if not exist "%BUILD_DIR%\build.ninja" (
    echo Configuring project...
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="%QT_PATH%"
)

REM Build all targets (including tests)
echo Building...
cmake --build build

REM Run tests
echo.
echo Running tests...
cd /d "%BUILD_DIR%" && ctest --output-on-failure
