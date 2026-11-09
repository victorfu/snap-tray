# Pin Window Controls Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Pins get a hover bar with zoom and opacity values, a panel with a slider, presets and reset for each, a status chip, Annotate and More. Click-through gets clearer exits. Badges name their value. The context menu is grouped by task.

**Architecture:**
- **View model.** A plain `PinHoverBarViewModel` (QObject) holds the bar's values and turns QML clicks into signals.
- **Windows.** `PinHoverBar` owns two QML windows created through `QmlOverlayManager`: the bar (`PinHoverBar.qml`, a tool window that does not take focus) and the value panel (`PinValuePanel.qml`, a popup window). This is the same pattern as `QmlWindowedToolbar` and `ToolbarOverflowPopup`. `PinHoverBar` also owns the show/hide timers and positioning.
- **Wiring.** `PinWindow` feeds the bar its state and hover events, and applies what the bar asks for. Keyboard, badges, menu regrouping, the click-through pill and the tray item are small changes in their existing classes.

**Tech Stack:** Qt 6.12 (Widgets, Quick), C++17, QML, Qt Test. `./scripts/build.sh`, `./scripts/run-tests.sh`.

**Spec:** `docs/superpowers/specs/2026-11-05-pin-window-controls-design.md`

## Global Constraints

- **Timing:** the bar appears after the pointer has rested on the pin for 300 ms. It hides 500 ms after the pointer has left the pin, the bar and the panel.
- **Placement:** inside the pin's bottom edge, centred, 8 px up. Hit targets are 28×24, from the #224 shared `ToolbarButton`.
- **Hidden** during a window drag or edge resize, while the annotation toolbar is visible, during crop, during region-layout editing, while click-through is on, and when the setting is off.
- **Value limits:**
  - Zoom: `minimumPinWindowZoom(...)` to 500%; presets 33/50/100/200%; reset 100%.
  - Opacity: 10–100%; presets 50/75/100%; reset 100%.
- **Keys** (pin focused): `Ctrl/Cmd+=`, `Ctrl/Cmd+-`, `Ctrl/Cmd+0`, and `Ctrl/Cmd+]` to reset opacity. `[` and `]` are unchanged.
- **Setting:** `pinWindow/showHoverControls` (bool, default true) through `PinWindowSettingsManager`. No direct `QSettings`.
- **Shared UI:** `GlassSurface`, `ToolbarButton`, `ComponentTokens`/`SemanticTokens` and `QmlOverlayManager` window types. No new visual system.
- **Translations:** every new user-visible string goes through `tr()`/`qsTr()` in all 24 languages. Contexts are `PinWindow`, `PinHoverBar`, `PinValuePanel`, `ClickThroughExitButton` and `MainApplication`.
- **Unchanged:** all existing shortcuts, mouse gestures and pin data.

## Review Focus

1. **The pointer moves from the pin onto the bar**, which is a separate window, so the pin gets `leaveEvent`. The bar must not hide while the pointer is over the bar or its panel. **[RF 1]**
2. **The pin moves or is zoomed while the bar or panel is open** (drag, wheel, panel slider). The bar stays attached to the pin's bottom edge and the panel stays attached to its button. A window drag hides both. **[RF 2]**
3. **A narrow or tiny pin.** Compact mode keeps the two value buttons and More. Below the minimum width the bar never shows, and nothing overflows the pin. **[RF 3]**
4. **The pin closes, hides, or enters click-through while the bar or panel is open.** Both windows go away, with no dangling event filter and no crash. **[RF 4]**
5. **Panel keyboard.** Esc or a click outside closes the panel and returns focus to its button. The keys go to the panel, not to the pin's shortcuts, while it is open. **[RF 5]**

---

### Task 1: Setting "Show controls on hover"

**Files:**
- Modify: `include/settings/PinWindowSettingsManager.h`, `src/settings/PinWindowSettingsManager.cpp`, `include/qml/SettingsBackend.h`, `src/qml/SettingsBackend.cpp`, `src/qml/settings/AdvancedSettings.qml`
- Test: `tests/Settings/tst_PinWindowSettingsManager.cpp`

**Interfaces:**
- Produces:
  - `bool PinWindowSettingsManager::loadShowHoverControls() const`
  - `void saveShowHoverControls(bool)`
  - `static constexpr bool kDefaultShowHoverControls = true`
  - key `"pinWindow/showHoverControls"`
  - `SettingsBackend::pinShowHoverControls` (Q_PROPERTY bool)

