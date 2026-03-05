@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "PROJECT_DIR=%SCRIPT_DIR%.."
set "TARGET=%PROJECT_DIR%\src\qml\recording\RecordingPreview.qml"

if not exist "%TARGET%" (
    echo ERROR: Target file not found: %TARGET%
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -Command "\
$target = '%TARGET%'; \
$matches = Select-String -Path $target -Pattern '#[0-9A-Fa-f]{3,8}|Qt\\.rgba\\(' -AllMatches; \
if ($matches) { \
  Write-Host 'ERROR: Found hardcoded color literals in RecordingPreview.qml'; \
  $matches | ForEach-Object { '{0}:{1}: {2}' -f $_.Path, $_.LineNumber, $_.Line.Trim() | Write-Host }; \
  exit 1 \
} else { \
  Write-Host 'OK: RecordingPreview.qml uses token-based colors only'; \
  exit 0 \
}"
if errorlevel 1 exit /b 1
