@echo off
REM Build debug version and run SnapTray

setlocal enabledelayedexpansion
set "VSLANG=1033"
set "MSVC_DEPS_PREFIX=Note: including file:"

set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%SCRIPT_DIR%.."
set "BUILD_DIR=%PROJECT_DIR%\build"
set "BIN_DIR=%BUILD_DIR%\bin"
set "EXE_PATH=%BIN_DIR%\SnapTray-Debug.exe"
set "QML_SOURCE_DIR=%PROJECT_DIR%\src\qml"
set "QML_IMPORT_DIR=%BUILD_DIR%"

cd /d "%PROJECT_DIR%"

REM Detect the supported Qt installation (or preserve an explicit QT_PATH).
call "%SCRIPT_DIR%detect-qt.bat"
if errorlevel 1 exit /b 1

echo Using Qt from: %QT_PATH%
echo.

REM Configure if needed
set "NEED_CONFIGURE=0"
if not exist "%BUILD_DIR%\CMakeCache.txt" set "NEED_CONFIGURE=1"
if not exist "%BUILD_DIR%\build.ninja" set "NEED_CONFIGURE=1"
if exist "%BUILD_DIR%\CMakeFiles\rules.ninja" (
    findstr /B /C:"msvc_deps_prefix = %MSVC_DEPS_PREFIX%" "%BUILD_DIR%\CMakeFiles\rules.ninja" >nul
    if errorlevel 1 (
        echo Existing build files use an unsupported MSVC output language. Reconfiguring...
        set "NEED_CONFIGURE=1"
    )
)

if "!NEED_CONFIGURE!"=="1" (
    echo Configuring project...
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="%QT_PATH%"
    set "CONFIGURE_EXIT_CODE=!ERRORLEVEL!"
    if not "!CONFIGURE_EXIT_CODE!"=="0" (
        echo.
        echo Configure failed with error code !CONFIGURE_EXIT_CODE!
        exit /b !CONFIGURE_EXIT_CODE!
    )
)

REM Build all targets (including tests)
echo Building all targets...
cmake --build build
if %ERRORLEVEL% neq 0 (
    echo.
    echo Build failed with error code %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)

REM Check if windeployqt is needed (Qt Quick/QML runtime is required)
if exist "%EXE_PATH%" (
    set "NEED_WINDEPLOYQT=0"
    if not exist "%BIN_DIR%\Qt6Cored.dll" set "NEED_WINDEPLOYQT=1"
    if not exist "%BIN_DIR%\Qt6Guid.dll" set "NEED_WINDEPLOYQT=1"
    if not exist "%BIN_DIR%\Qt6Widgetsd.dll" set "NEED_WINDEPLOYQT=1"
    if not exist "%BIN_DIR%\Qt6Qmld.dll" set "NEED_WINDEPLOYQT=1"
    if not exist "%BIN_DIR%\Qt6Quickd.dll" set "NEED_WINDEPLOYQT=1"
    if not exist "%BIN_DIR%\Qt6QuickWidgetsd.dll" set "NEED_WINDEPLOYQT=1"
    if not exist "%BIN_DIR%\platforms\qwindowsd.dll" set "NEED_WINDEPLOYQT=1"
    if not exist "%BIN_DIR%\qml\QtQuick\Layouts" set "NEED_WINDEPLOYQT=1"
    if not exist "%BIN_DIR%\qml\QtQuick\Controls\Basic" set "NEED_WINDEPLOYQT=1"

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
                "!QT_BIN_DIR!\windeployqt.exe" --qmldir "%QML_SOURCE_DIR%" --qmlimport "%QML_IMPORT_DIR%" "%EXE_PATH%"
            ) else (
                echo Warning: windeployqt.exe not found at !QT_BIN_DIR!
                echo Please run windeployqt manually or set Qt path correctly.
            )
        ) else (
            REM Use the same Qt selected for this build.
            set "WINDEPLOYQT=%QT_PATH%\bin\windeployqt.exe"
            if exist "!WINDEPLOYQT!" (
                "!WINDEPLOYQT!" --qmldir "%QML_SOURCE_DIR%" --qmlimport "%QML_IMPORT_DIR%" "%EXE_PATH%"
            ) else (
                echo Warning: Could not find windeployqt.exe
                echo Please run windeployqt manually before running.
            )
        )
    )
)

REM Run (Debug builds use SnapTray-Debug.exe)
echo.
echo Launching SnapTray-Debug...
if not exist "%EXE_PATH%" (
    echo Error: Executable not found: %EXE_PATH%
    exit /b 1
)

REM Runtime fallback for local development (works even when deployment is partial)
set "PATH=%QT_PATH%\bin;%PATH%"
set "QT_PLUGIN_PATH=%QT_PATH%\plugins"
set "QML2_IMPORT_PATH=%QT_PATH%\qml"

"%EXE_PATH%"
