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
- The recording editor's **Canvas** (#184) uses the same gradients, but with names, a chosen state and the order Background, Padding, Corners, Shadow, Aspect ratio. The two panels describe the same look in different words.

## Decisions

| Question | Decision |
| --- | --- |
| Settings navigation | By task: General, Capture, Pin Windows, Recording, Saving, Text Recognition, Hotkeys, then Updates and About after a divider. Advanced, Files and Watermark go away as pages. |
| Beautify presets | Named **styles** (Compact, Standard, Spacious, Flat), each setting padding, corners and shadow together, plus named background swatches. The pixel controls move under a collapsible **Customize**. |
| Scope | Spec, settings regrouping and Beautify, plus (added during review) settings search, per-setting reset and saved Beautify styles, in one change. |
| Settings search | A field at the top of the sidebar; results open the page, scroll to the setting and highlight it. Labels reuse the pages' own translations. |
| Per-setting reset | A reset button in the row's gutter while a value differs from its manager default. |
| Saved styles | Named looks (the whole `BeautifySettings`), kept by `BeautifySettingsManager`, at most 8. |

Out of scope:
- style presets in the recording Canvas (it keeps its own Small/Medium/Large amounts);
- any change to existing setting keys or stored values. New: `beautify/customStyles` and three named recording defaults.

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

### Defaults and reset

- Defaults live in each manager. Before this change only **Hotkeys** could reset (per shortcut, and **Restore All Defaults**, which now sits in the page header).
- Changes save at once, as before. Nothing here asks for confirmation.

### Per-setting reset

- `SettingsBackend` keeps a table: property name → default, taken from the manager's public constants (`kDefault…`, `default…()`) in the property's UI units (percent, list index). Three recording defaults that were literals became constants (`kDefaultPreselectArea`, `kDefaultKeepProjects`, `kDefaultProjectCleanupDays`).
- `changedSettings` lists the keys whose value differs from the default; it is recomputed from each property's notify signal. `resetSetting(key)` writes the default through the property's setter, so the manager stores it as usual.
- A row names its setting with `resetKey`. While it is changed, a 20 px reset button sits in the 24 px gutter left of the label, so the row's content never moves. Tooltip "Reset to default"; accessible name "Reset <label> to default".
- 36 settings have a reset: everything on Capture, Pin Windows and Saving except the watermark image, most of Recording, and Updates. Language, theme, start on login, the input device, record keys, after saving, the watermark image and the OCR settings have none: they depend on the system or are a choice only the user can make.

### Search

- A "Search settings" field at the top of the sidebar; `Ctrl+F`/`Cmd+F` focuses it. While it has text, the results replace the page list; `Esc` clears it.
- The index in `SettingsSidebar.qml` lists each setting as `QT_TRANSLATE_NOOP(<page context>, <label>)` with its section and target, and resolves it with `qsTranslate`, so search reuses the pages' translations and matches the English text too. Pages match by name, settings by label, section or page, and hotkeys by action name (from `SettingsBackend`). Unsupported pages are never indexed.
- `Up`/`Down` moves through results, `Enter` or a click opens one: `SettingsWindow.revealSetting(page, target)` opens the page, waits for it to load, scrolls the row (found by objectName or `resetKey`) into view and highlights it for 1.6 s.

### Label column

`SettingsRow` widens its 140 px label column to fit the longest single word of its label, up to 45 % of the row, so long compounds (German "Weichzeichnungsintensität") do not break mid-word. Beyond that it still wraps.

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

- The eight gradients (Ocean … Charcoal), 28 px, each with an accessible name ("Background: Ocean") and a tooltip, the same names as the recording Canvas.
- The swatch matching the current linear gradient shows as chosen. Solid colours and the radial gradient are set under Customize.

### Customize

- Collapsed by default. It stays as left while the panel is reused in a session.
- Controls in the recording Canvas's order, without colons: Background (type), Colors, Padding, Corners, Shadow (the shared `SettingsToggle`), Shadow blur (disabled while the shadow is off), Aspect ratio.
- Expanding grows the panel; the panel keeps itself on the screen.

### Saved styles

- **Save style…** next to the Style heading (hidden while the look already is a saved style) opens an inline name field, prefilled "My style N". `Return` saves, `Esc` cancels the name without closing the panel.
- A saved style is the whole look: background, padding, corners, shadow and ratio. Saving under an existing name (any case) updates it. At most 8; at the limit the button is greyed out and its tooltip says to delete one.
- Saved cards sit below the built-in styles, drawn on their own background, with a × on hover or focus (`Delete` also works). A saved card is chosen when background, padding, corners, shadow and ratio all match.
- **Rename:** a ✎ on hover or focus (`F2` also works) opens the same inline field in rename mode, prefilled with the name; the button reads "Rename". `renameCustomStyle(index, name)` refuses an empty name or one another saved style uses (any case; changing only the case of its own name is allowed). While the typed name is taken, the field border turns red, a message says so, and Rename is disabled. The look is not changed. Renaming rebuilds the cards, so focus returns to the renamed card afterwards.
- Stored by `BeautifySettingsManager` as JSON under `beautify/customStyles`; values are clamped on load and unnamed or broken entries are dropped.

### Shared language with the recording Canvas

Both use the names Background, Padding, Corners, Shadow and Aspect ratio, the same gradient names, and a chosen ring in the accent colour. The Canvas keeps its Small/Medium/Large amounts, because its sizes follow the video size rather than pixels.

## 3. Accessibility and keyboard

- Style cards and swatches: Tab stops, Space/Return to apply, `Accessible.Button`, checkable and checked, visible focus ring.
- Customize: an `Accessible.Button`, checkable, checked while open (Qt Quick has no expanded state for buttons); Space/Return toggles.
- State is never shown by colour alone: a chosen card also has a thicker ring and is announced as checked; the "Custom" caption names the state.

## 4. Verification

| Check | How |
| --- | --- |
| Pages, keys, gating, divider | `Qml_SettingsWindowFeatureGating` |
| Every former setting still reachable | `Qml_SettingsWindowFeatureGating` (each page loads and contains its controls) |
| Styles apply and match, background chosen state | `Qml_BeautifyPanelBackend` |
| Customize, shadow toggle, sliders survive reopen | `Qml_BeautifyPanelQml` |
| Per-setting reset, search, reveal, label column | `Qml_SettingsWindowFeatureGating` |
| Saved styles: save, update, limit, delete, bad data, Esc while naming | `Qml_BeautifyPanelBackend`, `Qml_BeautifyPanelQml` |
| Light/dark, minimum window, German | `Qml_SettingsEvidence` screenshots (macOS) |
| Windows, Ubuntu X11, physical DPI, screen readers | Pending manual pass (#238) |
