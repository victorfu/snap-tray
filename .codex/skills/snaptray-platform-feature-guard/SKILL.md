---
name: snaptray-platform-feature-guard
description: Review SnapTray changes that cross the platform abstraction boundary, especially capture engines, audio/video, window detection, OCR creation, updater behavior, install-source detection, packaging, and OS-specific permissions or fallbacks. Use when a change touches PlatformFeatures, src/platform, src/capture, src/video, src/update, packaging, WindowDetector, or platform-specific .mm/_win files. Do not use for ordinary widget or QML changes that stay inside the shared UI layer.
---

# SnapTray platform feature guard

Your job is to keep Windows and macOS behavior intentionally symmetric, or intentionally different with an explicit reason.

## Scope

This skill applies to these areas:
- `include/PlatformFeatures.h`
- `src/platform/*`
- `src/capture/*`
- `src/video/*`
- `src/update/*`
- `src/WindowDetector*`
- `packaging/macos/*`
- `packaging/windows/*`
- platform-specific `.mm` and `_win.cpp` files

## Review method

### 1. Identify the platform boundary
State which shared API or capability boundary the patch is crossing, for example:
- `PlatformFeatures`
- capture engine selection
- audio capture engine selection
- video playback backend
- updater service selection
- install source detection
- window detection behavior

### 2. Build a platform matrix
For every behaviorally meaningful change, answer this explicitly:
- Windows behavior after the patch
- macOS behavior after the patch
- whether they should match exactly, partially, or intentionally differ
- what fallback happens when a capability is unavailable

### 3. Look for one-sided edits
Flag it when the patch changes only one side of a paired platform feature without one of these:
- a documented intentional divergence
- a shared abstraction update that keeps the other side correct
- a fallback path that preserves product behavior

### 4. Check permission and packaging fallout
When the patch touches capture, OCR, updater, or installation, check whether it also needs:
- packaging script changes
- entitlements updates on macOS
- app manifest or installer changes on Windows
- release-feed or install-source handling updates

### 5. Check user-visible failure modes
Flag if the patch could lead to:
- silent feature disappearance on one platform
- runtime crash due to backend mismatch
- permission prompt regressions
- updater acting differently depending on install source without clear UI
- different file-path or environment behavior on Windows vs macOS

## SnapTray-specific hot spots

Be especially careful around:
- `src/capture/DXGICaptureEngine_win.cpp`
- `src/capture/SCKCaptureEngine_mac.mm`
- `src/capture/CoreAudioCaptureEngine_mac.mm`
- `src/capture/WASAPIAudioCaptureEngine_win.cpp`
- `src/platform/PlatformFeatures_mac.mm`
- `src/platform/PlatformFeatures_win.cpp`
- `src/WindowDetector.mm`
- `src/WindowDetector_win.cpp`
- `src/update/SparkleUpdateService_mac.mm`
- `src/update/WinSparkleUpdateService_win.cpp`
- `src/update/InstallSourceDetector*.cpp`
- `packaging/macos/package.sh`
- `packaging/windows/package*.bat`

## Test routing hints

Recommend these when relevant:
- `Update_UpdateSettingsManager`
- `Update_UpdateChecker`
- `Update_UpdateCoordinator`
- `Update_InstallSourceDetector`
- `Detection_WindowDetectorQueryMode`
- `Detection_WindowDetectorMacFilters`
- `RecordingManager_DXGICaptureEngineThreadLifecycle`
- `Encoding_NativeGifEncoder`
- `Encoding_EncoderFactory`

## Required output format

Use this structure:

### Platform boundary
What shared abstraction or platform surface is changing.

### Windows vs macOS matrix
A short table or bullet list.

### Missing symmetry or fallback risks
Only real risks.

### Packaging / permission / release follow-up
List exact follow-ups if any.

### Tests
Exact test names.

### Verdict
Use one of:
- safe and symmetric
- intentionally asymmetric but acceptable
- unsafe until missing platform or fallback work is added

## Important behavior

- Do not require perfect line-by-line symmetry when the OSes genuinely differ.
- Do require explicit product-level parity or explicit divergence.
- If the patch is entirely inside shared UI code, say this skill is not the best fit.
