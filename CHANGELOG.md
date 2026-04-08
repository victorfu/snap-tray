# Changelog

All notable SnapTray release changes are documented in this file.

This changelog is curated for release notes. GitHub Releases and the website release pages use the matching version section for release copy.

## [Unreleased]

- No unreleased entries yet.

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
