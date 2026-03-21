---
name: snaptray-overlay-surface-guard
description: Review risky UI and interaction changes in SnapTray overlay surfaces such as RegionSelector, PinWindow, ScreenCanvasSession, floating toolbars, transient panels, selection state, and style synchronization. Use when a change touches input handling, overlay placement, selection state, floating toolbars, transient UI, multi-region flows, or host-specific annotation surfaces. Do not use for backend-only settings changes, encoder internals, or general PR review with no overlay behavior involved.
---

# SnapTray overlay surface guard

Your job is to protect the most fragile SnapTray interaction surfaces from subtle regressions.

## Scope

This skill is for these families of changes:
- `src/RegionSelector.cpp`, `src/region/*`, `include/region/*`
- `src/PinWindow.cpp`, `src/pinwindow/*`
- `src/ScreenCanvasSession.cpp`
- floating toolbar and panel code in `src/qml/*Toolbar*`, `src/qml/QmlFloating*`, `src/qml/QmlOverlay*`
- `src/qml/PinToolOptionsViewModel.cpp`
- state/placement/selection code connected to these hosts

## Things to inspect every time

### 1. Input routing and cancellation
Check whether the patch changes any of these flows:
- mouse press / drag / release
- hover or cursor feedback
- keyboard shortcuts like Escape, Enter, Delete
- cancel semantics while transient UI is open
- whether toolbar clicks steal or preserve the expected host state

Look for mismatches between visual state and input state.

### 2. Selection state transitions
Check that selection state remains coherent across:
- creating a selection
- resizing or transforming
- switching tools
- entering crop, OCR, QR, pin, or share actions
- multi-region reorder, replace, delete, and done flows
- undo/redo around selection-affecting actions

The patch must not leave stale selection handles, stale hover state, or orphaned transient UI.

### 3. Dirty region and repaint behavior
When geometry, handles, or overlays change, verify that the patch does not create:
- missing repaint after state change
- over-repaint or flicker from dirty-region inflation mistakes
- stale magnifier or stale guide overlays
- style sync updates that arrive visually but not logically, or vice versa

### 4. Toolbar and option-panel synchronization
Whenever a tool, color, width, font, line style, shape type, or step badge size changes, check all three layers:
- host state (`RegionSelector`, `PinWindow`, or `ScreenCanvasSession`)
- toolbar view model / tool-options view model
- persisted settings where applicable

A common failure is updating one layer without the other two.

### 5. Placement and transient UI safety
For floating toolbars, sub-toolbars, overlays, panels, and popups, verify:
- placement still follows the active region or host window correctly
- geometry updates on move, resize, zoom, DPR, or screen change
- transient UI closes or survives exactly when intended
- cancel/escape does not accidentally dismiss the wrong layer

### 6. Host-specific behavior differences
Do not assume RegionSelector, PinWindow, and ScreenCanvas behave identically.

Check whether the patch accidentally copied rules from one host to another when the product behavior differs.

## SnapTray-specific regression checklist

Always consider these before giving approval:
- Did selection/input logic change without running `RegionSelector_SelectionStateManager` or `RegionSelector_RegionInputHandler`?
- Did toolbar/options syncing change without considering `RegionSelector_StyleSync`, `PinWindow_StyleSync`, or `ScreenCanvas_StyleSync`?
- Did transient UI behavior change without considering `RegionSelector_TransientUiCancelGuard`?
- Did multi-region logic change without considering the `RegionSelector_MultiRegion*` tests?
- Did floating toolbar code change without considering `Qml_FloatingToolbar` or `Qml_PinToolOptionsViewModel`?
- Did crop/history interactions in pin mode change without considering `PinWindow_CropUndo` or history tests?

## Required output format

Use this structure:

### Overlay risk summary
One short paragraph.

### Likely regression vectors
A ranked list.

### Surfaces that must stay in sync
List host state, view model, and persistence/UI layers that must agree.

### Targeted tests to run
Exact test names.

### Approval status
Use one of:
- safe to continue
- needs targeted fixes first
- too risky without deeper rewrite or test coverage

## Important behavior

- Be concrete; name the exact regression mode you suspect.
- Prefer state-transition bugs over vague style commentary.
- If the patch is safe, say why it is safe.
- If no overlay surface is involved, say this skill is not the right one.