- [ ] **Step 1: Failing test** (add to `tst_PinWindowSettingsManager.cpp`, matching the file's existing load/save tests, which clear the key in `init`/`cleanup`)

```cpp
void testShowHoverControlsDefaultsOnAndRoundTrips()
{
    auto& mgr = PinWindowSettingsManager::instance();
    SnapTray::getSettings().remove(PinWindowSettingsManager::kSettingsKeyShowHoverControls);
    QVERIFY(mgr.loadShowHoverControls());
    mgr.saveShowHoverControls(false);
    QVERIFY(!mgr.loadShowHoverControls());
    mgr.saveShowHoverControls(true);
    QVERIFY(mgr.loadShowHoverControls());
}
```

- [ ] **Step 2: Run** `cd build && ninja Settings_PinWindowSettingsManager`. Expected: a compile error, because `loadShowHoverControls` is not a member.

- [ ] **Step 3: Implement**, following the shadow setting's pattern.

```cpp
// header
bool loadShowHoverControls() const;
void saveShowHoverControls(bool enabled);
static constexpr bool kDefaultShowHoverControls = true;
static constexpr const char* kSettingsKeyShowHoverControls = "pinWindow/showHoverControls";
// cpp
bool PinWindowSettingsManager::loadShowHoverControls() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeyShowHoverControls, kDefaultShowHoverControls).toBool();
}
void PinWindowSettingsManager::saveShowHoverControls(bool enabled)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyShowHoverControls, enabled);
}
```

`SettingsBackend` gets `Q_PROPERTY(bool pinShowHoverControls READ … WRITE … NOTIFY pinShowHoverControlsChanged)`. Copy the `pinZoomStep` member, load and setter shape exactly.

In `AdvancedSettings.qml`, before the "Zoom step" slider in the Pin Window section:

```qml
SettingsToggle {
    label: qsTr("Show controls on hover")
    checked: settingsBackend.pinShowHoverControls
    onToggled: function(checked) { settingsBackend.pinShowHoverControls = checked }
}
```

- [ ] **Step 4: Run** `./bin/Settings_PinWindowSettingsManager`. Expected: all pass.

- [ ] **Step 5: Commit** `feat(pin): setting for hover controls (#225)`.

---

### Task 2: `PinHoverBarViewModel`

**Files:**
- Create: `include/qml/PinHoverBarViewModel.h`, `src/qml/PinHoverBarViewModel.cpp` (in the same CMake source and header lists as `PinToolbarViewModel`)
- Test: `tests/Qml/tst_PinHoverBarViewModel.cpp` (register it like `Qml_PinToolbarViewModel`)

**Interfaces:**
- Produces:

```cpp
class PinHoverBarViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(int zoomPercent READ zoomPercent NOTIFY valuesChanged)
    Q_PROPERTY(int minZoomPercent READ minZoomPercent NOTIFY valuesChanged)
    Q_PROPERTY(int opacityPercent READ opacityPercent NOTIFY valuesChanged)
    Q_PROPERTY(int status READ status NOTIFY statusChanged)          // Status enum
    Q_PROPERTY(bool compact READ compact NOTIFY compactChanged)
    Q_PROPERTY(int panel READ panel NOTIFY panelChanged)              // Panel enum
public:
    enum Status { None = 0, Live, Paused }; Q_ENUM(Status)
    enum Panel { NoPanel = 0, ZoomPanel, OpacityPanel }; Q_ENUM(Panel)
    static constexpr int kMaxZoomPercent = 500;
    static constexpr int kMinOpacityPercent = 10;
    static constexpr int kCompactWidth = 220;   // below this: value buttons + More only
    static constexpr int kMinimumWidth = 120;   // below this: no bar
    void setValues(qreal zoom, qreal minZoom, qreal opacity);
    void setStatus(Status s);
    void setAvailableWidth(int width);          // sets compact
    bool fits(int width) const { return width >= kMinimumWidth; }
    Q_INVOKABLE void openPanel(int panel);       // toggles; emits panelChanged
    Q_INVOKABLE void closePanel();
    Q_INVOKABLE void requestZoomPercent(int percent);      // clamped; emits zoomRequested
    Q_INVOKABLE void requestOpacityPercent(int percent);   // clamped; emits opacityRequested
    Q_INVOKABLE void requestAnnotate();
    Q_INVOKABLE void requestMore();
    Q_INVOKABLE void toggleStatus();                        // Live→pause, Paused→resume
signals:
    void valuesChanged(); void statusChanged(); void compactChanged(); void panelChanged();
    void zoomRequested(qreal zoom); void opacityRequested(qreal opacity);
    void annotateRequested(); void moreRequested();
    void pauseRequested(); void resumeRequested();
};
```

- [ ] **Step 1: Failing tests**

```cpp
void valuesRoundToPercent();     // setValues(1.504, 0.1, 0.8) → 150, 10, 80; one valuesChanged
void zoomRequestIsClamped();     // minZoom 0.25: requestZoomPercent(10) → zoomRequested(0.25); 900 → 5.0
void opacityRequestIsClamped();  // 5 → 0.10; 150 → 1.0
void compactBelowThreshold();    // setAvailableWidth(219) → compact; 220 → not; compactChanged once per flip
void fitsMinimum();              // fits(119) false, fits(120) true
void panelToggles();             // openPanel(Zoom) → Zoom; again → NoPanel; Opacity then closePanel → NoPanel
void statusToggle();             // Live → toggleStatus emits pauseRequested; Paused → resumeRequested; None → nothing
```

Write these with `QSignalSpy` in the style of `tst_PinToolbarViewModel.cpp`.

- [ ] **Step 2: Run** `ninja Qml_PinHoverBarViewModel`. Expected: a compile error, because the header is missing.

- [ ] **Step 3: Implement.** Clamp with `qBound`. `setValues` emits only when a rounded value changes. Zoom converts to `percent / 100.0`.

- [ ] **Step 4: Run** `./bin/Qml_PinHoverBarViewModel`. Expected: all pass.

- [ ] **Step 5: Commit** `feat(pin): hover bar view model (#225)`.

---

### Task 3: `PinHoverBar.qml` and `PinValuePanel.qml`

**Files:**
- Create: `src/qml/pin/PinHoverBar.qml`, `src/qml/pin/PinValuePanel.qml`
- Modify: `CMakeLists.txt`. Add both to the QML module file list and set `QT_RESOURCE_ALIAS pin/PinHoverBar.qml` / `pin/PinValuePanel.qml`, like `toolbar/ToolbarOverflowMenu.qml` at lines ~1070/~1227.
- Test: `tests/Qml/tst_PinHoverBarQml.cpp` (link like `Qml_ToolbarOverflow`)

**Interfaces:**
- Consumes: Task 2's `PinHoverBarViewModel`.
- Produces:
  - **QML roots:** `viewModel` (required property); `implicitWidth`/`implicitHeight`.
  - **objectNames:** `pinHoverZoom`, `pinHoverOpacity`, `pinHoverStatus`, `pinHoverAnnotate`, `pinHoverMore`, `pinValueSlider`, `pinValuePreset_<percent>`, `pinValueReset`.
  - **Root signals:** `buttonGeometry(string name, real x, real y, real w, real h)` for anchoring; a `focusButton(string)` function on the bar.

`PinHoverBar.qml` (sketch; use the existing component APIs: `ToolbarButton` has `textLabel`, `tooltipText`, `iconSource`, `clicked(int)`):

```qml
import QtQuick
import SnapTrayQml

GlassSurface {
    id: root
    required property var viewModel
    implicitWidth: row.implicitWidth + 12
    implicitHeight: 32
    Accessible.role: Accessible.ToolBar
    Accessible.name: qsTr("Pin controls")
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 2
        ToolbarButton {
            objectName: "pinHoverZoom"
            textLabel: qsTr("Zoom %1%").arg(root.viewModel.zoomPercent)
            tooltipText: qsTr("Zoom (Ctrl+= / Ctrl+- / Ctrl+0)")
            Accessible.name: textLabel
            onClicked: root.viewModel.openPanel(1)
        }
        ToolbarButton {
            objectName: "pinHoverOpacity"
            textLabel: qsTr("Opacity %1%").arg(root.viewModel.opacityPercent)
            tooltipText: qsTr("Opacity ([ / ])")
            Accessible.name: textLabel
            onClicked: root.viewModel.openPanel(2)
        }
        ToolbarButton {
            objectName: "pinHoverStatus"
            visible: !root.viewModel.compact && root.viewModel.status !== 0
            textLabel: root.viewModel.status === 2 ? qsTr("⏸ Paused") : qsTr("● Live")
            tooltipText: root.viewModel.status === 2 ? qsTr("Resume Live Update (P)") : qsTr("Pause Live Update (P)")
            Accessible.name: tooltipText
            onClicked: root.viewModel.toggleStatus()
        }
        ToolbarButton {
            objectName: "pinHoverAnnotate"
            visible: !root.viewModel.compact
            iconSource: "qrc:/icons/icons/pencil.svg"
            tooltipText: qsTr("Annotate (Space)")
            Accessible.name: qsTr("Annotate")
            onClicked: root.viewModel.requestAnnotate()
        }
        ToolbarButton {
            objectName: "pinHoverMore"
            iconSource: "qrc:/icons/icons/more.svg"
            tooltipText: qsTr("More")
            Accessible.name: tooltipText
            onClicked: root.viewModel.requestMore()
        }
    }
}
```

Use the icon paths the #224 More button and the Pencil tool already use. Find them with `grep -rn "more\.svg\|pencil" src/qml src/tools/ToolRegistry.cpp`. Keyboard navigation inside the bar (Left/Right/Enter) follows whatever `ToolbarButton` focus handling #224 added for `FloatingToolbar`. Reuse that, and add nothing new if it is already there.

`PinValuePanel.qml`:
- A `GlassSurface` with a title Text ("Zoom" or "Opacity").
- A `Slider`, `objectName: "pinValueSlider"`. Its range comes from the view model: zoom is `minZoomPercent`..500, opacity is 10..100. `onMoved` calls `requestZoomPercent` or `requestOpacityPercent`.
- A `Repeater` of preset buttons, `pinValuePreset_<p>`: [33, 50, 100, 200] or [50, 75, 100].
- A reset button, `pinValueReset`, labelled `qsTr("Reset to 100%")`.
- Every control has an `Accessible.name`.
- It shows `viewModel.panel` (1 or 2) and hides itself at 0.

- [ ] **Step 1: Failing tests** in `tst_PinHoverBarQml.cpp`. Load each file into a `QQuickView` with a real `PinHoverBarViewModel`, like `tst_ToolbarOverflow` loads `ToolbarOverflowMenu.qml`.

```cpp
void barShowsValuesAndNames();     // setValues(1.5,0.1,0.8): pinHoverZoom text "Zoom 150%", Accessible.name same
void compactHidesStatusAndAnnotate(); // setStatus(Live), setAvailableWidth(200) → pinHoverStatus & pinHoverAnnotate invisible, zoom/opacity/more visible
void buttonsRaiseRequests();       // click pinHoverZoom → panel()==ZoomPanel; click pinHoverMore → moreRequested
void zoomPanelPresetAndReset();    // panel=Zoom: click pinValuePreset_200 → zoomRequested(2.0); click pinValueReset → zoomRequested(1.0)
void opacityPanelRangeAndReset();  // panel=Opacity: slider from==10,to==100; pinValuePreset_50 → opacityRequested(0.5)
void panelControlsAreNamed();      // every slider/preset/reset has non-empty Accessible.name
```

- [ ] **Step 2: Run** `ninja Qml_PinHoverBarQml && ./bin/Qml_PinHoverBarQml`. Expected: FAIL, because the QML files do not exist (load error).

- [ ] **Step 3: Implement** the two QML files and their CMake registration.

- [ ] **Step 4: Run.** Expected: all pass, with no QML warnings (`QTest::failOnWarning(QRegularExpression(".*(ReferenceError|TypeError).*"))` in `init()`).

- [ ] **Step 5: Commit** `feat(pin): hover bar and value panel QML (#225)`.

---

### Task 4: `PinHoverBar` controller (windows, timers, positioning)

**Files:**
- Create: `include/pinwindow/PinHoverBar.h`, `src/pinwindow/PinHoverBar.mm` (`.mm` like `ToolbarOverflowPopup.mm`; plain C++)
- Test: `tests/PinWindow/tst_PinHoverBar.cpp`

**Interfaces:**
- Consumes: Tasks 2 and 3.
- Produces:

```cpp
class PinHoverBar : public QObject {
    Q_OBJECT
public:
    static constexpr int kShowDelayMs = 300;
    static constexpr int kHideDelayMs = 500;
    static constexpr int kBottomInset = 8;
    PinHoverBar(PinHoverBarViewModel* viewModel, QWindow* owner, QObject* parent = nullptr);
    ~PinHoverBar() override;
    void setPinGeometry(const QRect& contentGlobal);   // the pin's visible image rect, global; repositions
    void pointerEntered();                              // starts the show timer (if allowed)
    void pointerLeft();                                 // starts the hide timer
    void setSuppressed(bool suppressed);                // true → hide now (bar + panel), no timers
    void showNow();                                     // keyboard focus path; ignores the delay
    void hideNow();
    bool isBarVisible() const;
    bool isPanelVisible() const;
    QWindow* barWindow() const;
    QWindow* panelWindow() const;
    QRect barGeometry() const;
    bool containsGlobal(const QPoint& p) const;         // pin rect ∪ bar ∪ panel
    void setCursorPositionProvider(std::function<QPoint()> provider);  // tests
signals:
    void barShown();
    void barHidden();
};
```

**Rules:**
- **Show timer:** single-shot, `kShowDelayMs`. When it fires, it shows the bar only if the view model fits the pin width, nothing suppresses it, and the cursor is still inside `containsGlobal`.
- **Hide timer:** single-shot, `kHideDelayMs`. When it fires, it checks `containsGlobal(cursor)`. If the cursor is still inside (on the bar or panel), it restarts. Otherwise it hides the bar and the panel. **[RF 1]**
- **Bar position:** `x = pinRect.center().x() - width / 2`, `y = pinRect.bottom() - kBottomInset - height`. `setPinGeometry` repositions the bar, and the panel too if it is open. **[RF 2]**
- **Panel:** the bar listens to `viewModel->panelChanged`. When a panel opens, it shows the panel window anchored above the bar's matching button (from `buttonGeometry`), installs the qApp event filter (Esc closes; a press outside the panel and the bar closes) and closes again on `NoPanel`. **[RF 5]**
- **Windows:** the bar uses `QmlOverlayManager::createToolPanelWindow()`, which does not take focus; the panel uses `createPopupWindow()`. Both get `setTransientParent(owner)` and `applyShownOverlayWindowPolicy` on show. `hideNow` and the destructor remove the filter and close both windows. **[RF 4]**
- **Width:** `viewModel->setAvailableWidth(pinRect.width())` runs on every `setPinGeometry`. **[RF 3]**

- [ ] **Step 1: Failing tests** with a real view model and a cursor provider:

```cpp
void showsAfterDelayOnly();          // pointerEntered; at 100 ms not visible; QTRY within 1000 ms visible
void leavingOntoBarKeepsIt();        // shown; cursor provider returns barGeometry().center(); pointerLeft; wait 800 ms → still visible  [RF 1]
void leavingAwayHides();             // cursor far away; pointerLeft → hidden within 1000 ms, panel hidden too
void followsPin();                   // shown; setPinGeometry moved by (50,30) → barGeometry moved by (50,30)  [RF 2]
void tinyPinNeverShows();            // setPinGeometry width 100 → pointerEntered → stays hidden 600 ms  [RF 3]
void suppressedHidesImmediately();   // shown + panel open → setSuppressed(true) → both hidden now; pointerEntered does nothing
void panelEscReturnsFocus();         // open ZoomPanel → key Esc on panel window → panel hidden, viewModel.panel()==NoPanel  [RF 5]
void destroyWhileOpenIsClean();      // shown + panel; delete controller → no crash, qApp filter gone (a later key event does not crash)  [RF 4]
```

- [ ] **Step 2: Run** `ninja PinWindow_PinHoverBar && ./bin/PinWindow_PinHoverBar`. Expected: a compile error (no header).

- [ ] **Step 3: Implement** the rules.

- [ ] **Step 4: Run.** Expected: all pass.

- [ ] **Step 5: Commit** `feat(pin): hover bar windows and timing (#225)`.

---

### Task 5: Wire the bar into `PinWindow`; keyboard and badges

**Files:**
- Modify: `include/PinWindow.h`, `src/PinWindow.cpp`, `include/pinwindow/UIIndicators.h`, `src/pinwindow/UIIndicators.cpp`
- Test: `tests/PinWindow/tst_Transform.cpp` (keys, badges) and the new `tests/PinWindow/tst_PinHoverIntegration.cpp`

**Interfaces:**
- Consumes: Task 4's `PinHoverBar`, Task 2's view model and Task 1's setting.
- Produces:
  - `void PinWindow::resetZoom()`, `void PinWindow::resetOpacity()`, `void PinWindow::zoomByStep(int direction)`
  - `bool PinWindow::hoverBarSuppressed() const`
  - `UIIndicators::showZoomIndicator(qreal, bool liveCornerTaken)` (adds the second argument)

**Rules:**
- **Creation:** `PinWindow` creates its `PinHoverBarViewModel` and `PinHoverBar` lazily on the first `enterEvent` or `focusInEvent`.
- **Feeding the view model:**
  - `setValues(m_zoomLevel, minimumPinWindowZoom(transformedContentLogicalSize()), m_opacity)` runs after every zoom/opacity change.
  - `setStatus(...)` runs on Live start/stop/pause/resume.
  - `setPinGeometry(...)` runs on move, resize and `updateSize()`, using the content rect without the shadow margin, mapped to global.
- **Pointer and focus:**
  - `enterEvent` calls `pointerEntered()`; a new `leaveEvent` override calls `pointerLeft()`.
  - `focusInEvent` calls `showNow()`. `Tab` while the pin is focused and the bar is visible moves focus into the bar window: `barWindow()->requestActivate()`, then `focusButton("pinHoverZoom")`.
- **Suppression:** `hoverBarSuppressed()` is true when `m_isDragging || m_isResizing || (m_toolbar && m_toolbar->isVisible()) || isRegionLayoutMode() || m_clickThrough || crop tool active || !loadShowHoverControls()`. Every change to those states calls `m_hoverBar->setSuppressed(hoverBarSuppressed())`: where `m_isDragging` / `m_isResizing` flip, in `showToolbar`/`hideToolbar`, in `setClickThrough`, on region-layout enter/exit, on crop-tool select/exit, and in `hideEvent` (`hideNow`). **[RF 2, RF 4]**
- **Requests from the bar:**
  - `zoomRequested(z)` calls `setZoomLevel(z)` and shows the badge.
  - `opacityRequested(o)` calls `setOpacity(o)` and shows the badge.
  - `annotateRequested` calls `showToolbar()`, the same as Space.
  - `moreRequested` calls `m_contextMenu->popup(barGeometry().topLeft())`, after `createContextMenu()` if needed and the same refresh `contextMenuEvent` does. Extract that refresh into `refreshContextMenuState()` first, so it is shared.
  - `pauseRequested`/`resumeRequested` call `pauseLiveCapture`/`resumeLiveCapture`.
- **Keyboard** (`keyPressEvent`, before the existing digit/bracket handling):
  - `matches(QKeySequence::ZoomIn)`, or Ctrl+`=`, calls `zoomByStep(1)`.
  - `ZoomOut` calls `zoomByStep(-1)`.
  - Ctrl+`0` calls `resetZoom()`.
  - Ctrl+`]` calls `resetOpacity()`.
  - `zoomByStep` uses `loadZoomStep()` like the wheel path, anchored at the top-left like the wheel.
- **Badges:**
  - The text becomes `tr("Zoom %1%")` / `tr("Opacity %1%")`. The badge strings are built in `PinWindow` (translation context `PinWindow`) and passed in. Change the `UIIndicators` signatures to take the already-formatted text: `showZoomIndicator(const QString& text, bool topRight)` and `showOpacityIndicator(const QString& text)`.
  - `topRight` is `m_isLiveMode`.
  - Show a badge from every zoom/opacity change source. Move the badge call into `setZoomLevel` and `setOpacity`, guarded by a `bool showBadge = true` parameter so construction and restore do not flash. Remove the now-duplicate calls in the wheel and edge-resize paths.
- **Live dot tooltip:** `setToolTip(m_isLiveMode ? tr("Live Update (L to stop, P to pause)") : QString())`, refreshed on Live state changes.

- [ ] **Step 1: Failing tests**

In `tst_Transform.cpp`:

```cpp
void testKeyboardZoomAndReset();     // QTest::keyClick(&w, Qt::Key_Equal, Qt::ControlModifier) → zoom 1+step; Key_Minus back; Ctrl+0 → 1.0
void testOpacityReset();             // setOpacity(0.4); Ctrl+] → 1.0
void testBadgeNamesValueForEverySource();  // menu preset (zoom 200 action trigger), resetZoom, opacity step: badge text "Zoom 200%" / "Opacity 95%"
```

In `tst_PinHoverIntegration.cpp` (a `PinWindow` shown on screen, with the controller's cursor provider pointed at test positions):

```cpp
void hoverShowsBarWithValues();      // enter → bar visible within 1 s; vm zoomPercent==100
void barZoomRequestZoomsPin();       // vm->requestZoomPercent(200) → pin zoom 2.0, badge shown
void suppressedStates_data();        // dragging, toolbar shown, click-through, setting off, region layout
void suppressedStates();             // each → bar hidden and does not show on enter
void moreOpensContextMenu();         // vm->requestMore() → m_contextMenu->isVisible()
void closingPinClosesBar();          // bar+panel open → pin close → windows gone  [RF 4]
```

- [ ] **Step 2: Run** `ninja PinWindow_Transform PinWindow_PinHoverIntegration` and run both. Expected: compile errors for the missing methods, then assertion failures.

- [ ] **Step 3: Implement** the rules.

- [ ] **Step 4: Run** both suites plus `PinWindow_Resize` and `PinWindow_StyleSync` (shared key handling). Expected: all pass.

- [ ] **Step 5: Commit** `feat(pin): hover controls, zoom/opacity keys and named badges (#225)`.

---

### Task 6: Context menu grouped by task; reset items; shortcuts shown

**Files:**
- Modify: `src/PinWindow.cpp` (`createContextMenu`, lines ~1165–1422)
- Test: `tests/PinWindow/tst_ContextMenuTranslations.cpp`, or a new `tests/PinWindow/tst_ContextMenuLayout.cpp` if that file is translation-only

**Interfaces:**
- Consumes: Task 5's `resetZoom` / `resetOpacity`.
- Produces: the menu order from spec §3, and objectNames on top-level actions/menus: `pinMenuShowToolbar`, `pinMenuZoom`, `pinMenuOpacity`, `pinMenuCopy`, `pinMenuClickThrough`, `pinMenuLive`, `pinMenuMoveToScreen`, `pinMenuClose`, and so on. Tests read the structure through these names.

**Rules:**
- Reorder the existing blocks only. No behaviour change except:
  - the new "Reset to 100%" in Zoom and in the new top-level Opacity menu;
  - the current-value rows in both menus;
  - `setShortcut` hints: zoom in/out/reset (Ctrl+=, Ctrl+-, Ctrl+0), opacity reset (Ctrl+]), Live start/stop `L` and pause `P`.
- Separators between the five groups: View | Output | Modes | Pins | Close.
- The Opacity menu moves out of Image processing. Image processing keeps rotate, flip and crop.
- Move to Screen moves to the Pins group. Its show/hide logic in `refreshContextMenuState` is unchanged.

- [ ] **Step 1: Failing test**

```cpp
void testContextMenuGroups()
{
    PinWindow w(testPixmap(), QPoint(0, 0));
    w.createContextMenu();
    // Top-level item objectNames between separators, in order:
    const QList<QStringList> expected = {
        {"pinMenuShowToolbar", "pinMenuShowBorder", "pinMenuZoom", "pinMenuOpacity", "pinMenuImageProcessing"},
        {"pinMenuCopy", "pinMenuSave", "pinMenuWatermark", "pinMenuInfo", "pinMenuHistoryFolder"},
        {/* click-through if supported */ "pinMenuLive", "pinMenuLivePause", "pinMenuFrameRate", "pinMenuRegionLayout"},
        {"pinMenuMoveToScreen", "pinMenuMerge", "pinMenuCloseAll"},
        {"pinMenuClose"}};
    QCOMPARE(groupsOf(w.m_contextMenu), withPlatformItems(expected));
}
void testZoomAndOpacityResetItems();   // trigger pinMenuZoom→"Reset to 100%" after setZoomLevel(2) → 1.0; same for opacity
void testShortcutHintsShown();         // zoom reset action shortcut == Ctrl+0; live action shortcut == L
```

`groupsOf` walks `actions()`, splitting at separators and collecting `objectName()` (for submenus, `menuAction()->objectName()`). `withPlatformItems` inserts `"pinMenuClickThrough"` at the front of group 3 when `capabilities().supportsClickThrough`.

- [ ] **Step 2: Run.** Expected: FAIL, because the objectNames are empty and the order is different.

- [ ] **Step 3: Implement.** Move the code blocks and add `setObjectName` and the new items.

- [ ] **Step 4: Run** the menu tests and `PinWindow_ContextMenuTranslations`. Expected: all pass, and `testContextMenuDoesNotExposeBeautifyAction` still passes.

- [ ] **Step 5: Commit** `feat(pin): context menu grouped by task, with resets and shortcut hints (#225)`.

---

### Task 7: Click-through pill restyle and tray item

**Files:**
- Modify: `include/pinwindow/ClickThroughExitButton.h`, `src/pinwindow/ClickThroughExitButton.cpp`, `src/MainApplication.cpp` and `include/MainApplication.h` (tray), `include/PinWindowManager.h` and `src/PinWindowManager.cpp`
- Test: `tests/PinWindow/tst_Transform.cpp` (pill), `tests/App/tst_MainApplicationTrayMenu.cpp` (tray), `tests/PinWindow/tst_PinWindowManager*` (or Transform) for the manager helper

**Interfaces:**
- Produces:
  - `bool PinWindowManager::anyClickThrough() const`
  - `void PinWindowManager::disableClickThroughForAll()`
  - `ClickThroughExitButton::text()` returns `tr("Click-through · Turn off ✕")`, and its accessible name is `tr("Turn off click-through")`.

**Rules:**
- **The pill** keeps its window, position and timing logic. Only its painting changes: draw the background with `GlassRenderer` (the shared helper; see its use in existing QWidget floating panels) instead of the stylesheet, and use the new label. `setAccessibleName` is set.
- **The tray item** `m_disableClickThroughAction = m_trayMenu->addAction(tr("Turn Off Click-through for All Pins"))` goes after "Close All Pins". `m_trayMenu`'s `aboutToShow` sets its visibility to `m_pinWindowManager->anyClickThrough()`. Triggering it calls `disableClickThroughForAll()`, which calls `setClickThrough(false)` on each pin.

- [ ] **Step 1: Failing tests**

```cpp
// tst_Transform.cpp
void testClickThroughPillTextAndName();   // setClickThrough(true) → indicator's button text contains "Turn off", accessibleName non-empty
// tst_MainApplicationTrayMenu.cpp (follow its existing fixture)
void trayDisableClickThroughItem();       // no pins → item hidden on aboutToShow; one pin with click-through → visible; trigger → pin.isClickThrough()==false
```

- [ ] **Step 2: Run.** Expected: FAIL (the text differs; the tray action is missing).

- [ ] **Step 3: Implement.** On Linux, `supportsClickThrough` is false, so `anyClickThrough()` stays false and the item stays hidden.

- [ ] **Step 4: Run** both suites. Expected: all pass. Existing click-through tests (`testSetClickThrough`, `testClickThroughExitAfterShowAllPins`) still pass.

- [ ] **Step 5: Commit** `feat(pin): clearer click-through pill and tray exit (#225)`.

---

### Task 8: Translations, docs, evidence, full suite

**Files:**
- Modify: `translations/*.ts` (24); `docs/docs/pin-window.md`; `docs/zh-tw/docs/pin-window.md`; `docs/developer/ui-design-guidelines.md`; `src/qml/PinToolbarViewModel.cpp` (the "Done (Space/Esc)" tooltip through `QCoreApplication::translate("PinWindow", …)`)
- Create: `tests/Qml/tst_PinWindowEvidence.cpp`, gated by `SNAPTRAY_PIN_DESIGN_DIR`, `SNAPTRAY_PIN_TRANSLATION` and `SNAPTRAY_PIN_VIDEO_DIR`. Screenshots go to `docs/assets/images/developer/pin/`, the video to `docs/assets/images/developer/pin/videos/`.

- [ ] **Step 1: Translations.**
  - Run `cmake --build build --target update_translations`.
  - Fill every new `<message>` in all 24 `.ts` files. Use the #224 `apply.py` approach: a JSON of translations per string, applied by script, and vanished entries left unmarked.
  - Check with `grep -c 'type="unfinished"' translations/*.ts` that no new string is unfinished.
- [ ] **Step 2: Docs.**
  - Update the user docs: the hover bar, the panels, the resets, the keys, and the click-through exits. Replace the "pencil icon" sentence with Space / Annotate.
  - Add a "Pin window" section to the guidelines: contract, acceptance matrix (macOS evidence; Windows and Ubuntu pending; physical DPI pending; screen readers pending) and regeneration commands.
- [ ] **Step 3: Evidence.**
  - Screenshots:
    - the bar, light and dark;
    - the bar in German;
    - the zoom panel;
    - a compact pin;
    - the Live chip;
    - the click-through pill.
  - Video: hover → zoom panel slider → preset → reset → opacity panel → More → leave.
  - Encode with the #224 ffmpeg commands and check the frames on a contact sheet.
- [ ] **Step 4: Full suite.** Run `./scripts/run-tests.sh`. Expected: `100% tests passed`.
- [ ] **Step 5: Commit** `docs(pin): hover controls, translations and evidence (#225)`.

The Windows, Ubuntu X11 and screen-reader checks for pins go into #238 as a comment when the PR opens, with the user's go-ahead.
