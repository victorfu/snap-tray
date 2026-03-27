# Changelog

All notable SnapTray release changes are documented in this file.

This changelog is curated for release notes. GitHub Releases and the website release pages use the matching version section for release copy.

## [Unreleased]

- No unreleased entries yet.

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
