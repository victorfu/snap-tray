---
layout: docs
title: FAQ
description: Common questions about privacy, formats, OCR, hotkeys, automation, and platform-specific limitations.
permalink: /docs/faq/
lang: en
route_key: docs_faq
doc_group: advanced
doc_order: 3
---

## Does SnapTray upload my captures?

No automatic upload happens by default. Capture, annotation, pinning, and recording stay local unless you explicitly use the Share URL action. SnapTray also checks for updates via the GitHub Releases API, which can be disabled in Settings.

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
