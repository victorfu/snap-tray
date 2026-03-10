---
layout: page
title: Privacy Policy
permalink: /privacy/
description: SnapTray privacy policy. Local-first with no telemetry or tracking.
lang: en
route_key: privacy
---

<p><strong>Last Updated:</strong> March 2026</p>

## Overview

SnapTray is a local-first screenshot and screen recording application. It only connects to the network when you explicitly trigger a Share URL upload or when the optional update checker runs. We are committed to protecting your privacy and being transparent about our practices.

**The short version:** SnapTray is local-first with no telemetry, tracking, or analytics. Network access is limited to two explicit features: Share URL (user-triggered image upload) and optional update checking (version string only).

## Data Collection

**We do not collect any data.** Specifically:

- No telemetry or usage analytics
- No crash reports sent to external servers
- No user tracking or unique identifiers

## Data Storage

All data generated or used by SnapTray is stored locally on your device:

| Data Type | Storage Location | Purpose |
|-----------|------------------|---------|
| Screenshots | User-specified folder (default: Pictures) | Your captured images |
| Recordings | User-specified folder (default: Downloads) | Your screen recordings |
| Settings | OS settings storage* | Your preferences |

*Settings storage locations:
- **macOS:** `~/Library/Preferences/cc.snaptray.plist`
- **Windows:** `HKEY_CURRENT_USER\Software\SnapTray`
- Legacy locations are migrated on first launch of newer builds and cleaned up after migration succeeds.

## Permissions

SnapTray requires certain system permissions to function. Here is why:

### macOS

| Permission | Why It Is Needed |
|------------|------------------|
| Screen Recording | Core functionality for screenshots and recordings |
| Accessibility | Window detection for smart capture features |

### Windows

| Permission | Why It Is Needed |
|------------|------------------|
| Full Trust | Required for screen capture (DXGI), audio capture (WASAPI), and OCR functionality |

**Optional permissions:**
- Microphone access is requested only if you choose to record audio.

## Network Communication

SnapTray connects to the network only for two explicit features:

### Share URL

When you explicitly use the Share URL action, your captured image is uploaded to `x.snaptray.cc`. You may optionally set a password. Shared links expire automatically. No upload happens unless you trigger it.

### Update Check

SnapTray checks `api.github.com` for new releases. This check is enabled by default and can be disabled in Settings. Only the app version string is sent. No personal data, usage information, or device identifiers are transmitted. Update checks are skipped for Store installs (e.g., Microsoft Store, Mac App Store).

### What SnapTray does not do

- Does not include any tracking SDKs or analytics
- Does not use cookies or similar technologies
- Does not send telemetry or crash reports

## Open Source

SnapTray is open source software released under the MIT License. You can review the complete source code to verify our privacy practices:

- [GitHub Repository](https://github.com/vfxfu/snap-tray)

## Children's Privacy

SnapTray does not collect any personal information from anyone, including children under 13 years of age.

## Changes to This Policy

If we make changes to this privacy policy, we will update the "Last Updated" date at the top of this page. You can check this page periodically, review release notes, or enable update notifications in Settings to stay informed.

## Contact

If you have questions about this privacy policy or SnapTray's privacy practices, open an issue on our [GitHub repository](https://github.com/vfxfu/snap-tray/issues).

---

## Summary

| Question | Answer |
|----------|--------|
| Does SnapTray collect my data? | No |
| Does SnapTray send data to servers? | Only when you use Share URL, or if update checking is enabled (version string only) |
| Does SnapTray track my usage? | No |
| Does SnapTray include analytics? | No |
| Does SnapTray phone home? | No. Update checks are opt-out and send only the version number |
| Where are my files stored? | Only on your local device |
| Can I verify these claims? | Yes, the source code is open |
