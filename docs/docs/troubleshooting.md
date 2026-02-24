---
layout: docs
title: Troubleshooting
description: Fix permission, startup, encoding, and hotkey issues on macOS and Windows.
permalink: /docs/troubleshooting/
lang: en
route_key: docs_troubleshooting
doc_group: advanced
doc_order: 2
---

## Capture does not start

### macOS

- Verify Screen Recording permission is enabled
- Verify Accessibility permission is enabled
- Restart SnapTray after permission changes

### Windows

- Ensure GPU drivers are up to date
- Confirm required runtime libraries are present

## Hotkey not responding

- Open Settings > Hotkeys and verify no duplicate binding
- Reassign to another key and test
- Check if another app globally intercepts the same combo

## Recording issues

- Lower frame rate if dropped frames appear
- Switch output format to MP4 for long recordings
- Re-check selected audio source and device

## Build or launch errors

For development builds, run project scripts:

- macOS: `./scripts/build.sh`
- Windows: `scripts\\build.bat`

Then run test scripts to verify environment consistency.
