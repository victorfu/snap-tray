@echo off
REM Intentionally no setlocal: the caller needs the compiler environment.
REM Preserve an existing, complete x64 developer environment.
if /i "%VSCMD_ARG_TGT_ARCH%"=="x64" if defined INCLUDE if defined LIB (
    where cl.exe >nul 2>&1
    if not errorlevel 1 exit /b 0
)

set "SNAPTRAY_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%SNAPTRAY_VSWHERE%" (
    echo Error: Visual Studio Installer not found. Install Visual Studio Build Tools with Desktop development with C++.
    exit /b 1
)

set "SNAPTRAY_VS_PATH="
for /f "usebackq delims=" %%I in (`"%SNAPTRAY_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "SNAPTRAY_VS_PATH=%%I"
if not exist "%SNAPTRAY_VS_PATH%\Common7\Tools\VsDevCmd.bat" (
    echo Error: MSVC x64 build tools not found. Install Desktop development with C++ and the Windows SDK.
    exit /b 1
)

call "%SNAPTRAY_VS_PATH%\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
if not defined INCLUDE (
    echo Error: MSVC setup did not initialize the INCLUDE environment variable.
    exit /b 1
)
if not defined LIB (
    echo Error: MSVC setup did not initialize the LIB environment variable.
    exit /b 1
)
where cl.exe >nul 2>&1
if errorlevel 1 (
    echo Error: MSVC setup did not make cl.exe available.
    exit /b 1
)
set "SNAPTRAY_VSWHERE="
set "SNAPTRAY_VS_PATH="
exit /b 0
