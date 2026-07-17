# Changelog

All notable SnapTray release changes are documented in this file.

This changelog is curated for release notes. GitHub Releases and the website release pages use the matching version section for release copy.

## [Unreleased]

### Fixed

- Fixed SnapTray occasionally hanging during shutdown, which could block relaunches and in-app updates after the menu bar icon disappeared.

### Removed

- Removed CLI recording commands; recordings now start only from the tray menu or recording hotkey.

## [1.0.59] - 2026-07-15

### Improved

- Refreshed the v1.0.58 release page and macOS/Windows update feeds so release and download metadata stay aligned.

### Fixed

- Fixed Region Capture occasionally remaining stuck as a transparent overlay instead of appearing.
- Fixed region capture copy on macOS so the selector waits for the clipboard write to finish before closing, preventing an immediate paste from using the previous clipboard image.
- Fixed website page titles and shared-link previews so ampersands and other special characters display correctly.

## [1.0.58] - 2026-06-20

### Improved

- Hid unfinished image-sharing actions from the region capture and Pin toolbars so only supported sharing flows appear in the app.
- Updated English, Traditional Chinese, Japanese, Korean, and Thai documentation and navigation to match the current sharing feature set.
- Refreshed v1.0.57 release-page and appcast metadata so update feeds stay aligned.

## [1.0.57] - 2026-06-15

### Added

- Added Japanese, Korean, and Thai website localization, including translated user docs, marketing pages, navigation labels, release/download strings, and hreflang metadata.

### Improved

- Improved the website language switcher with a data-driven dropdown that works better on mobile layouts.
- Improved Linux cursor theming by resolving Xcursor environment settings from X resources.
- Refreshed download fallbacks, appcast metadata, and website page titles so release pages stay current and clearer in search results.

### Fixed

- Fixed Windows OCR build compatibility with newer MSVC toolchains by removing obsolete coroutine flags.
- Fixed top-edge region selections so compact dimension labels appear correctly.
- Fixed Japanese comparison-table translation coverage.

## [1.0.56] - 2026-06-03

### Fixed

- Fixed toolbar copy-to-clipboard flows so large images no longer stall the UI before toast feedback appears.
- Fixed rapid successive copy actions on macOS so an older, slower image encode can no longer overwrite a newer clipboard copy.
- Fixed selection-completion toolbar placement so floating controls stay synchronized as capture selections finish, including Windows toolbar handoff.
- Fixed Windows Print Screen setup prompts and localized guidance so applying the shortcut setting is clearer and more reliable.

## [1.0.55] - 2026-05-28

### Improved

- Improved Windows CI and release-build reliability by keeping sccache configuration inside the workspace and allowing slower server startup.
- Refreshed release and update metadata after v1.0.54 so the website and in-app update feeds stay aligned.

### Fixed

- Fixed Windows Print Screen capture binding so the shortcut can be registered reliably as a global hotkey.
- Fixed hidden toast windows so they no longer intercept input when notifications are not visible.

## [1.0.54] - 2026-05-27

### Added

- Added the Ubuntu 22.04 X11 Linux beta with AppImage packaging, Linux-specific platform capability gating, and CI build coverage.

### Improved

- Improved Linux capture, Screen Canvas, pin toolbar focus, AppImage launch handling, and initial selection-toolbar responsiveness.
- Improved Linux feature availability so unsupported recording and OCR entry points stay hidden across the tray, CLI, tools, and settings.

### Fixed

- Fixed Capture Mode copy so the selected image is written to the clipboard before the selector closes, avoiding first-copy failures.
- Fixed Windows Print Screen shortcut conflict handling and compatibility with Snipping Tool-related settings.
- Fixed Linux AppImage packaging fallbacks and X11 capture overlay behavior.

## [1.0.53] - 2026-05-18

### Added

- Added visible hotkey registration status in UI menus so unavailable shortcuts are easier to diagnose.

### Fixed

- Fixed Windows `Print Screen` hotkey binding and conflict detection, including compatibility for older `Native:0x2C` settings and clearer Snipping Tool guidance.

## [1.0.52] - 2026-05-07

### Improved

- Improved macOS build and release reliability by pinning the active SDK path and refreshing cached CMake dependencies when the SDK changes.
- Moved macOS CI builds back to GitHub-hosted runners for more consistent release validation.

### Fixed

- Fixed annotation color persistence to store portable ARGB strings, keeping saved custom colors readable across machines and Qt settings backends.
- Fixed the toolbar width preview so the dot stays centered as stroke width changes.

## [1.0.51] - 2026-04-23

### Improved

- Refined the macOS menu bar icon sizing so SnapTray better matches native status item proportions.
- Refreshed the QR code toolbar icon to align with the shared Lucide-style icon set.

### Fixed

- Fixed macOS capture copying by writing PNG data eagerly, avoiding delayed pasteboard fulfillment crashes.

## [1.0.50] - 2026-04-12

### Improved

