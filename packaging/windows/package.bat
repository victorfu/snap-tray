@echo off
setlocal enabledelayedexpansion

REM SnapTray Windows Packaging Script
REM Creates an NSIS installer with the application
REM
REM Usage:
REM   package.bat                     - Build without signing
REM   set CODESIGN_CERT=path\to\cert.pfx && package.bat  - Build with signing
REM
REM Prerequisites:
REM   - CMake (winget install Kitware.CMake)
REM   - Qt 6.x (https://www.qt.io/download-qt-installer)
REM   - NSIS (winget install NSIS.NSIS)
REM   - Visual Studio Build Tools or Visual Studio
REM
REM Environment variables:
REM   QT_PATH              - Path to Qt installation (e.g., C:\Qt\6.6.1\msvc2019_64)
REM   CODESIGN_CERT        - Path to .pfx certificate file
REM   CODESIGN_PASSWORD    - Certificate password

set SCRIPT_DIR=%~dp0
set PROJECT_ROOT=%SCRIPT_DIR%..\..
set BUILD_DIR=%PROJECT_ROOT%\release
set OUTPUT_DIR=%PROJECT_ROOT%\dist
set STAGING_DIR=%BUILD_DIR%\staging

REM Extract version from CMakeLists.txt
for /f "tokens=3 delims=() " %%a in ('findstr /C:"project(SnapTray VERSION" "%PROJECT_ROOT%\CMakeLists.txt"') do (
    for /f "tokens=1 delims= " %%b in ("%%a") do set VERSION=%%b
)
set APP_NAME=SnapTray

echo =======================================
echo   SnapTray Windows Packager
echo =======================================
echo Version: %VERSION%
echo Build directory: %BUILD_DIR%
echo Output directory: %OUTPUT_DIR%
echo.

REM Check for Qt
if "%QT_PATH%"=="" (
    REM Try common Qt installation paths
    if exist "C:\Qt\6.8.0\msvc2022_64" (
        set QT_PATH=C:\Qt\6.8.0\msvc2022_64
    ) else if exist "C:\Qt\6.7.0\msvc2022_64" (
        set QT_PATH=C:\Qt\6.7.0\msvc2022_64
    ) else if exist "C:\Qt\6.6.1\msvc2019_64" (
        set QT_PATH=C:\Qt\6.6.1\msvc2019_64
    ) else (
        echo ERROR: Qt not found. Please set QT_PATH environment variable.
        echo Example: set QT_PATH=C:\Qt\6.6.1\msvc2019_64
        exit /b 1
    )
)
echo Qt path: %QT_PATH%

REM Check for NSIS
where makensis >nul 2>&1
if errorlevel 1 (
    echo ERROR: NSIS not found. Please install with: winget install NSIS.NSIS
    exit /b 1
)

REM Step 1: Build Release
echo.
echo [1/5] Building release...
cmake -S "%PROJECT_ROOT%" -B "%BUILD_DIR%" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_PREFIX_PATH="%QT_PATH%"
if errorlevel 1 (
    echo ERROR: CMake configure failed
    exit /b 1
)

cmake --build "%BUILD_DIR%" --config Release --parallel
if errorlevel 1 (
    echo ERROR: Build failed
    exit /b 1
)

REM Check if exe was built
if not exist "%BUILD_DIR%\Release\%APP_NAME%.exe" (
    echo ERROR: Build failed - %APP_NAME%.exe not found
    exit /b 1
)

REM Step 2: Create staging directory
echo.
echo [2/5] Preparing staging directory...
if exist "%STAGING_DIR%" rmdir /s /q "%STAGING_DIR%"
mkdir "%STAGING_DIR%"

REM Copy executable
copy "%BUILD_DIR%\Release\%APP_NAME%.exe" "%STAGING_DIR%\" >nul

REM Step 3: Run windeployqt
echo.
echo [3/5] Running windeployqt...
"%QT_PATH%\bin\windeployqt.exe" --release --no-translations "%STAGING_DIR%\%APP_NAME%.exe"
if errorlevel 1 (
    echo ERROR: windeployqt failed
    exit /b 1
)

REM Step 4: Code signing (if certificate provided)
echo.
if defined CODESIGN_CERT (
    echo [4/5] Signing executable...
    signtool sign /f "%CODESIGN_CERT%" /p "%CODESIGN_PASSWORD%" ^
        /t http://timestamp.digicert.com ^
        /d "%APP_NAME%" ^
        "%STAGING_DIR%\%APP_NAME%.exe"
    if errorlevel 1 (
        echo WARNING: Code signing failed, continuing without signature
    ) else (
        echo Executable signed successfully
    )
) else (
    echo [4/5] Skipping code signing (CODESIGN_CERT not set)
)

REM Step 5: Create NSIS installer
echo.
echo [5/5] Creating installer...
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

REM Generate version-specific NSIS header
set NSIS_DIR=%SCRIPT_DIR%
echo !define VERSION "%VERSION%" > "%NSIS_DIR%version.nsh"
echo !define APP_NAME "%APP_NAME%" >> "%NSIS_DIR%version.nsh"
echo !define STAGING_DIR "%STAGING_DIR%" >> "%NSIS_DIR%version.nsh"
echo !define OUTPUT_DIR "%OUTPUT_DIR%" >> "%NSIS_DIR%version.nsh"
echo !define PROJECT_ROOT "%PROJECT_ROOT%" >> "%NSIS_DIR%version.nsh"

REM Verify generation
if not exist "%NSIS_DIR%version.nsh" (
    echo ERROR: Failed to generate version.nsh
    exit /b 1
)

makensis /V3 "%SCRIPT_DIR%\installer.nsi"
if errorlevel 1 (
    echo ERROR: NSIS installer creation failed
    exit /b 1
)

REM Sign installer if certificate provided
if defined CODESIGN_CERT (
    echo Signing installer...
    signtool sign /f "%CODESIGN_CERT%" /p "%CODESIGN_PASSWORD%" ^
        /t http://timestamp.digicert.com ^
        /d "%APP_NAME% Setup" ^
        "%OUTPUT_DIR%\%APP_NAME%-%VERSION%-Setup.exe"
)

REM Done
echo.
echo =======================================
echo   Build Complete
echo =======================================
echo Package: %OUTPUT_DIR%\%APP_NAME%-%VERSION%-Setup.exe
echo.
echo To install:
echo   1. Run the Setup.exe
echo   2. Follow the installation wizard
echo   3. Launch SnapTray from Start Menu or Desktop

endlocal
