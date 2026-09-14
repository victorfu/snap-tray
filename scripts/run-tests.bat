@echo off
REM Run all tests for SnapTray

setlocal enabledelayedexpansion
set "VSLANG=1033"
set "MSVC_DEPS_PREFIX=Note: including file:"

set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%SCRIPT_DIR%.."
set "BUILD_DIR=%PROJECT_DIR%\build"

cd /d "%PROJECT_DIR%"

REM Detect the supported Qt installation (or preserve an explicit QT_PATH).
call "%SCRIPT_DIR%detect-qt.bat"
if errorlevel 1 exit /b 1

echo Using Qt from: %QT_PATH%
echo.

REM Configure if needed (check for both CMakeCache.txt and build.ninja)
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
echo Building...
cmake --build build
set "BUILD_EXIT_CODE=!ERRORLEVEL!"
if not "!BUILD_EXIT_CODE!"=="0" (
    echo.
    echo Build failed with error code !BUILD_EXIT_CODE!
    exit /b !BUILD_EXIT_CODE!
)

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
