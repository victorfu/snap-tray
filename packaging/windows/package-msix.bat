@echo off
setlocal enabledelayedexpansion

REM SnapTray Windows MSIX Packaging Script
REM Creates an MSIX package for Microsoft Store distribution
REM
REM Usage:
REM   package-msix.bat
REM
REM Prerequisites:
REM   - CMake (winget install Kitware.CMake)
REM   - Qt 6.x (https://www.qt.io/download-qt-installer)
REM   - Windows SDK 10.0.17763.0+ (includes MakeAppx.exe)
REM   - Visual Studio Build Tools or Visual Studio
REM
REM Environment variables:
REM   QT_PATH              - Path to Qt installation (e.g., C:\Qt\6.6.1\msvc2019_64)
REM   IDENTITY_NAME        - MSIX Identity Name (default: SnapTray.Dev)
REM   PUBLISHER_ID         - MSIX Publisher ID (default: CN=SnapTray Dev)
REM   MSIX_SIGN_CERT       - Path to .pfx certificate file (optional)
REM   MSIX_SIGN_PASSWORD   - Certificate password (optional)

set SCRIPT_DIR=%~dp0
set PROJECT_ROOT=%SCRIPT_DIR%..\..
set BUILD_DIR=%PROJECT_ROOT%\build
set OUTPUT_DIR=%PROJECT_ROOT%\dist
set STAGING_DIR=%BUILD_DIR%\msix-staging
set ASSETS_DIR=%STAGING_DIR%\Assets

REM Extract version from CMakeLists.txt
for /f "tokens=*" %%a in ('findstr /C:"project(SnapTray VERSION" "%PROJECT_ROOT%\CMakeLists.txt"') do (
    for /f "tokens=3" %%b in ("%%a") do set VERSION=%%b
)
set APP_NAME=SnapTray

REM Default MSIX identity values for local development
if "%IDENTITY_NAME%"=="" set IDENTITY_NAME=SnapTray.Dev
if "%PUBLISHER_ID%"=="" set PUBLISHER_ID=CN=SnapTray Dev

echo =======================================
echo   SnapTray MSIX Packager
echo =======================================
echo Version: %VERSION%
echo Identity: %IDENTITY_NAME%
echo Publisher: %PUBLISHER_ID%
echo Build directory: %BUILD_DIR%
echo Output directory: %OUTPUT_DIR%
echo.

REM Check for Qt
if "%QT_PATH%"=="" (
    if exist "C:\Qt\6.10.1\msvc2022_64" (
        set QT_PATH=C:\Qt\6.10.1\msvc2022_64
    ) else if exist "C:\Qt\6.8.0\msvc2022_64" (
        set QT_PATH=C:\Qt\6.8.0\msvc2022_64
    ) else if exist "C:\Qt\6.7.0\msvc2022_64" (
        set QT_PATH=C:\Qt\6.7.0\msvc2022_64
    ) else if exist "C:\Qt\6.6.1\msvc2019_64" (
        set QT_PATH=C:\Qt\6.6.1\msvc2019_64
    ) else (
        echo ERROR: Qt not found. Please set QT_PATH environment variable.
        exit /b 1
    )
)
echo Qt path: %QT_PATH%

REM Check for Windows SDK (MakeAppx.exe)
set SDK_BIN=
for %%v in (10.0.26100.0 10.0.22621.0 10.0.22000.0 10.0.19041.0 10.0.18362.0 10.0.17763.0) do (
    if exist "C:\Program Files (x86)\Windows Kits\10\bin\%%v\x64\makeappx.exe" (
        set "SDK_BIN=C:\Program Files (x86)\Windows Kits\10\bin\%%v\x64"
        goto :sdk_found
    )
)
echo ERROR: Windows SDK not found. Please install Windows 10 SDK.
exit /b 1
:sdk_found
echo Windows SDK: %SDK_BIN%

REM Step 1: Build Release
echo.
echo [1/7] Building release...

set SCCACHE_ARGS=
where sccache >nul 2>&1
if not errorlevel 1 (
    echo Using sccache for faster builds
    set SCCACHE_ARGS=-DCMAKE_CXX_COMPILER_LAUNCHER=sccache -DCMAKE_C_COMPILER_LAUNCHER=sccache
)

cmake -S "%PROJECT_ROOT%" -B "%BUILD_DIR%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_PREFIX_PATH="%QT_PATH%" ^
    %SCCACHE_ARGS%
if errorlevel 1 (
    echo ERROR: CMake configure failed
    exit /b 1
)

cmake --build "%BUILD_DIR%" --config Release --parallel
if errorlevel 1 (
    echo ERROR: Build failed
    exit /b 1
)

REM Find the executable (Ninja puts it in bin\, VS puts it in bin\Release\)
set EXE_PATH=
if exist "%BUILD_DIR%\bin\Release\%APP_NAME%.exe" (
    set EXE_PATH=%BUILD_DIR%\bin\Release\%APP_NAME%.exe
) else if exist "%BUILD_DIR%\bin\%APP_NAME%.exe" (
    set EXE_PATH=%BUILD_DIR%\bin\%APP_NAME%.exe
) else (
    echo ERROR: Build failed - %APP_NAME%.exe not found
    exit /b 1
)

REM Step 2: Create MSIX staging directory
echo.
echo [2/7] Preparing MSIX staging directory...
if exist "%STAGING_DIR%" rmdir /s /q "%STAGING_DIR%"
mkdir "%STAGING_DIR%"
mkdir "%ASSETS_DIR%"
copy "%EXE_PATH%" "%STAGING_DIR%\" >nul

REM Step 3: Run windeployqt
echo.
echo [3/7] Running windeployqt...
"%QT_PATH%\bin\windeployqt.exe" --release --no-translations "%STAGING_DIR%\%APP_NAME%.exe"
if errorlevel 1 (
    echo ERROR: windeployqt failed
    exit /b 1
)

REM Step 4: Generate Store assets
echo.
echo [4/7] Generating Store assets...
if exist "%SCRIPT_DIR%assets\Square44x44Logo.png" (
    echo Using pre-generated assets from packaging\windows\assets
    xcopy /s /y /q "%SCRIPT_DIR%assets\*.*" "%ASSETS_DIR%\" >nul
) else (
    echo Generating assets from snaptray.png...
    powershell -ExecutionPolicy Bypass -File "%SCRIPT_DIR%generate-assets.ps1" -OutputDir "%ASSETS_DIR%"
    if errorlevel 1 (
        echo ERROR: Asset generation failed
        exit /b 1
    )
)

REM Step 5: Generate AppxManifest.xml
echo.
echo [5/7] Generating AppxManifest.xml...
powershell -Command "$content = Get-Content '%SCRIPT_DIR%AppxManifest.xml.in' -Raw; $content = $content -replace '@IDENTITY_NAME@', '%IDENTITY_NAME%'; $content = $content -replace '@PUBLISHER_ID@', '%PUBLISHER_ID%'; $content = $content -replace '@VERSION@', '%VERSION%'; Set-Content -Path '%STAGING_DIR%\AppxManifest.xml' -Value $content -Encoding UTF8"
if errorlevel 1 (
    echo ERROR: Failed to generate AppxManifest.xml
    exit /b 1
)

REM Step 6: Create MSIX package
echo.
echo [6/7] Creating MSIX package...
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

set MSIX_OUTPUT=%OUTPUT_DIR%\%APP_NAME%-%VERSION%.msix
"%SDK_BIN%\makeappx.exe" pack /d "%STAGING_DIR%" /p "%MSIX_OUTPUT%" /o
if errorlevel 1 (
    echo ERROR: MakeAppx failed
    exit /b 1
)

REM Step 7: Sign MSIX (optional)
echo.
if defined MSIX_SIGN_CERT (
    echo [7/7] Signing MSIX package...
    "%SDK_BIN%\signtool.exe" sign /fd SHA256 /f "%MSIX_SIGN_CERT%" ^
        /p "%MSIX_SIGN_PASSWORD%" ^
        /t http://timestamp.digicert.com ^
        "%MSIX_OUTPUT%"
    if errorlevel 1 (
        echo WARNING: Signing failed, continuing with unsigned package
    ) else (
        echo MSIX package signed successfully
    )
) else (
    echo [7/7] Skipping signing (MSIX_SIGN_CERT not set)
)

REM Create .msixupload for Store submission
echo.
echo Creating .msixupload for Store submission...
copy "%MSIX_OUTPUT%" "%OUTPUT_DIR%\%APP_NAME%-%VERSION%.msixupload" >nul

echo.
echo =======================================
echo   Build Complete
echo =======================================
echo Package: %MSIX_OUTPUT%
echo Upload:  %OUTPUT_DIR%\%APP_NAME%-%VERSION%.msixupload
echo.
echo For local testing (unsigned):
echo   Add-AppPackage -Path "%MSIX_OUTPUT%" -AllowUnsigned
echo.
echo For Microsoft Store submission:
echo   1. Sign in to Partner Center (https://partner.microsoft.com)
echo   2. Create or update your app submission
echo   3. Upload the .msixupload file

endlocal
