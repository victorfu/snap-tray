# Privacy Policy

**Last Updated:** January 2026

## Overview

SnapTray is a screenshot and screen recording application that operates entirely on your local device. We are committed to protecting your privacy and being transparent about our practices.

**The short version:** SnapTray does not collect, transmit, or share any of your data. Everything stays on your device.

## Data Collection

**We do not collect any data.** Specifically:

- No telemetry or usage analytics
- No crash reports sent to external servers
- No user tracking or unique identifiers
- No network communication of any kind

## Data Storage

All data generated or used by SnapTray is stored locally on your device:

| Data Type | Storage Location | Purpose |
|-----------|------------------|---------|
| Screenshots | User-specified folder (default: Pictures) | Your captured images |
| Recordings | User-specified folder (default: Downloads) | Your screen recordings |
| Settings | OS settings storage* | Your preferences |

*Settings storage locations:
- **macOS:** `~/Library/Preferences/com.victorfu.snaptray.plist`
- **Windows:** `HKEY_CURRENT_USER\Software\Victor Fu\SnapTray`

## Permissions

SnapTray requires certain system permissions to function. Here's why:

### macOS

| Permission | Why It's Needed |
|------------|-----------------|
| Screen Recording | Core functionality - capturing screenshots and recordings |
| Accessibility | Window detection for smart capture features |

### Windows

| Permission | Why It's Needed |
|------------|-----------------|
| Full Trust | Required for screen capture (DXGI), audio capture (WASAPI), and OCR functionality |

**Optional permissions:**
- Microphone access is only requested if you choose to record audio with your screen recordings

## Third-Party Services

SnapTray does not integrate with any third-party services, analytics platforms, or cloud storage providers. The application:

- Does not connect to the internet
- Does not send data to any servers
- Does not include any tracking SDKs
- Does not use cookies or similar technologies

## Open Source

SnapTray is open source software released under the MIT License. You can review the complete source code to verify our privacy practices:

- [GitHub Repository](https://github.com/vfxfu/snap-tray)

## Children's Privacy

SnapTray does not collect any personal information from anyone, including children under 13 years of age.

## Changes to This Policy

If we make changes to this privacy policy, we will update the "Last Updated" date at the top of this page. Since SnapTray has no network connectivity, you would need to check this page manually or review release notes for any updates.

## Contact

If you have questions about this privacy policy or SnapTray's privacy practices, please open an issue on our [GitHub repository](https://github.com/vfxfu/snap-tray/issues).

---

## Summary

| Question | Answer |
|----------|--------|
| Does SnapTray collect my data? | No |
| Does SnapTray send data to servers? | No |
| Does SnapTray track my usage? | No |
| Does SnapTray include analytics? | No |
| Where are my files stored? | Only on your local device |
| Can I verify these claims? | Yes, the source code is open |
