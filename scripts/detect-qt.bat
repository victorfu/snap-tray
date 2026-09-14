@echo off
REM Preserve explicit toolchain selection; otherwise use the supported Qt release.
if defined QT_PATH exit /b 0

if exist "C:\Qt\6.10.1\msvc2022_64\bin\qmake.exe" (
    set "QT_PATH=C:\Qt\6.10.1\msvc2022_64"
    exit /b 0
)

echo Error: Qt 6.10.1 MSVC 2022 installation not found.
echo Please install the supported Qt release or set QT_PATH explicitly.
echo Example: set QT_PATH=C:\Qt\6.10.1\msvc2022_64
exit /b 1
