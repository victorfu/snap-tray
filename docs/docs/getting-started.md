---
layout: docs
title: Getting Started
description: Complete a reliable 10-minute setup so screenshots, pin windows, OCR, and recording workflows work on day one.
permalink: /docs/getting-started/
lang: en
route_key: docs_getting_started
doc_group: basics
doc_order: 2
---

## What you will complete in 10 minutes

- Permission setup
- First screenshot and annotation
- First pin window
- First recording export
- Hotkey conflict check

## Runtime prerequisites

### macOS

- macOS 14+
- Screen Recording permission
- Accessibility permission recommended for window detection and smoother workflows

### Windows

- Windows 10+
- Microphone permission only when recording audio

## Permission checklist

### macOS

Before your first serious capture session, verify:

1. `System Settings > Privacy & Security > Screen Recording`
2. `System Settings > Privacy & Security > Accessibility`

Enable SnapTray in both places, then restart the app if you changed permissions.

### Windows

Most runtime permissions are handled by the OS and installer shape. If you record audio, confirm the selected microphone or audio source is available and permitted.

## Step 1: Verify tray runtime (1 min)

1. Launch SnapTray.
2. Confirm the tray icon is visible.
3. Open the menu and verify these entries exist:
   - Region Capture
   - Screen Canvas
   - Settings

If the tray icon does not appear, continue with [Troubleshooting](/docs/troubleshooting/).

## Step 2: Test screenshot flow (2 min)

1. Press `F2`.
2. Drag any region and confirm the magnifier and size overlay appear.
3. Add one arrow annotation.
4. Press `Ctrl/Cmd + C` and paste into a text editor.
5. Press `Ctrl/Cmd + S` and confirm the file was written.

## Step 3: Test pin workflow (2 min)

1. Capture again with `F2`.
2. Press `Enter` to pin the result.
3. Move, resize, and zoom the pin window.
4. Switch apps and confirm the pin stays visible.

## Step 4: Test recording flow (3 min)

1. Start recording from the tray menu or press `R` from the capture toolbar.
2. Record 5 to 10 seconds.
3. Stop and export MP4.
4. Confirm the output path in Settings > Files.

## Step 5: Resolve hotkey conflicts (2 min)

Open Settings > Hotkeys and rebind any conflicting actions.

Minimum recommended actions:

- Region Capture
- Screen Canvas
- Quick Pin
- Paste

## Recommended next pages

If you want hands-on learning:

- [Tutorial Hub](/docs/tutorials/)
- [Quick Share (30s)](/docs/tutorials/quick-share/)

If you want feature deep dives:

- [Region Capture](/docs/region-capture/)
- [Annotation Tools](/docs/annotation-tools/)
- [Recording](/docs/recording/)
