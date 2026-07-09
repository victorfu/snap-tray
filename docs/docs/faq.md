---
last_modified_at: 2026-03-26
layout: docs
title: FAQ
seo_title: "SnapTray FAQ: Privacy, Formats, OCR and Automation Help"
description: Common questions about privacy, formats, OCR, hotkeys, automation, and platform-specific limitations.
permalink: /docs/faq/
lang: en
route_key: docs_faq
doc_group: advanced
doc_order: 3
faq_schema:
  - question: "Does SnapTray upload my captures?"
    answer: "No automatic upload happens. Capture, annotation, and pinning stay local. Recording is macOS/Windows only and also stays local."
  - question: "Which output format should I use?"
    answer: "On macOS/Windows, use MP4 for long videos and tutorials, GIF for short loops or small demos, and WebP for lighter animated snippets. Recording formats are not available in the Linux beta."
  - question: "Why is OCR unavailable on my system?"
    answer: "OCR is available on macOS/Windows when platform support and installed language packs are present. It is hidden and not included in the Linux beta."
  - question: "Can I customize default hotkeys?"
    answer: "Yes. Open Settings > Hotkeys and edit by category."
  - question: "Can I use SnapTray from scripts?"
    answer: "Yes. The CLI is the official automation interface for local capture and IPC-based control."
  - question: "What are the current platform limitations?"
    answer: "Multi-monitor and mixed-DPI capture still benefits from more real-device testing. On macOS/Windows, screen recording may fall back to the Qt capture engine when native APIs are unavailable. System audio capture on macOS requires macOS 13+ or a virtual audio device such as BlackHole. Linux beta is an Ubuntu 22.04 X11 AppImage; recording and OCR are not included, and Wayland is not supported."
  - question: "Where should I report bugs?"
    answer: "Open an issue on GitHub and include your OS version, SnapTray version, reproducible steps, and screenshots or recordings when relevant."
---

## Does SnapTray upload my captures?

No automatic upload happens. Capture, annotation, and pinning stay local. Recording is macOS/Windows only and also stays local. On direct-download builds, SnapTray checks for updates via the platform-native updater (Sparkle on macOS, WinSparkle on Windows), which can be disabled in Settings.

## Which output format should I use?

Recording and animated export are macOS/Windows only. On those platforms:

- MP4 for long videos and tutorials
- GIF for short loops or small demos
- WebP for lighter animated snippets

## Why is OCR unavailable on my system?

OCR is available on macOS/Windows when platform support and installed language packs are present. It is hidden and not included in the Linux beta.

## Can I customize default hotkeys?

Yes. Open Settings > Hotkeys and edit by category.

## Can I use SnapTray from scripts?

Yes. The CLI is the official automation interface for local capture and IPC-based control. See [CLI](/docs/cli/) for the complete command reference.

## What are the current platform limitations?

- Multi-monitor and mixed-DPI capture still benefits from more real-device testing
- On macOS/Windows, screen recording may fall back to the Qt capture engine when native APIs are unavailable, which can be slower
- System audio capture on macOS requires macOS 13+ or a virtual audio device such as BlackHole
- Linux beta is an Ubuntu 22.04 X11 AppImage; recording and OCR are not included, and Wayland sessions are not supported

## Where should I report bugs?

Open an issue on GitHub and include:

- OS version
- SnapTray version
- Reproducible steps
- Screenshots or recordings when relevant
