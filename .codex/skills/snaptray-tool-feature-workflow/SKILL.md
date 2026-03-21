---
name: snaptray-tool-feature-workflow
description: "Plan or review end-to-end changes to the SnapTray tool system: adding a new tool, changing a tool contract, adjusting toolbar availability, or modifying tool options and persistence. Use when the change involves ToolId, ToolRegistry, a tool handler, toolbar view models, tool options, or host integration in RegionSelector, PinWindow, or ScreenCanvasSession. Do not use for generic overlay behavior that does not change a tool contract; use snaptray-overlay-surface-guard instead."
---

# SnapTray tool feature workflow

Your job is to make sure tool-related changes are implemented completely, not partially.

## What counts as a tool change

This includes:
- adding a new annotation or action tool
- changing whether a tool appears on a host toolbar
- changing default tool ordering or grouping
- changing tool options such as color, width, font, line style, shape type, fill mode, step badge size
- changing how a tool persists settings
- changing how a host reacts when a tool is selected

## Mandatory file checklist

When a task changes a tool contract, inspect these first:

### Core tool contract
- `include/tools/ToolId.h`
- `include/tools/ToolDefinition.h`
- `include/tools/ToolRegistry.h`
- `src/tools/ToolRegistry.cpp`
- `include/tools/ToolSectionConfig.h`
- `src/tools/ToolSectionConfig.cpp`
- `include/tools/ToolManager.h`
- `src/tools/ToolManager.cpp`

### Handler surface
- matching handler header in `include/tools/handlers/*`
- matching implementation in `src/tools/handlers/*`
- `include/tools/handlers/AllHandlers.h` when relevant

### Host integration
At least one of these must be checked when tool behavior changes:
- `src/RegionSelector.cpp`
- `src/PinWindow.cpp`
- `src/ScreenCanvasSession.cpp`
- `include/region/RegionToolbarHandler.h`
- `src/region/RegionToolbarHandler.cpp`

### Toolbar and options UI
Check the affected surfaces:
- `src/qml/PinToolbarViewModel.cpp`
- `src/qml/RegionToolbarViewModel.cpp`
- `src/qml/CanvasToolbarViewModel.cpp`
- `src/qml/PinToolOptionsViewModel.cpp`
- `src/qml/ToolbarViewModelBase.cpp`
- `src/qml/toolbar/*`

### Persistence and settings
If the tool stores defaults or remembers options, inspect the matching settings manager, usually:
- `include/settings/AnnotationSettingsManager.h`
- `src/settings/AnnotationSettingsManager.cpp`

Add other settings managers only if the tool truly belongs there.

### Documentation and user-facing strings
If a user-visible tool is added or changed, consider:
- `README.md`
- `README.zh-TW.md`
- `docs/docs/annotation-tools.md`
- `docs/zh-tw/docs/annotation-tools.md`
- any host-specific page such as `region-capture.md`, `pin-window.md`, or `screen-canvas.md`

## Common incomplete-change failures

Flag these hard if they appear:
- added `ToolId` but forgot `ToolRegistry`
- updated registry but forgot one of the host toolbars
- added handler but forgot host dispatch/selection behavior
- updated tool options UI but forgot persistence
- updated persistence but forgot initial view-model hydration
- added a tool to one host even though the product expects it on two or three hosts
- changed action semantics like undo/redo/cancel without reviewing host-specific behavior

## Required planning sequence

When planning or reviewing a tool change, follow this order:
1. Define the behavioral contract of the tool.
2. Identify which hosts should expose it.
3. Update registry and tool metadata.
4. Update or add the handler.
5. Wire host integration and dispatch.
6. Wire toolbar and options UI.
7. Wire persistence only if needed.
8. Add or update tests.
9. Update docs if user-visible.

## Required output format

Use this structure:

### Tool contract
What the tool does, where it appears, and what options it exposes.

### Files that must change
Grouped by `core`, `handler`, `host integration`, `toolbar/options`, `persistence`, `docs`.

### Easy-to-forget follow-up items
Short checklist.

### Tests
Exact test names, split into `must run` and `nice to run`.

### Safe patch split
Prefer 2 to 4 patches when the task is broad.

## Testing expectations

At minimum, consider:
- `Tools_ToolRegistry`
- the matching `Tools_*ToolHandler` test if it exists
- the host style-sync or surface test where the tool appears
- `Qml_PinToolOptionsViewModel` if options or option visibility changed
- `Qml_CanvasToolbarViewModel` if canvas toolbar behavior changed

## Important behavior

- Keep new tools data-driven when possible.
- Do not recommend direct `QSettings` access for tool defaults when an existing settings manager is the right home.
- Do not assume all hosts share the same action semantics.
- If the task is only about overlay placement or input without a tool contract change, say this skill is not the best fit.
