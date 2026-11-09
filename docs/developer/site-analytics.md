---
last_modified_at: 2026-11-08
layout: docs
title: Website Analytics
seo_title: "SnapTray Website Analytics: GA4 Setup, Event Contract and Attribution"
description: How snaptray.cc measures campaign traffic and download clicks with opt-in Google Analytics 4, and how to read the data without exposing secrets.
permalink: /developer/site-analytics/
lang: en
route_key: developer_site_analytics
nav_data: developer_nav
docs_copy_key: developer
---

This page covers analytics on the **snaptray.cc website only**. The desktop app has no telemetry and this does not change that.

## How it is wired

| Piece | File |
|-------|------|
| On/off switch and production host | `docs/_config.yml` → `analytics.ga4_measurement_id`, `analytics.production_host` |
| Campaign allow-list | `docs/_data/analytics_campaigns.yml` |
| Config JSON, consent prompt, script tag | `docs/_includes/analytics.html` (included by `_layouts/default.html`) |
| Consent, attribution, link tracking | `docs/assets/js/analytics.js` |
| Prompt and footer strings | `analytics_consent` in `docs/_data/i18n/*.yml` |
| Public disclosure | "Website (snaptray.cc)" section of every `privacy-policy.md` |
| Contract tests | `tests/Docs/tst_SiteAnalytics.js` (CTest `Docs_SiteAnalytics`, needs Node 18+) |

Analytics only runs when **all** of these hold:

1. `ga4_measurement_id` is set (empty by default, which ships nothing).
2. The site is built with `JEKYLL_ENV=production` (GitHub Pages does this; `jekyll serve` does not).
3. The page is served from `production_host`, so forks and previews never send data.
4. The browser sends neither Global Privacy Control nor Do Not Track. With either signal there is no prompt, no footer **Analytics settings** link, and nothing loads, even if the visitor allowed analytics earlier.
5. The visitor chose **Allow** in the prompt. Before that, Google's script is not loaded, nothing is stored, and no request goes to Google.

Tracking never calls `preventDefault`, never waits for the network, and uses `transport_type: 'beacon'`, so a blocked or failed analytics request cannot stop a download.

## Event contract

All events carry `page_language`, `traffic_source`, `traffic_medium`, `traffic_campaign`, `traffic_content` and `target_market`.

| Event | Fired on | Extra parameters |
|-------|----------|------------------|
| `page_view` | Each page load after consent (sent manually; automatic page views are off) | — |
| `download_click` | Click or middle-click on a GitHub release asset ending in `.dmg`, `.exe` or `.AppImage` | `platform` (`macos`/`windows`/`linux`), `release_version` (tag taken from the clicked URL, or `unknown`), `placement` |
| `store_click` | Link to `apps.microsoft.com` | `store=microsoft`, `placement` |
| `github_click` | Any other link into the repository | `destination_type` (`repository`, `releases`, `issues`, `source`, `release_asset`, `other`), `placement` |

- `placement` comes from the nearest `data-analytics-placement` ancestor: `header`, `hero`, `download_card`, `release_list`, `footer`, or `other`.
- "Jump to download" links (`#download`) and not-yet-populated buttons (`href="#"`) are not events.
- Links are matched by event delegation on `document`, so the asynchronously rendered release buttons and release list are covered without rebinding. Keyboard activation fires `click`, so it is covered too.
- The release version comes from the asset URL itself. When the Linux button falls back to an older release that still has an AppImage, the event reports that older tag.

## Campaign values and attribution

UTM values are lowercased and must be listed in `analytics_campaigns.yml`:

- Unknown `utm_campaign` → every field is `other` and the market is `unknown`.
- Known campaign with an unlisted source or medium → that field is `other`.
- `utm_content` must look like `post01` / `group2` (letters, then 1–3 digits); anything else is `other`.

Never put email addresses, account names or free text into a UTM tag. Add new channels to the YAML **before** publishing their links.

Attribution is kept in `sessionStorage` (`snaptray.attribution`) only after consent:

1. A recognized campaign in the URL starts a new attribution and replaces an earlier one.
2. Otherwise a stored attribution is kept if the last page view was less than 30 minutes ago, so in-site navigation and language switches keep the original campaign.
3. Otherwise: unrecognized UTM → `other`; external referrer → `<domain> / referral`; no referrer → `(direct) / (none)`; internal referrer with nothing stored (expired, new tab, storage blocked) → `unknown`. Nothing is invented.

This 30-minute tab window is our own rule and is not the same as a GA4 session. `target_market` comes from the campaign table, never from page language or visitor location.

## What is sent to Google

- `page_location` is origin + path. On the landing page only, the recognized UTM values are re-added so GA's own acquisition reports see the campaign. Other query parameters and fragments are dropped.
- `page_referrer` is the referring site's origin only (same-site referrers keep their path).
- Google signals and ad personalization are off; `ad_storage`, `ad_user_data` and `ad_personalization` consent are denied.
- Declining after allowing sets Google's `ga-disable-MEASUREMENT_ID` flag before updating consent to denied, stops events, clears tab attribution and test-traffic state, and deletes `_ga` / `_ga_*` cookies. Allowing again clears the disable flag.
- Other open tabs listen for shared consent changes and stop tracking on revocation, removal, or invalid/expired consent. They do not write the choice back or automatically start tracking when another tab grants consent.

## One-time GA4 setup

Do these in the GA4 admin before setting the measurement ID:

1. Create a GA4 property and a web data stream for `https://snaptray.cc`. Record the account owner.
2. **Data retention**: event data retention = **2 months** (the privacy policy states this). Change the policy text in all five languages if you change it.
3. **Enhanced measurement**: turn off outbound clicks, file downloads, site search, form interactions and page changes based on browser history. They would duplicate our events and could send full link URLs.
4. **Google signals**: off. Leave ads links off.
5. **Custom dimensions** (event scope): `page_language`, `placement`, `platform`, `release_version`, `store`, `destination_type`, `traffic_source`, `traffic_medium`, `traffic_campaign`, `traffic_content`, `target_market`.
6. **Internal traffic**: create a data filter that excludes `traffic_type = internal`. Opening any page with `?analytics_test=1` marks the tab's events as internal and enables DebugView, so you can verify events without polluting reports.
7. Report time zone: Asia/Taipei.

Then set `analytics.ga4_measurement_id` in `docs/_config.yml`. **Deploy the updated privacy policy before or together with the ID.**

## Reading the data (dot and maintainers)

Use the [GA4 Data API](https://developers.google.com/analytics/devguides/reporting/data/v1) with a dedicated Google Cloud service account that has the **Viewer** role on the property only. Keep its key in the runner's secret store; never commit it, paste it into an issue, or ship it to the website. Until that access exists and one read has succeeded, reporting is a manual export from the GA4 UI and must be labelled as such.

Report definitions:

- Visits per campaign: `page_view` count or GA sessions, grouped by `traffic_campaign` / `traffic_source` / `target_market`. Name the metric after the GA definition used.
- `download_click` count ÷ sessions is a **click ratio** and can exceed 100%. Only "sessions with ≥1 download_click ÷ sessions" is a session conversion rate.
- GitHub `download_count` and stars are separate sources and are not attributable to one campaign.

## Known limits

- Only visitors who choose **Allow** and send no GPC/DNT signal are counted; ad blockers and network failures reduce counts further. Treat numbers as lower bounds, not visitor totals.
- A link opened in a new tab may not inherit `sessionStorage` and then reports `unknown`.
- Download clicks show intent, not completed downloads, installs or retention.
