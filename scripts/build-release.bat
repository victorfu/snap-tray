@echo off
REM Build release version of SnapTray (without running)

setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%SCRIPT_DIR%.."
set "BUILD_DIR=%PROJECT_DIR%\release"
set "BIN_DIR=%BUILD_DIR%\bin"
set "EXE_PATH=%BIN_DIR%\SnapTray.exe"

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

REM Configure if needed
if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo Configuring project...
    cmake -S . -B release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT_PATH%"
)

REM Build all targets
echo Building all targets...
cmake --build release
if %ERRORLEVEL% neq 0 (
    echo.
    echo Build failed with error code %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)

REM Check if windeployqt is needed (Qt Quick/QML runtime is required)
if exist "%EXE_PATH%" (
    set "NEED_WINDEPLOYQT=0"
    if not exist "%BIN_DIR%\Qt6Core.dll" set "NEED_WINDEPLOYQT=1"
    if not exist "%BIN_DIR%\Qt6Gui.dll" set "NEED_WINDEPLOYQT=1"
    if not exist "%BIN_DIR%\Qt6Widgets.dll" set "NEED_WINDEPLOYQT=1"
    if not exist "%BIN_DIR%\Qt6Qml.dll" set "NEED_WINDEPLOYQT=1"
    if not exist "%BIN_DIR%\Qt6Quick.dll" set "NEED_WINDEPLOYQT=1"
    if not exist "%BIN_DIR%\Qt6QuickWidgets.dll" set "NEED_WINDEPLOYQT=1"
    if not exist "%BIN_DIR%\platforms\qwindows.dll" set "NEED_WINDEPLOYQT=1"

    if "!NEED_WINDEPLOYQT!"=="1" (
        echo.
        echo Qt dependencies not found. Running windeployqt...

        REM Try to find Qt installation from CMakeCache
        for /f "tokens=2 delims==" %%a in ('findstr /C:"Qt6_DIR:PATH=" "%BUILD_DIR%\CMakeCache.txt" 2^>nul') do (
            set "QT_DIR=%%a"
        )

        if defined QT_DIR (
            REM Navigate from Qt6_DIR (lib/cmake/Qt6) to bin
            set "QT_BIN_DIR=!QT_DIR!\..\..\..\bin"
            if exist "!QT_BIN_DIR!\windeployqt.exe" (
                "!QT_BIN_DIR!\windeployqt.exe" "%EXE_PATH%"
            ) else (
                echo Warning: windeployqt.exe not found at !QT_BIN_DIR!
                echo Please run windeployqt manually or set Qt path correctly.
            )
        ) else (
            REM Fallback: try common Qt paths
            set "WINDEPLOYQT="
            if exist "C:\Qt\6.10.1\msvc2022_64\bin\windeployqt.exe" set "WINDEPLOYQT=C:\Qt\6.10.1\msvc2022_64\bin\windeployqt.exe"
            if exist "C:\Qt\6.8.0\msvc2022_64\bin\windeployqt.exe" set "WINDEPLOYQT=C:\Qt\6.8.0\msvc2022_64\bin\windeployqt.exe"
            if exist "C:\Qt\6.7.0\msvc2022_64\bin\windeployqt.exe" set "WINDEPLOYQT=C:\Qt\6.7.0\msvc2022_64\bin\windeployqt.exe"

            if defined WINDEPLOYQT (
                "!WINDEPLOYQT!" "%EXE_PATH%"
            ) else (
                echo Warning: Could not find windeployqt.exe
                echo Please run windeployqt manually before running.
            )
        )
    )
)

echo.
echo Build complete: %EXE_PATH%
