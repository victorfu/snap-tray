@echo off
REM Build release version and run SnapTray

setlocal enabledelayedexpansion
set "VSLANG=1033"

set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%SCRIPT_DIR%.."
set "BUILD_DIR=%PROJECT_DIR%\release"
set "BIN_DIR=%BUILD_DIR%\bin"
set "EXE_PATH=%BIN_DIR%\SnapTray.exe"

cd /d "%PROJECT_DIR%"

REM Detect the supported Qt installation (or preserve an explicit QT_PATH).
call "%SCRIPT_DIR%detect-qt.bat"
if errorlevel 1 exit /b 1

echo Using Qt from: %QT_PATH%
echo.

REM Configure if needed, including stale Qt package caches.
cmake "-DSNAPTRAY_BUILD_DIR=%BUILD_DIR%" -DSNAPTRAY_BUILD_TYPE=Release -P "%SCRIPT_DIR%configure-windows.cmake"
if errorlevel 1 exit /b 1

REM Build all targets
echo Building all targets...
cmake --build release
if %ERRORLEVEL% neq 0 (
    echo.
    echo Build failed with error code %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)

REM Deploy matching Qt dependencies before launching the app or tests.
cmake "-DSNAPTRAY_BUILD_DIR=%BUILD_DIR%" -DSNAPTRAY_BUILD_TYPE=Release -P "%SCRIPT_DIR%deploy-windows-qt.cmake"
if errorlevel 1 exit /b 1

REM Run (Release builds use SnapTray.exe)
echo.
echo Launching SnapTray...
if not exist "%EXE_PATH%" (
    echo Error: Executable not found: %EXE_PATH%
    exit /b 1
)

REM Runtime fallback for local development (works even when deployment is partial)
set "PATH=%QT_PATH%\bin;%PATH%"
set "QT_PLUGIN_PATH=%QT_PATH%\plugins"
set "QML2_IMPORT_PATH=%QT_PATH%\qml"

"%EXE_PATH%"
