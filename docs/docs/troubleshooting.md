---
last_modified_at: 2026-03-24
layout: docs
title: Troubleshooting
description: Fix permission, startup, Qt deployment, encoding, recording, and hotkey issues on macOS and Windows.
permalink: /docs/troubleshooting/
lang: en
route_key: docs_troubleshooting
doc_group: advanced
doc_order: 2
---

## Capture does not start

### macOS

- Verify Screen Recording permission in `System Settings > Privacy & Security > Screen Recording`
- Verify Accessibility permission in `System Settings > Privacy & Security > Accessibility`
- Restart SnapTray after changing either permission

### Windows

- Update GPU drivers if capture fails or the region selector does not appear
- Confirm required runtime dependencies are deployed for local development builds
- Check microphone permission only if you are recording audio

## Tray icon does not appear

- Relaunch SnapTray
- Confirm the app was not blocked by OS security prompts
- If you are running a local development build, run the platform build script again and verify dependencies

If the app still does not appear, rebuild with:

- macOS: `./scripts/build.sh`
- Windows: `scripts\build.bat`

## Gatekeeper blocks app launch on macOS

Official signed and notarized releases should launch without Gatekeeper warnings.

To verify a DMG manually:

```bash
spctl -a -vv -t open "dist/SnapTray-<version>-macOS.dmg"
```

For local ad-hoc or development builds only, you can clear quarantine attributes:

```bash
xattr -cr /Applications/SnapTray.app
```

## Windows app shows missing DLL or Qt plugin errors

If you see messages such as `Qt6Core.dll was not found` or `no Qt platform plugin could be initialized`, deploy Qt dependencies with `windeployqt`:

```batch
C:\Qt\6.10.1\msvc2022_64\bin\windeployqt.exe build\bin\SnapTray-Debug.exe
```

Use the same Qt installation path that you passed to `CMAKE_PREFIX_PATH`.

## Hotkey not responding

- Open Settings > Hotkeys and verify the action is still bound
- Rebind the action and test immediately
- Check for conflicts with another global hotkey utility
- Keep the most frequent actions on simple single-combo shortcuts

## Recording issues

- Lower frame rate if you see dropped frames
- Prefer MP4 for longer recordings
- Re-check the selected audio source and device
- On macOS, system audio capture requires macOS 13+ or a virtual audio device such as BlackHole

## Build or local launch errors

For development environments, validate the full toolchain with repository scripts:

- macOS: `./scripts/build.sh` then `./scripts/run-tests.sh`
- Windows: `scripts\build.bat` then `scripts\run-tests.bat`

If you are debugging packaging or signing issues, continue in [Release & Packaging](/developer/release-packaging/).
