@echo off
REM Run all tests for SnapTray

setlocal enabledelayedexpansion
set "VSLANG=1033"

set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%SCRIPT_DIR%.."
set "BUILD_DIR=%PROJECT_DIR%\build"

cd /d "%PROJECT_DIR%"

REM Detect the supported Qt installation (or preserve an explicit QT_PATH).
call "%SCRIPT_DIR%detect-qt.bat"
if errorlevel 1 exit /b 1

echo Using Qt from: %QT_PATH%
echo.

REM Configure if needed, including stale Qt package caches.
cmake "-DSNAPTRAY_BUILD_DIR=%BUILD_DIR%" -DSNAPTRAY_BUILD_TYPE=Debug -P "%SCRIPT_DIR%configure-windows.cmake"
if errorlevel 1 exit /b 1

REM Build all targets (including tests)
echo Building...
cmake --build build
set "BUILD_EXIT_CODE=!ERRORLEVEL!"
if not "!BUILD_EXIT_CODE!"=="0" (
    echo.
    echo Build failed with error code !BUILD_EXIT_CODE!
    exit /b !BUILD_EXIT_CODE!
)

REM Deploy matching Qt dependencies before launching the app or tests.
cmake "-DSNAPTRAY_BUILD_DIR=%BUILD_DIR%" -DSNAPTRAY_BUILD_TYPE=Debug -P "%SCRIPT_DIR%deploy-windows-qt.cmake"
if errorlevel 1 exit /b 1

REM Run tests (add Qt bin to PATH for DLL loading)
echo.
echo Running tests...
set "PATH=%QT_PATH%\bin;%PATH%"
cd /d "%BUILD_DIR%" && ctest --interactive-debug-mode 0 --output-on-failure
set "TEST_EXIT_CODE=!ERRORLEVEL!"
if not "!TEST_EXIT_CODE!"=="0" (
    echo.
    echo Tests failed with error code !TEST_EXIT_CODE!
    exit /b !TEST_EXIT_CODE!
)

exit /b 0
