---
name: snaptray-architecture-review
description: Review a SnapTray patch, PR, or proposed design for repository-specific architecture compliance, cross-layer mistakes, missing follow-up work, and missing tests or docs. Use when the user asks for a serious SnapTray-focused code review rather than a generic style review. Do not use for initial task routing before implementation; use snaptray-change-router for that.
---

# SnapTray architecture review

Your job is to review a SnapTray change against the repository's actual architectural rules, not generic C++ style advice.

## Core rules to enforce

### 1. Prefer the existing settings managers
If a change introduces or expands persistence, prefer the appropriate manager instead of ad-hoc direct `QSettings` access.

Typical managers include:
- `AnnotationSettingsManager`
- `FileSettingsManager`
- `PinWindowSettingsManager`
- `AutoBlurSettingsManager`
- `OCRSettingsManager`
- `WatermarkSettingsManager`
- `RecordingSettingsManager`
- `RegionCaptureSettingsManager`
- `ScreenCanvasSettingsManager`
- `UpdateSettingsManager`

If direct `QSettings` is used, treat that as a likely architecture issue unless there is a clear established exception.

### 2. Respect the platform abstraction
Platform-specific capabilities should flow through `PlatformFeatures` or the established platform-specific layer.

Flag it when shared UI code starts directly depending on platform-specific details without going through the intended abstraction.

### 3. Respect the hotkey boundary
Global hotkey behavior should stay centered in `HotkeyManager` and related types.

Flag it when unrelated features start re-implementing hotkey persistence, registration, or conflict logic.

### 4. Keep MCP debug-only unless explicitly required
Treat MCP surfaces as debug/developer-facing unless the task clearly expands product scope.

Flag accidental release-surface coupling.

### 5. Prefer data-driven routing over repeated switch sprawl
When the repo already uses registries, dispatch tables, or configuration objects, prefer extending those patterns over adding scattered duplicated logic.

### 6. Keep host-specific behavior intentional
Do not force RegionSelector, PinWindow, and ScreenCanvasSession into one-size-fits-all behavior when the product semantics differ.

### 7. Look for incomplete cross-layer updates
This repo often requires touching more than one layer. Flag it when a patch updates one but not the others.

Common misses:
- settings manager changed but `SettingsBackend` or QML settings UI not updated
- tool registry changed but toolbar/view model or host dispatch not updated
- user-visible behavior changed but docs were not updated
- platform behavior changed but packaging, permissions, or updater flow not considered
- translation-sensitive strings changed without considering `Settings_QmlTranslations` or docs parity

## Review severity buckets

Use these exact buckets:

### Blocking issues
Real defects, likely broken behavior, or serious architecture violations.

### High-risk issues
Probably correct today but fragile, inconsistent, or likely to regress.

### Follow-up items
Non-blocking but should be done before or soon after merge.

### Tests to run
Exact tests that best validate the risky surfaces.

## What good review looks like here

A good review should answer:
- Did the patch respect the existing module boundaries?
- Did it update every required layer?
- Did it reuse the established manager/registry/abstraction instead of creating a parallel path?
- Are there missing tests or missing docs?
- Is the smallest safe fix obvious?

## What not to do

- Do not flood the review with style-only remarks.
- Do not invent architecture rules that are not visible in the repo.
- Do not demand a giant rewrite when a small fix will align the patch with the existing design.
- Do not call something a bug unless you can explain the failure mode.

## Required output format

Use this structure:

### Overall assessment
One short paragraph.

### Blocking issues
Numbered list.

### High-risk issues
Numbered list.

### Follow-up items
Bullet list.

### Tests to run
Exact test names.

### Suggested minimal fixes
One short list of the smallest concrete fixes.

## Important behavior

- Be strict about real architecture drift, not personal taste.
- When a patch is clean, say so explicitly.
- If the patch belongs more naturally to a narrower SnapTray skill, say which one and why.
