---
layout: docs
title: FAQ
description: Common questions about privacy, formats, OCR, hotkeys, automation, and platform-specific limitations.
permalink: /docs/faq/
lang: en
route_key: docs_faq
doc_group: advanced
doc_order: 3
faq_schema:
  - question: "Does SnapTray upload my captures?"
    answer: "No automatic upload happens by default. Capture, annotation, pinning, and recording stay local unless you explicitly use the Share URL action."
  - question: "Which output format should I use?"
    answer: "MP4 for long videos and tutorials, GIF for short loops or small demos, WebP for lighter animated snippets."
  - question: "Why is OCR unavailable on my system?"
    answer: "OCR availability depends on platform support and installed language packs."
  - question: "Can I customize default hotkeys?"
    answer: "Yes. Open Settings > Hotkeys and edit by category."
  - question: "Can I use SnapTray from scripts?"
    answer: "Yes. The CLI is the official automation interface for local capture and IPC-based control."
  - question: "What are the current platform limitations?"
    answer: "Multi-monitor and mixed-DPI capture still benefits from more real-device testing. Screen recording may fall back to the Qt capture engine when native APIs are unavailable. System audio capture on macOS requires macOS 13+ or a virtual audio device such as BlackHole."
  - question: "Where should I report bugs?"
    answer: "Open an issue on GitHub and include your OS version, SnapTray version, reproducible steps, and screenshots or recordings when relevant."
---

## Does SnapTray upload my captures?

No automatic upload happens by default. Capture, annotation, pinning, and recording stay local unless you explicitly use the Share URL action. On direct-download builds, SnapTray checks for updates via the platform-native updater (Sparkle on macOS, WinSparkle on Windows), which can be disabled in Settings.

## Which output format should I use?

- MP4 for long videos and tutorials
- GIF for short loops or small demos
- WebP for lighter animated snippets

## Why is OCR unavailable on my system?

OCR availability depends on platform support and installed language packs.

## Can I customize default hotkeys?

Yes. Open Settings > Hotkeys and edit by category.

## Can I use SnapTray from scripts?

Yes. The CLI is the official automation interface for local capture and IPC-based control. See [CLI](/docs/cli/) for the complete command reference.

## What are the current platform limitations?

- Multi-monitor and mixed-DPI capture still benefits from more real-device testing
- Screen recording may fall back to the Qt capture engine when native APIs are unavailable, which can be slower
- System audio capture on macOS requires macOS 13+ or a virtual audio device such as BlackHole

## Where should I report bugs?

Open an issue on GitHub and include:

- OS version
- SnapTray version
- Reproducible steps
- Screenshots or recordings when relevant
