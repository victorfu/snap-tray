# Pin Window Controls, Mode State and Exits — Design

Issue: [#225](https://github.com/victorfu/snap-tray/issues/225). Tracking: #221. Follows #224 (toolbar grouping, `ToolbarOverflowPopup`) and #237.

## Problem

A pin is mostly an image, and its view controls are hard to find.

- **Zoom** works by wheel, edge resize and context-menu presets. It has no reset and no keyboard control. The "NN%" badge does not say which value it is, and it does not appear for menu changes.
- **Opacity** works by Ctrl+wheel, `[`/`]` and a menu entry buried under *Image processing*. It cannot be reset.
- **Click-through** can be left only through a small stylesheet pill on the pin. Its dashed-border cue is visible only when the border is shown.
- **Live Update** is shown only by a 6 px dot. Its `L`/`P` shortcuts appear nowhere.
- **The context menu** spreads multi-pin actions across three groups.

## Decisions

| Question | Decision |
| --- | --- |
| Where zoom/opacity values and resets live | A small hover bar on the pin: zoom %, opacity %, mode status, annotate, more. |
| How values are adjusted from the bar | A value button opens a small panel with a slider, presets and "Reset to 100%". |
| Click-through exits | A restyled glass pill ("Click-through · Turn off ✕") plus a tray item "Turn Off Click-through for All Pins". No new hotkey. |
| Implementation | A separate QML floating window, the same pattern as `QmlWindowedToolbar`. The value panel is a second popup window, like `ToolbarOverflowPopup`. |

Out of scope:
- rewriting `PinWindow` in QML;
- the unused shadow setting (`loadShadowEnabled`);
- changes to pin data or to the settings formats beyond one new boolean.

## 1. Hover bar

### Content (left to right)

- `🔍 150%`: the zoom value button, which opens the zoom panel.
- `◐ 80%`: the opacity value button, which opens the opacity panel.
- A status chip, shown only in a special mode: "● Live" or "⏸ Paused". Clicking it toggles pause.
- `✎` Annotate: shows the annotation toolbar, the same as Space.
- `⋯` More: opens the pin's context menu at the button.

### Appearance and placement

- It uses the shared QML `GlassSurface` and `ToolbarButton` and the #224 28×24 hit targets.
- It sits inside the pin's bottom edge, centred, 8 px up. The other corners stay free: top-left for the zoom badge and Live dot, top-right for the click-through pill, bottom-left for the opacity badge.
- On a narrow pin it keeps the two value buttons and `⋯`; the status chip and Annotate move into `⋯`.
- Below that width the bar is not shown, and the context menu and keyboard still work.

### When it appears

- After the pointer has rested on the pin for **300 ms**. The delay keeps the bar from flashing as the pointer crosses the pin.
- When the pin gets keyboard focus by Tab (shown only; see the keyboard note below).
- **Keyboard (implementation ruling, final review):** the bar and panel are non-activating windows, so they are pointer-only. Every action on them has a key on the focused pin (§2 Keyboard, `Space`, `L`/`P`), and any key other than Esc closes an open panel before the pin handles it.

### When it hides

- **500 ms** after the pointer has left the pin, the bar and the bar's panel.
- It never shows during a window drag or edge resize, while the annotation toolbar is visible, during crop, during region-layout editing, or while click-through is on.
- The bar is a separate window, so pin copy, save and merge exports never include it.

### Setting

"Show controls on hover", on by default, stored through `PinWindowSettingsManager`. It sits in the Pin Window section of the Advanced settings.

## 2. Value panels, keyboard, badges

### Zoom panel

- A slider from the pin's minimum zoom (`minimumPinWindowZoom()`) to 500%, applied live.
- Presets 33%, 50%, 100% and 200%.
- "Reset to 100%".
- A slider drag keeps one anchor (the pin's top-left corner), like an edge resize, so the pin does not wander.

### Opacity panel

- A slider from 10 to 100%.
- Presets 50%, 75% and 100%.
- "Reset to 100%".

### Both panels

- Each panel is a popup window anchored to its button. It installs its key/click-outside filter only while it is open, like `ToolbarOverflowPopup`.
- Esc, any other key, or a click outside closes it.
- Each slider and preset has an accessible name.

### Keyboard (pin focused)

| Keys | Action |
| --- | --- |
| `Ctrl/Cmd + =` | zoom in by the zoom step |
| `Ctrl/Cmd + -` | zoom out by the zoom step |
| `Ctrl/Cmd + 0` | reset zoom to 100% |
| `[` / `]` | opacity down/up (unchanged) |
| `Ctrl/Cmd + ]` | reset opacity to 100% |

The context menu shows each of these next to its item.

### Badges

- The text names the value: "Zoom 150%", "Opacity 80%".
- A badge appears for every change, whether from the wheel, edge resize, a menu item, a panel, a key or a reset.
- The zoom badge moves to the top-right while Live Update is active, so it does not cover the Live dot.

### Context-menu values

- The **Zoom** submenu holds the presets, the current value, "Reset to 100%" and Smoothing.
- A top-level **Opacity** submenu holds Increase and Decrease (with their shortcuts), the current value and "Reset to 100%". Opacity moves out of *Image processing*.

## 3. Modes and the context menu

### Click-through

- **The pill** is restyled with the shared glass and reads "Click-through · Turn off ✕". It has an accessible name and stays at the pin's top-right. Clicking it turns click-through off.
- **The tray menu** gains "Turn Off Click-through for All Pins", shown only while at least one pin has click-through on.
- **The state is visible without colour:** the pill states it in words even when the border is hidden.
- **Linux:** unchanged; the item is absent there.

### Live Update

- The bar's status chip reads "● Live" or "⏸ Paused" and toggles pause. Stop sits in `⋯`.
- The top-left dot keeps its colours and gains a tooltip: "Live Update (L to stop, P to pause)".
- The menu items show `L` and `P`.

### Annotation mode

No change. The annotation toolbar is the indicator, and the bar hides while it is visible.

### Context menu, grouped by task

1. **View:** Show Toolbar (Space), Show Border, Zoom ▸, Opacity ▸, Image processing ▸ (rotate, flip, crop)
2. **Output:** Copy (Ctrl+C), Save (Ctrl+S), Watermark ▸, Info ▸, Open History Folder
3. **Modes:** Click-through, Start/Stop Live Update (L), Pause/Resume (P), Frame Rate ▸, Adjust Region Layout
4. **Pins:** Move to Screen ▸, Merge Pins, Close All Pins
5. **Close** (Esc)

All existing shortcuts and mouse gestures stay.

### Clean-up

- The "Done (Space/Esc)" tooltip and the badge text go through `tr()`.
- `docs/docs/pin-window.md` no longer mentions a "pencil icon" to open the toolbar.

## 4. Testing and evidence

### Unit and QML tests

- `PinHoverBarViewModel`: values, status chip, compact layout, actions.
- Panels: range, presets, reset, closing on Esc and click-outside, focus return.

### `tests/PinWindow`

- The bar appears after 300 ms, survives the pointer moving onto it, and hides 500 ms after the pointer leaves.
- It is suppressed in each excluded state, and the setting turns it off.
- The keyboard shortcuts work.
- Badges name their value and appear for every change source.
- The click-through pill's text and accessible name; the tray item's visibility and its effect.
- Context-menu grouping and order. The existing translation-context tests still pass.

### Evidence (gated by environment variables, like #224)

- **Screenshots:**
  - the bar in light, dark and German;
  - the zoom panel;
  - a narrow pin;
  - the Live chip;
  - the click-through pill.
- **Operation video:** hover, zoom panel slider, reset, opacity, `⋯`, then leave.

### Runs

- `./scripts/run-tests.sh` on macOS: all pass.
- Windows and Ubuntu 22.04 X11 manual checks are added to #238; they are not marked as passed.

### Docs

- `docs/docs/pin-window.md` (English and zh-TW): the bar, the panels, resets, the shortcuts.
- `docs/developer/ui-design-guidelines.md`: a "Pin window" section with the contract, the acceptance matrix and the evidence.
- New strings are translated in all 24 languages.
