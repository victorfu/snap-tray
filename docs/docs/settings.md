---
last_modified_at: 2026-11-06
layout: docs
title: Settings
seo_title: "SnapTray Settings: Capture, Pin Windows, Saving, Recording and Text Recognition"
description: Tune capture, pin windows, saving and watermarks, platform-specific recording and text recognition, hotkeys, and updates.
permalink: /docs/settings/
lang: en
route_key: docs_settings
doc_group: workflow
doc_order: 4
---

## Settings pages

Pages are grouped by task. Every page except About starts with a title and a
sentence saying what it covers. Updates and About sit below a divider.

Recording and Text Recognition are available on macOS and Windows only. Linux
beta hides both pages because those features are not included.

### General

Launch at startup, display language, app theme, and CLI install/uninstall.

On macOS, this page also shows Screen Recording and Accessibility permission status with one-click system settings shortcuts.

### Capture

- **After Capture**: what `After full screen capture` does (copy, save, or open the editor).
- **Region Selector**: the cursor companion (magnifier) and shortcut hints.
- **Auto Blur**: blur type and intensity used when Auto Blur hides faces and text.

### Pin Windows

Default opacity, **Show controls on hover**, and how much each opacity or zoom step changes.

### Recording

Frame rate, default output format, preview-first export behavior, audio source/device, and countdown.

### Saving

- Screenshot and recording folders, and the filename format template.
- Auto-save and **Remember last folder**: with it on, Region, Pin, Beautify, and Library Save As dialogs reopen in the folder of the last successful manual image save. Auto-save continues to use the Screenshots folder.
- **Watermark**: the image, whether it applies to screenshots and recordings, opacity, scale, margin, and position, with a preview.
- **Library**: how many screenshots the Library keeps. When there are more, the oldest are removed from the Library; files you saved stay where they are.

### Text Recognition

OCR language order and post-recognition behavior.

### Hotkeys

Centralized hotkey editor by functional category. Each shortcut can be reset, and **Restore All Defaults** resets them all.

### Updates

Current version, auto-check interval, last-checked status, and manual check now.

### About

App name, version, copyright, author, and website link.

## Find and reset settings

- **Search settings** at the top of the sidebar (`Ctrl+F`, or `Cmd+F` on macOS) finds settings by name, by section (for example "watermark"), by page, and finds hotkeys by action name. `Up`/`Down` and `Enter` open a result: SnapTray switches to its page, scrolls to the setting and highlights it briefly. `Esc` clears the search.
- Settings save as soon as you change them. When a setting differs from its default, a **reset** button appears to the left of its name; it restores that one setting. Hotkeys keep their own **Reset** and **Restore All Defaults**. Settings that depend on your system or a choice only you can make, such as language, theme, start on login, the input device, the watermark image or the OCR languages, have no reset button.

## Recommended baseline profile

- On macOS/Windows, 30 FPS MP4 for standard tutorials
- Dedicated folders for screenshots and, where available, recordings
- Auto update checks enabled daily

## Next step

Use [Troubleshooting](/docs/troubleshooting/) if any permission or output issue appears.


### Linux AppImage updates

Signed Linux AppImage builds can check for updates from the tray menu or the
Updates settings page. Review the version and release notes, choose **Download
Update**, then **Restart and Update** when ready. **Later** keeps the verified
download for a future session. Automatic checks follow your configured interval;
download and installation require confirmation.

Keep the AppImage in a writable directory with space for the download and a
backup. If an update fails, the existing version is preserved or restored; use
**Discard Download** to clear a failed pending download and check again. You can
also download a release manually. Older unsigned AppImages need one manual
upgrade to a signed build before in-app updates are available.
