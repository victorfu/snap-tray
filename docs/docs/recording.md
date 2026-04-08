---
last_modified_at: 2026-03-26
layout: docs
title: Recording
description: Record full screen sources with MP4, GIF, and WebP outputs.
permalink: /docs/recording/
lang: en
route_key: docs_recording
doc_group: workflow
doc_order: 2
---

## Recording entry points

- Tray menu: Record Screen
- CLI: `snaptray record start [--screen N]`

## Recording lifecycle

1. Choose the screen to record when prompted on multi-display setups.
2. Recording starts immediately on the selected screen.
3. Use the floating control bar to monitor duration and stop recording.
4. Click Stop to export.

## Output formats

| Format | Typical use |
|---|---|
| MP4 (H.264) | Tutorials, long recordings |
| GIF | Short looping demos |
| WebP | Lightweight animated snippets |

## Audio options

Audio capture is available for MP4 recordings when supported by the current platform and source:

- Microphone
- System audio
- Mixed capture

GIF and WebP exports are silent.

## Quality tuning

Open Settings > Recording and adjust frame rate, quality, countdown, and preview behavior.
