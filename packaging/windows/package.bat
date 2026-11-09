@echo off
setlocal

REM SnapTray Windows Packaging Script
REM Wrapper script that calls package-nsis.bat and/or package-msix.bat
REM
REM Usage:
REM   package.bat              - Build both NSIS and MSIX
REM   package.bat nsis         - Build NSIS installer only
REM   package.bat msix         - Build MSIX package only

set "SCRIPT_DIR=%~dp0"

if /i "%~1"=="nsis" goto package_nsis
if /i "%~1"=="msix" goto package_msix

REM Build both
echo Building NSIS installer...
call "%SCRIPT_DIR%package-nsis.bat"
if errorlevel 1 (
    echo NSIS build failed
    exit /b 1
)

echo.
echo =======================================
echo.

echo Building MSIX package...
call "%SCRIPT_DIR%package-msix.bat"
if errorlevel 1 (
    echo MSIX build failed
    exit /b 1
)

endlocal
exit /b 0

:package_nsis
call "%SCRIPT_DIR%package-nsis.bat"
exit /b %ERRORLEVEL%

:package_msix
call "%SCRIPT_DIR%package-msix.bat"
exit /b %ERRORLEVEL%
