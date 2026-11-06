# Settings by Task, Beautify Styles — Design

Issue: [#227](https://github.com/victorfu/snap-tray/issues/227). Tracking: #221. Follows #224 (toolbar), #225 (pin controls) and #226 (Library).

## Problem

**Settings.** The window has nine pages, and the page names do not say which task a setting belongs to.

- **Advanced** mixes three tasks:
  - capture: the cursor companion, shortcut hints, what follows a full screen capture;
  - Auto Blur: intensity and type;
  - pins: opacity, steps, hover controls. It also holds "Max history entries", which caps the screenshots kept in the Library and is not a pin setting.
- **Files** and **Watermark** both decide what a saved file looks like and where it goes, but they are separate pages.
- Pages have no title, so after a click nothing says which page is open or what it is for.
- Section descriptions do not wrap, so a long translation runs past the window.

**Beautify.** The panel opens on controls rather than on results.

- The eight presets are 24 px gradient dots without names. They change only the background, and nothing shows which one is chosen.
- Padding, corners and shadow are only pixel sliders.
- Labels end in colons ("Padding:", "  Blur:"), and Shadow uses a hand-made switch without keyboard or screen reader support.
- The recording editor's **Frame** (#184) uses the same gradients, but with names, a chosen state and the order Background, Padding, Corners, Shadow, Aspect ratio. The two panels describe the same look in different words.

## Decisions

| Question | Decision |
| --- | --- |
| Settings navigation | By task: General, Capture, Pin Windows, Recording, Saving, Text Recognition, Hotkeys, then Updates and About after a divider. Advanced, Files and Watermark go away as pages. |
| Beautify presets | Named **styles** (Compact, Standard, Spacious, Flat), each setting padding, corners and shadow together, plus named background swatches. The pixel controls move under a collapsible **Customize**. |
| Scope | Spec, settings regrouping and Beautify in one change. Search and per-setting reset need new backend support, so they are out of scope (follow-up). |

Out of scope:
- settings search, per-setting reset, saving custom Beautify styles;
- style presets in the recording Frame (it keeps its own Small/Medium/Large amounts);
- any change to setting keys, stored values or managers.

## 1. Settings pages

Every setting keeps its manager, `SettingsBackend` property and stored key. Only the page it sits on changes.

| Page (key) | Sections and settings | Was |
| --- | --- | --- |
| General (`general`) | Start on login · Language · Appearance · Permissions (macOS) · Command Line | General |
| Capture (`capture`) | **After Capture:** After full screen capture · **Region Selector:** Cursor companion, Show shortcut hints · **Auto Blur:** Blur type, Blur intensity | Advanced |
| Pin Windows (`pins`) | **Appearance:** Default opacity, Show controls on hover · **Steps:** Opacity step, Zoom step | Advanced |
| Recording (`recording`, if supported) | unchanged | Recording |
| Saving (`saving`) | **Save Locations** · **Filename Format** · **Save Behavior** · **Watermark** (apply to images/recordings, image, scale, opacity, margin, position, preview) · **Library:** Screenshots kept in Library | Files, Watermark, Advanced |
| Text Recognition (`ocr`, macOS/Windows) | unchanged content | OCR |
| Hotkeys (`hotkeys`) | unchanged | Hotkeys |
| — divider — | | |
| Updates (`updates`) · About (`about`) | unchanged | |

- "Max history entries" is renamed **Screenshots kept in Library**, because #226 renamed History to Library. It is the same `PinWindowSettingsManager::loadMaxCacheFiles` value.
- The Auto Blur section names its feature, and a description says it applies to the automatic blur of faces and text.
- Platform gating is unchanged: Recording only when `recordingSupported`, Text Recognition only when `ocrSettingsVisible`. Pages still load on first selection.
- Hotkeys moves below the task pages: it covers every task, and Capture is now the first task.

### Page header

Each page except About (which already centres the app name) starts with `SettingsPageHeader`: the page name as a title and one sentence saying what it covers, for example "Capture — What happens when you take a screenshot, and how the region selector helps you aim." The sidebar stays the only navigation.

### Sidebar

- The same list, keyboard (Up/Down, Space, Return) and active accent as before.
- A thin divider separates the task pages from Updates and About. It is drawn above the first item of the second group, so indices and keyboard order are unchanged.

### Defaults and reset (inventory)

- Defaults live in each manager. No page has a "reset to default" control except **Hotkeys** (per-shortcut Reset and **Restore All Defaults**, which now sits in the page header), and this change adds none.
- Changes save at once, as before. Nothing here asks for confirmation.

## 2. Beautify panel

```text
┌ Beautify ─────────────────── ✕ ┐
│ [          preview           ] │
│ STYLE                          │
│ [Compact][Standard][Spacious][Flat]
│ BACKGROUND                     │
│ ● ● ● ● ● ● ● ●                │
│ ▸ Customize                    │
│   Background type · Colors     │
│   Padding · Corners            │
│   Shadow · Shadow blur         │
│   Aspect ratio                 │
│ [Copy] [Save] [Close]          │
└────────────────────────────────┘
```

### Styles

| Style | Padding | Corners | Shadow |
| --- | --- | --- | --- |
| Compact | 32 px | 8 px | on, blur 24 |
| Standard | 64 px | 12 px | on, blur 40 (the `BeautifySettings` defaults) |
| Spacious | 128 px | 16 px | on, blur 60 |
| Flat | 64 px | 0 px | off |

- A style writes only padding, corner radius, shadow on/off and shadow blur. The background, ratio and shadow offset/colour stay as they are.
- Each style is a card with a small drawing of that style on the current background, and its name below. The card that matches the current values shows as chosen; when none match, the caption reads "Custom".
- The styles are defined in `BeautifyPanelBackend.cpp` (`kStylePresets`); names are marked with `QT_TRANSLATE_NOOP("BeautifyPanel", ...)` so lupdate extracts them.

### Background swatches

- The eight gradients (Ocean … Charcoal), 28 px, each with an accessible name ("Background: Ocean") and a tooltip, the same names as the recording Frame.
- The swatch matching the current linear gradient shows as chosen. Solid colours and the radial gradient are set under Customize.

### Customize

- Collapsed by default. It stays as left while the panel is reused in a session.
- Controls in the recording Frame's order, without colons: Background (type), Colors, Padding, Corners, Shadow (the shared `SettingsToggle`), Shadow blur (disabled while the shadow is off), Aspect ratio.
- Expanding grows the panel; the panel keeps itself on the screen.

### Shared language with the recording Frame

Both use the names Background, Padding, Corners, Shadow and Aspect ratio, the same gradient names, and a chosen ring in the accent colour. The Frame keeps its Small/Medium/Large amounts, because its sizes follow the video size rather than pixels.

## 3. Accessibility and keyboard

- Style cards and swatches: Tab stops, Space/Return to apply, `Accessible.Button`, checkable and checked, visible focus ring.
- Customize: an `Accessible.Button` with an expanded state; Space/Return toggles.
- State is never shown by colour alone: a chosen card also has a thicker ring and is announced as checked; the "Custom" caption names the state.

## 4. Verification

| Check | How |
| --- | --- |
| Pages, keys, gating, divider | `Qml_SettingsWindowFeatureGating` |
| Every former setting still reachable | `Qml_SettingsWindowFeatureGating` (each page loads and contains its controls) |
| Styles apply and match, background chosen state | `Qml_BeautifyPanelBackend` |
| Customize, shadow toggle, sliders survive reopen | `Qml_BeautifyPanelQml` |
| Light/dark, minimum window, German | `Qml_SettingsEvidence` screenshots (macOS) |
| Windows, Ubuntu X11, physical DPI, screen readers | Pending manual pass (#238) |
