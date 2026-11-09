---
layout: page
title: Privacy Policy
seo_title: "SnapTray Privacy Policy: Local-First, No Telemetry"
permalink: /privacy/
description: SnapTray privacy policy. Local-first with no telemetry or tracking.
lang: en
route_key: privacy
---

<p><strong>Last Updated:</strong> November 2026</p>

## Overview

SnapTray is a local-first screenshot and annotation application with recording on macOS and Windows. Linux beta supports screenshots and annotation only. It only connects to the network when the optional update checker runs. We are committed to protecting your privacy and being transparent about our practices.

**The short version:** SnapTray is local-first with no telemetry, tracking, or analytics. Network access is limited to optional update checking (version string only).

This policy covers the SnapTray desktop app. The snaptray.cc website has its own section: [Website (snaptray.cc)](#website).

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
| Recordings | User-specified folder (default: Downloads) | Your screen recordings on macOS and Windows only. Linux beta does not include recording. |
| Settings | OS settings storage* | Your preferences |

*Settings storage locations:
- **macOS:** `~/Library/Preferences/cc.snaptray.plist`
- **Windows:** `HKEY_CURRENT_USER\Software\SnapTray`
- **Linux beta:** Qt/Linux user configuration storage under the user's config directory, commonly `~/.config/<organization>/<application>.conf`
- On macOS, obsolete pre-`cc.snaptray` preference files are removed during cleanup and are not imported. Windows legacy settings are migrated before cleanup.

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

SnapTray connects to the network for optional update checking. It does not upload screenshots.

### Update Check

SnapTray checks `api.github.com` for new releases. This check is enabled by default and can be disabled in Settings. Only the app version string is sent. No personal data, usage information, or device identifiers are transmitted. Update checks are skipped for Store installs (e.g., Microsoft Store, Mac App Store).

### What SnapTray does not do

- Does not include any tracking SDKs or analytics
- Does not use cookies or similar technologies
- Does not send telemetry or crash reports

## Website (snaptray.cc) {#website}

This section covers only the snaptray.cc website. It does not change anything above: the SnapTray app still collects no data.

The website asks before using analytics. Only if you choose **Allow** in the website's analytics prompt does it load Google Analytics 4 (provided by Google) to count visits and download interest. If you choose **Decline**, ignore the prompt, or your browser sends Global Privacy Control or Do Not Track, Google Analytics is not loaded.

After you allow it, the website sends:

- Page views: the page path without its query string, and the page language
- The referring website's domain (not its full address)
- Campaign tags from the link you arrived through (`utm_source`, `utm_medium`, `utm_campaign`, `utm_content`), only when they match the website's predefined list; other values are recorded as "other"
- Clicks on download, Microsoft Store and GitHub links: platform, release version, and where the link is on the page

Google also receives your IP address and browser details as part of providing the service; Google Analytics 4 does not log or store IP addresses. Google signals and ad personalization are turned off. The website does not send form input, full query strings, email addresses, or anything from the SnapTray app.

Cookies and browser storage:

| Name | Type | Set when | Purpose | Duration |
|------|------|----------|---------|----------|
| `_ga`, `_ga_<ID>` | Cookie (Google Analytics) | After you allow analytics | Tell repeat visits and sessions apart | Up to 2 years, or until you decline |
| `snaptray.analyticsConsent` | Local storage | When you choose Allow or Decline | Remember your choice | 12 months |
| `snaptray.attribution` | Session storage | After you allow analytics | Keep the campaign you arrived from while you browse | Until the tab closes, or after 30 minutes without a page view |

Analytics data is kept in Google Analytics for 2 months and then deleted. Aggregated reports (for example, total download clicks per campaign) may be kept longer.

To change your choice, use **Analytics settings** at the bottom of any page. Declining after allowing stops analytics and deletes this website's Google Analytics cookies in that browser.

Analytics counts are approximate: ad blockers, declined prompts and network problems mean some visits are never counted.

## Open Source

SnapTray is open source software released under the MIT License. You can review the complete source code to verify our privacy practices:

- [GitHub Repository](https://github.com/victorfu/snap-tray)

## Children's Privacy

SnapTray does not collect any personal information from anyone, including children under 13 years of age.

## Changes to This Policy

If we make changes to this privacy policy, we will update the "Last Updated" date at the top of this page. You can check this page periodically, review release notes, or enable update notifications in Settings to stay informed.

## Contact

If you have questions about this privacy policy or SnapTray's privacy practices, open an issue on our [GitHub repository](https://github.com/victorfu/snap-tray/issues).

---

## Summary

| Question | Answer |
|----------|--------|
| Does SnapTray collect my data? | No |
| Does SnapTray send data to servers? | Only if update checking is enabled (version string only) |
| Does SnapTray track my usage? | No |
| Does SnapTray include analytics? | No |
| Does SnapTray phone home? | No. Update checks are opt-out and send only the version number |
| Where are my files stored? | Only on your local device |
| Can I verify these claims? | Yes, the source code is open |
| Does the snaptray.cc website use analytics? | Only Google Analytics 4, and only after you allow it in the website prompt. See [Website](#website) |