- Improved marker drawing so dense strokes render more cleanly and avoid unnecessary point noise.
- Improved shortcut-hint overlay repainting so anti-aliased edges stay clean while capture hints update.

### Fixed

- Fixed a macOS recording-preview crash that could happen when saving from the preview opened the native file dialog.

## [1.0.49] - 2026-04-10

### Improved

- Improved Windows download guidance with a clearer warning when using the unsigned executable package.

### Fixed

- Fixed Windows window-detection handling so SnapTray builds cleanly again and avoids unstable refresh behavior tied to GUI-thread-only APIs.

## [1.0.48] - 2026-04-10

### Improved

- Improved Beautify panel close-target layout so the dismiss control remains easier to hit in QML-driven flows.

### Fixed

- Fixed a detected-window refresh lifetime bug that could crash macOS while SnapTray updated the available window list asynchronously.

## [1.0.47] - 2026-04-09

### Improved

- Improved detected-window refresh handling so transient top-level windows stay targetable more reliably while SnapTray updates the available window list.

### Fixed

- Fixed Beautify panel dismissal so the close control remains clickable.
- Fixed repeat-badge feedback so auto-hidden badges can appear again on the next action instead of getting stuck suppressed.
- Fixed macOS Settings shutdown warnings triggered by input-method teardown during window close.

## [1.0.46] - 2026-04-08

### Added

- Added a screen picker dialog before recording starts so full-screen recordings can target the intended display explicitly.

### Improved

- Improved recording flows to use a consistent screen-first workflow, keeping tray and hotkey recording entry points aligned around full-screen capture.
- Improved macOS detected-window targeting by ranking accessibility matches in tiers so automatic selection is more likely to lock onto the intended window.
- Removed an unsupported automation surface from the app settings so unsupported controls no longer appear in the UI.

### Fixed

- Fixed OCR copy feedback to use the shared toast path so repeated copy actions no longer stack duplicate notifications.
- Fixed a macOS cursor-restore crash when the floating toolbar brings back the move cursor after capture interactions.

## [1.0.45] - 2026-04-04

### Improved

- Improved narrow-region capture layout with a compact size label and automatic control-panel hiding so small selections stay readable instead of overlapping the capture chrome.
- Updated the capture selection accent to follow the shared design-system palette for more consistent selection visuals across capture and pin workflows.

### Fixed

- Fixed shortcut-hint overlay rendering and made its labels translatable so capture help stays cleaner and localized across supported languages.
- Fixed macOS snapshot color preservation so captures, saved PNGs, and Pin windows better match the source display.
- Fixed a macOS toast overlay shutdown crash and restored proper enabled-state propagation for shared dialog and settings buttons.

## [1.0.44] - 2026-04-02

### Added

- Added a richer History window with smart-folder sidebar filters, sorting, and grid/list browsing so recent captures are easier to find.
- Added Beautify as a Pin toolbar action and expanded toolbar fallback placement so controls can stay attached to the selection even when outside positions are blocked.

### Improved

- Improved floating toolbar placement and attachment anchoring so region capture controls stay aligned to the active selection without covering as much working space.
- Declared the supported app localizations in the macOS bundle so system surfaces can present SnapTray in the languages the app ships.

### Fixed

- Fixed PinWindow initialization timing and reduced Windows pin drift so pinned captures open more reliably in the intended region.
- Fixed translation coverage for shared PinWindow actions, detected-window selection completion, and OCR copy cleanup to keep capture flows consistent across locales.
- Fixed a toast teardown crash caused by QQuickView destruction during static shutdown.

## [1.0.43] - 2026-03-30

### Improved

- Improved region capture toolbar placement and hover behavior so detached controls stay out of the way while selection movement remains predictable.

### Fixed

- Fixed the macOS move cursor to use a dedicated Size All cursor during region adjustments.
- Fixed the tray menu translation for `Check for Updates`.

## [1.0.42] - 2026-03-28

### Added

- Added `Paste from Clipboard` and `Check for Updates` actions to the tray menu so both flows are reachable directly from the menu.

### Improved

- Simplified the update-check path to rely on the platform update coordinator, reducing duplicate updater logic and keeping tray-triggered checks aligned with the existing settings backend.

## [1.0.41] - 2026-03-26

### Added

- Added copy-to-clipboard support in Screen Canvas exports.
- Added tray tooltip hotkey summaries.
- Made Beaver the default cursor companion for region capture.

### Improved

- Reworked region selection and annotation repaint paths to reduce full-screen overlay work and keep high-DPI interactions responsive.
- Improved Screen Canvas capture behavior by hiding active surfaces during copy and export capture.
- Consolidated developer documentation into the docs site and linked the canonical references from the repository root guidance.

### Fixed

- Fixed marker stroke gaps and multiple fractional-DPR seams, hover artifacts, and selection border issues across region capture workflows.
- Fixed magnifier tracking, detached floating UI restoration, and size readout alignment during capture transitions.
- Fixed Screen Canvas copy and export stability without native overlay handles, plus cursor companion alignment during fast movement.
