# Brush size affordance + independent mosaic brush size — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the annotation sub-toolbar's size control visibly adjustable, and give the mosaic tool a persisted brush size independent of the shared line width.

**Architecture:** Three layers, bottom-up. (1) A `WidthSlot` lookup routes each `ToolId` to either the shared stroke width or the mosaic brush width, consumed by both `AnnotationSettingsManager` and `ToolManager`. (2) `WidthSection.qml` grows a value readout and an up/down stepper; the mouse wheel path is untouched. (3) The tooltip machinery that is currently duplicated in two toolbar classes is extracted into one controller and reused by the sub-toolbar.

**Tech Stack:** C++17, Qt 6.10.1, QML (Qt Quick), Objective-C++ on macOS, Qt Test, CMake + Ninja.

**Design doc:** [`docs/superpowers/specs/2026-07-30-mosaic-brush-size-affordance-design.md`](../specs/2026-07-30-mosaic-brush-size-affordance-design.md)

## Global Constraints

- Width range stays **1–30**. `PinToolOptionsViewModel::minWidth()` returns 1, `maxWidth()` returns 30. Do not widen the range — larger brushes need an on-canvas ring, which is out of scope.
- **Do not modify the wheel path.** `PinToolOptionsViewModel::handleWidthWheelDelta`, the `MouseArea` at `src/qml/toolbar/ToolOptionsStrip.qml:62`, and `RegionSelector::wheelEvent` must behave exactly as before.
- Mosaic brush default is **18**. `MosaicStroke` doubles it (`stroker.setWidth(m_width * 2)`), giving a 36 px footprint.
- Settings access goes through `AnnotationSettingsManager`. No direct `QSettings` in feature code.
- Prefer lookup tables over repeated `switch`/`if` chains on `ToolId` (project rule: tool behavior is data-driven).
- User-visible strings use `tr()` and must be added to all 24 files in `TS_FILES` (`CMakeLists.txt:1284`).
- `.mm` sources go in `SNAPTRAY_QML_NATIVE_SOURCES` (`CMakeLists.txt:998`); the existing `if(NOT APPLE) … LANGUAGE CXX` rule at line 1011 then covers them.
- Build: `./scripts/build.sh`. Tests: `./scripts/run-tests.sh`. On Windows use the `.bat` equivalents.

---

### Task 1: Width slot lookup + mosaic brush size setting

Adds the routing table and the persisted mosaic size. Pure logic, no UI — fully unit tested.

**Files:**
- Create: `include/tools/ToolWidthSlot.h`
- Create: `src/tools/ToolWidthSlot.cpp`
- Modify: `include/settings/AnnotationSettingsManager.h`
- Modify: `src/settings/AnnotationSettingsManager.cpp`
- Modify: `CMakeLists.txt:325` (add `src/tools/ToolWidthSlot.cpp` after `ToolSectionConfig.cpp`)
- Test: `tests/Tools/tst_ToolWidthSlot.cpp` (new), `tests/CMakeLists.txt`
- Test: `tests/Settings/tst_AnnotationSettingsManager.cpp`

**Interfaces:**
- Consumes: `ToolId` from `include/tools/ToolId.h`.
- Produces:
  - `enum class WidthSlot { Stroke, MosaicBrush };`
  - `WidthSlot widthSlotForTool(ToolId toolId);`
  - `int AnnotationSettingsManager::loadWidthForTool(ToolId) const;`
  - `void AnnotationSettingsManager::saveWidthForTool(ToolId, int width);`
  - `static constexpr int AnnotationSettingsManager::kDefaultMosaicBrushSize = 18;`

- [ ] **Step 1: Write the failing slot-lookup test**

Create `tests/Tools/tst_ToolWidthSlot.cpp`:

```cpp
#include <QtTest/QtTest>

#include "tools/ToolWidthSlot.h"

class tst_ToolWidthSlot : public QObject
{
    Q_OBJECT

private slots:
    void testMosaicUsesItsOwnSlot();
    void testStrokeToolsShareOneSlot();
    void testUnlistedToolsDefaultToStroke();
};

void tst_ToolWidthSlot::testMosaicUsesItsOwnSlot()
{
    QCOMPARE(widthSlotForTool(ToolId::Mosaic), WidthSlot::MosaicBrush);
}

void tst_ToolWidthSlot::testStrokeToolsShareOneSlot()
{
    QCOMPARE(widthSlotForTool(ToolId::Pencil), WidthSlot::Stroke);
    QCOMPARE(widthSlotForTool(ToolId::Arrow), WidthSlot::Stroke);
    QCOMPARE(widthSlotForTool(ToolId::Shape), WidthSlot::Stroke);
    QCOMPARE(widthSlotForTool(ToolId::Polyline), WidthSlot::Stroke);
}

void tst_ToolWidthSlot::testUnlistedToolsDefaultToStroke()
{
    QCOMPARE(widthSlotForTool(ToolId::Marker), WidthSlot::Stroke);
    QCOMPARE(widthSlotForTool(ToolId::Copy), WidthSlot::Stroke);
}

QTEST_APPLESS_MAIN(tst_ToolWidthSlot)
#include "tst_ToolWidthSlot.moc"
```

Register it in `tests/CMakeLists.txt` next to the other `Tools_*` entries (near line 775):

```cmake
add_executable(Tools_ToolWidthSlot Tools/tst_ToolWidthSlot.cpp)
target_link_libraries(Tools_ToolWidthSlot PRIVATE snaptray_ui Qt6::Test)
add_test(NAME Tools_ToolWidthSlot COMMAND Tools_ToolWidthSlot)
set_tests_properties(Tools_ToolWidthSlot PROPERTIES TIMEOUT 60 LABELS "unit")
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `./scripts/build.sh`
Expected: build FAILS with `fatal error: 'tools/ToolWidthSlot.h' file not found`.

- [ ] **Step 3: Implement the slot lookup**

Create `include/tools/ToolWidthSlot.h`:

```cpp
#ifndef TOOLWIDTHSLOT_H
#define TOOLWIDTHSLOT_H

#include "ToolId.h"

/**
 * @brief Which stored width a tool reads and writes.
 *
 * Mosaic paints an area, not a line, so it keeps its own size. Every other
 * width-capable tool shares one stroke width, matching the single width
 * control in the sub-toolbar.
 */
enum class WidthSlot {
    Stroke,
    MosaicBrush,
};

/**
 * @brief Map a tool to its width slot. Tools without an entry use Stroke.
 */
WidthSlot widthSlotForTool(ToolId toolId);

#endif // TOOLWIDTHSLOT_H
```

Create `src/tools/ToolWidthSlot.cpp`:

```cpp
#include "tools/ToolWidthSlot.h"

#include <map>

namespace {

// Tools whose width is stored separately from the shared stroke width.
const std::map<ToolId, WidthSlot> kWidthSlotOverrides = {
    {ToolId::Mosaic, WidthSlot::MosaicBrush},
};

} // anonymous namespace

WidthSlot widthSlotForTool(ToolId toolId)
{
    auto it = kWidthSlotOverrides.find(toolId);
    return it != kWidthSlotOverrides.end() ? it->second : WidthSlot::Stroke;
}
```

Add to `CMakeLists.txt` immediately after line 325 (`src/tools/ToolSectionConfig.cpp`):

```cmake
    src/tools/ToolWidthSlot.cpp
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `./scripts/build.sh && ctest --test-dir build -R Tools_ToolWidthSlot --output-on-failure`
Expected: PASS, 3 tests.

- [ ] **Step 5: Write the failing settings test**

Add to the `private slots:` block in `tests/Settings/tst_AnnotationSettingsManager.cpp`, after the existing width tests (near line 41):

```cpp
    // Per-tool width tests
    void testLoadMosaicBrushSize_DefaultValue();
    void testSaveLoadWidthForTool_MosaicIsIndependentOfStroke();
    void testLoadWidthForTool_ClampsOutOfRangeStoredValue();
```

Add the implementations at the end of the file, before `QTEST_MAIN`:

```cpp
void tst_AnnotationSettingsManager::testLoadMosaicBrushSize_DefaultValue()
{
    QCOMPARE(AnnotationSettingsManager::instance().loadWidthForTool(ToolId::Mosaic),
             AnnotationSettingsManager::kDefaultMosaicBrushSize);
    QCOMPARE(AnnotationSettingsManager::kDefaultMosaicBrushSize, 18);
}

void tst_AnnotationSettingsManager::testSaveLoadWidthForTool_MosaicIsIndependentOfStroke()
{
    auto& manager = AnnotationSettingsManager::instance();

    manager.saveWidthForTool(ToolId::Pencil, 3);
    manager.saveWidthForTool(ToolId::Mosaic, 24);

    QCOMPARE(manager.loadWidthForTool(ToolId::Pencil), 3);
    QCOMPARE(manager.loadWidthForTool(ToolId::Arrow), 3);
    QCOMPARE(manager.loadWidthForTool(ToolId::Mosaic), 24);

    manager.saveWidthForTool(ToolId::Arrow, 7);

    QCOMPARE(manager.loadWidthForTool(ToolId::Pencil), 7);
    QCOMPARE(manager.loadWidthForTool(ToolId::Mosaic), 24);
}

void tst_AnnotationSettingsManager::testLoadWidthForTool_ClampsOutOfRangeStoredValue()
{
    auto settings = SnapTray::getSettings();
    settings.setValue("mosaicBrushSize", 999);
    QCOMPARE(AnnotationSettingsManager::instance().loadWidthForTool(ToolId::Mosaic), 30);

    settings.setValue("mosaicBrushSize", 0);
    QCOMPARE(AnnotationSettingsManager::instance().loadWidthForTool(ToolId::Mosaic), 1);

    settings.setValue("annotationWidth", -5);
    QCOMPARE(AnnotationSettingsManager::instance().loadWidthForTool(ToolId::Pencil), 1);
}
```

Add `settings.remove("mosaicBrushSize");` to `clearAllTestSettings()` (near line 96), and add `#include "tools/ToolId.h"` to the includes at the top.

- [ ] **Step 6: Run the settings test to verify it fails**

Run: `./scripts/build.sh`
Expected: build FAILS with `no member named 'loadWidthForTool' in 'AnnotationSettingsManager'`.

- [ ] **Step 7: Implement the per-tool width settings**

In `include/settings/AnnotationSettingsManager.h`, add `#include "tools/ToolWidthSlot.h"` to the includes, then after the existing width settings block (line 28):

```cpp
    // Per-tool width settings (routes through WidthSlot)
    int loadWidthForTool(ToolId toolId) const;
    void saveWidthForTool(ToolId toolId, int width);
```

Add to the default values block (after line 58):

```cpp
    static constexpr int kDefaultMosaicBrushSize = 18;
    static constexpr int kMinWidth = 1;
    static constexpr int kMaxWidth = 30;
```

Add to the private keys block (after line 76):

```cpp
    static constexpr const char* kSettingsKeyMosaicBrushSize = "mosaicBrushSize";
```

In `src/settings/AnnotationSettingsManager.cpp`, add `#include <QtGlobal>` and append after `saveWidth` (line 46):

```cpp
int AnnotationSettingsManager::loadWidthForTool(ToolId toolId) const
{
    auto settings = SnapTray::getSettings();
    const bool isMosaic = widthSlotForTool(toolId) == WidthSlot::MosaicBrush;
    const int stored = isMosaic
        ? settings.value(kSettingsKeyMosaicBrushSize, kDefaultMosaicBrushSize).toInt()
        : settings.value(kSettingsKeyWidth, kDefaultWidth).toInt();

    return qBound(kMinWidth, stored, kMaxWidth);
}

void AnnotationSettingsManager::saveWidthForTool(ToolId toolId, int width)
{
    auto settings = SnapTray::getSettings();
    const int clamped = qBound(kMinWidth, width, kMaxWidth);
    if (widthSlotForTool(toolId) == WidthSlot::MosaicBrush) {
        settings.setValue(kSettingsKeyMosaicBrushSize, clamped);
    } else {
        settings.setValue(kSettingsKeyWidth, clamped);
    }
}
```

Leave the existing `loadWidth()` / `saveWidth()` in place — Task 3 removes their last callers.

- [ ] **Step 8: Run the tests to verify they pass**

Run: `./scripts/build.sh && ctest --test-dir build -R "Tools_ToolWidthSlot|Settings_AnnotationSettingsManager" --output-on-failure`
Expected: PASS.

- [ ] **Step 9: Commit**

```bash
git add include/tools/ToolWidthSlot.h src/tools/ToolWidthSlot.cpp \
        include/settings/AnnotationSettingsManager.h src/settings/AnnotationSettingsManager.cpp \
        CMakeLists.txt tests/CMakeLists.txt \
        tests/Tools/tst_ToolWidthSlot.cpp tests/Settings/tst_AnnotationSettingsManager.cpp
git commit -m "feat(tools): add per-tool width slots and mosaic brush size setting"
```

---

### Task 2: Route mosaic width through `ToolContext`

`MosaicToolHandler` stops reading the shared stroke width.

**Files:**
- Modify: `include/tools/ToolContext.h:39` (add `mosaicWidth` next to `width`)
- Modify: `include/tools/ToolManager.h:127-128`
- Modify: `src/tools/ToolManager.cpp:327-329`
- Modify: `src/tools/handlers/MosaicToolHandler.cpp:16`
- Modify: `include/tools/handlers/MosaicToolHandler.h:18` (remove dead constant)
- Test: `tests/Tools/tst_ToolManagerWidthRouting.cpp` (new), `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `widthSlotForTool(ToolId)` and `WidthSlot` from Task 1.
- Produces: `ToolContext::mosaicWidth`; `ToolManager::setWidth(int)` and `ToolManager::width()` now operate on the active tool's slot.

- [ ] **Step 1: Write the failing routing test**

Create `tests/Tools/tst_ToolManagerWidthRouting.cpp`:

```cpp
#include <QtTest/QtTest>

#include "tools/ToolManager.h"

class tst_ToolManagerWidthRouting : public QObject
{
    Q_OBJECT

private slots:
    void testWidthIsPerSlotAndSurvivesToolSwitching();
    void testMosaicDefaultIsIndependentOfStrokeDefault();
};

void tst_ToolManagerWidthRouting::testWidthIsPerSlotAndSurvivesToolSwitching()
{
    ToolManager manager;

    manager.setCurrentTool(ToolId::Pencil);
    manager.setWidth(3);
    QCOMPARE(manager.width(), 3);

    manager.setCurrentTool(ToolId::Mosaic);
    manager.setWidth(24);
    QCOMPARE(manager.width(), 24);

    manager.setCurrentTool(ToolId::Pencil);
    QCOMPARE(manager.width(), 3);

    manager.setCurrentTool(ToolId::Arrow);
    QCOMPARE(manager.width(), 3);

    manager.setCurrentTool(ToolId::Mosaic);
    QCOMPARE(manager.width(), 24);
}

void tst_ToolManagerWidthRouting::testMosaicDefaultIsIndependentOfStrokeDefault()
{
    ToolManager manager;

    manager.setCurrentTool(ToolId::Mosaic);
    QCOMPARE(manager.width(), 18);

    manager.setCurrentTool(ToolId::Pencil);
    QCOMPARE(manager.width(), 3);
}

QTEST_MAIN(tst_ToolManagerWidthRouting)
#include "tst_ToolManagerWidthRouting.moc"
```

`ToolManager`'s constructor is `explicit ToolManager(QObject* parent = nullptr)` — no annotation layer is needed for these assertions, matching `tests/Tools/tst_ToolManagerEscape.cpp`.

Register in `tests/CMakeLists.txt`:

```cmake
add_executable(Tools_ToolManagerWidthRouting Tools/tst_ToolManagerWidthRouting.cpp)
target_link_libraries(Tools_ToolManagerWidthRouting PRIVATE snaptray_ui Qt6::Test)
add_test(NAME Tools_ToolManagerWidthRouting COMMAND Tools_ToolManagerWidthRouting)
set_tests_properties(Tools_ToolManagerWidthRouting PROPERTIES TIMEOUT 60 LABELS "unit")
```

Check `ToolManager`'s constructor signature in `include/tools/ToolManager.h` before running; if it does not take an `AnnotationLayer*`, match the form used by `tests/Tools/tst_ToolManagerEscape.cpp`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `./scripts/build.sh && ctest --test-dir build -R Tools_ToolManagerWidthRouting --output-on-failure`
Expected: FAIL — `manager.width()` returns 24 for Pencil, because both tools share `m_context->width`.

- [ ] **Step 3: Add the mosaic slot to `ToolContext`**

In `include/tools/ToolContext.h`, replace line 39:

```cpp
    int width = 3;
```

with:

```cpp
    int width = 3;          // Shared stroke width (pencil, arrow, shape, polyline)
    int mosaicWidth = 18;   // Mosaic brush footprint; MosaicStroke doubles this
```

- [ ] **Step 4: Route `ToolManager` through the slot**

In `include/tools/ToolManager.h`, replace the inline `width()` accessor (line 128):

```cpp
    void setWidth(int width);
    int width() const;
```

In `src/tools/ToolManager.cpp`, add `#include "tools/ToolWidthSlot.h"` and replace `setWidth` (lines 327-329):

```cpp
void ToolManager::setWidth(int width) {
    if (widthSlotForTool(currentTool()) == WidthSlot::MosaicBrush) {
        m_context->mosaicWidth = width;
    } else {
        m_context->width = width;
    }
}

int ToolManager::width() const {
    return widthSlotForTool(currentTool()) == WidthSlot::MosaicBrush
        ? m_context->mosaicWidth
        : m_context->width;
}
```

- [ ] **Step 5: Point `MosaicToolHandler` at the mosaic slot**

In `src/tools/handlers/MosaicToolHandler.cpp`, replace line 16:

```cpp
    int brushWidth = ctx->width > 0 ? ctx->width : kDefaultBrushWidth;
```

with:

```cpp
    const int brushWidth = ctx->mosaicWidth;
```

In `include/tools/handlers/MosaicToolHandler.h`, delete the now-unused `kDefaultBrushWidth` constant (line 18) and change the member default on line 57 to reference the settings default instead:

```cpp
    int m_brushWidth = 18;
```

`kDefaultBlockSize` stays.

- [ ] **Step 6: Run the tests to verify they pass**

Run: `./scripts/build.sh && ctest --test-dir build -R "Tools_" --output-on-failure`
Expected: PASS, including the pre-existing `Tools_MosaicToolHandler`.

- [ ] **Step 7: Commit**

```bash
git add include/tools/ToolContext.h include/tools/ToolManager.h src/tools/ToolManager.cpp \
        include/tools/handlers/MosaicToolHandler.h src/tools/handlers/MosaicToolHandler.cpp \
        tests/CMakeLists.txt tests/Tools/tst_ToolManagerWidthRouting.cpp
git commit -m "feat(tools): give mosaic its own brush width in ToolContext"
```

---

### Task 3: Host wiring — load and save per tool

The three annotation surfaces persist and restore the right slot.

**Files:**
- Modify: `src/RegionSelector.cpp:358`, `src/RegionSelector.cpp:1224-1237`
- Modify: `src/ScreenCanvasSession.cpp:241`, `src/ScreenCanvasSession.cpp:2075`
- Modify: `src/PinWindow.cpp:3156`, `src/PinWindow.cpp:4310`
- Modify: `src/settings/AnnotationSettingsManager.cpp`, `include/settings/AnnotationSettingsManager.h` (remove `loadWidth`/`saveWidth`)
- Test: `tests/Settings/tst_AnnotationSettingsManager.cpp` (drop the old-API tests)

**Interfaces:**
- Consumes: `loadWidthForTool` / `saveWidthForTool` from Task 1; `ToolManager::setWidth` from Task 2.
- Produces: nothing new. This task removes the last callers of the tool-agnostic width API.

- [ ] **Step 1: Replace the width save path in `RegionSelector`**

`RegionSelector::onLineWidthChanged` (around line 1224) currently branches on mosaic but does the same thing in both arms apart from the cursor refresh. Replace the whole body:

```cpp
{
    m_inputState.annotationWidth = width;
    AnnotationSettingsManager::instance().saveWidthForTool(m_inputState.currentTool, width);
    if (m_inputState.currentTool == ToolId::Mosaic) {
        // Brush cursor previews the footprint, so it must follow the new size.
        setToolCursor();
    }
    m_toolManager->setWidth(width);
    requestCaptureSceneUpdate();
}
```

At line 358, seed from the tool that is actually selected:

```cpp
    m_inputState.annotationWidth = settings.loadWidthForTool(m_inputState.currentTool);
```

- [ ] **Step 2: Seed the ViewModel on tool change in `RegionSelector`**

At `src/RegionSelector.cpp:3888`, immediately after
`m_qmlSubToolbar->showForTool(static_cast<int>(m_inputState.currentTool));`, add:

```cpp
    const int toolWidth =
        AnnotationSettingsManager::instance().loadWidthForTool(m_inputState.currentTool);
    m_inputState.annotationWidth = toolWidth;
    m_toolManager->setWidth(toolWidth);
    m_toolOptionsViewModel->setCurrentWidth(toolWidth);
    setToolCursor();
```

Use `setCurrentWidth`, not `handleWidthChanged` — the latter emits `widthValueChanged`, which would loop straight back into `onLineWidthChanged` and re-save.

- [ ] **Step 3: Apply the same change to `ScreenCanvasSession`**

At `src/ScreenCanvasSession.cpp:241`:

```cpp
    const int savedWidth = annotationSettings.loadWidthForTool(m_currentToolId);
```

Replace `ScreenCanvasSession::onLineWidthChanged` (line 2071):

```cpp
void ScreenCanvasSession::onLineWidthChanged(int width)
{
    m_toolManager->setWidth(width);
    m_laserRenderer->setWidth(width);
    AnnotationSettingsManager::instance().saveWidthForTool(m_currentToolId, width);
    updateAllSurfaces();
}
```

At line 2203, immediately after
`m_qmlSubToolbar->showForTool(static_cast<int>(m_currentToolId));`, add:

```cpp
    const int toolWidth =
        AnnotationSettingsManager::instance().loadWidthForTool(m_currentToolId);
    m_toolManager->setWidth(toolWidth);
    m_toolOptionsViewModel->setCurrentWidth(toolWidth);
```

`ScreenCanvas` has no mosaic tool (`ToolRegistry.cpp:434`), so every lookup resolves to
`WidthSlot::Stroke` and behaviour is unchanged — make the change anyway so all three
surfaces share one code path.

- [ ] **Step 4: Apply the same change to `PinWindow`**

At `src/PinWindow.cpp:3156`:

```cpp
    m_annotationWidth = annotationSettings.loadWidthForTool(m_currentToolId);
```

Replace the save call in `PinWindow::onWidthChanged` (line 4310):

```cpp
    AnnotationSettingsManager::instance().saveWidthForTool(m_currentToolId, width);
```

At line 3603, immediately after `m_subToolbar->showForTool(toolId);`, add:

```cpp
    const int toolWidth = AnnotationSettingsManager::instance().loadWidthForTool(
        static_cast<ToolId>(toolId));
    m_annotationWidth = toolWidth;
    if (m_toolManager) {
        m_toolManager->setWidth(toolWidth);
    }
    if (auto* optionsVM = m_subToolbar->viewModel()) {
        optionsVM->setCurrentWidth(toolWidth);
    }
    updateCursorForTool();
```

Note `PinWindow` names the member `m_subToolbar` (not `m_qmlSubToolbar`) and reaches the
ViewModel through `m_subToolbar->viewModel()`.

- [ ] **Step 5: Remove the superseded API**

Delete `loadWidth()` / `saveWidth()` from `include/settings/AnnotationSettingsManager.h` (lines 27-28) and their definitions in `src/settings/AnnotationSettingsManager.cpp` (lines 36-46). Delete `testLoadWidth_DefaultValue`, `testSaveLoadWidth_Roundtrip`, and `testSaveLoadWidth_BoundaryValues` from `tests/Settings/tst_AnnotationSettingsManager.cpp` — the `loadWidthForTool` tests from Task 1 cover the same ground.

- [ ] **Step 6: Verify no callers remain**

Run: `grep -rn "loadWidth()\|saveWidth(" src include tests`
Expected: no output.

- [ ] **Step 7: Build and run the full suite**

Run: `./scripts/build.sh && ./scripts/run-tests.sh`
Expected: all tests pass.

- [ ] **Step 8: Manual check**

Launch with `./scripts/build-and-run.sh`. Capture a region, pick pencil, set width 3, switch to mosaic — the dot must jump to 18 and the brush cursor must be visibly larger. Switch back to pencil — it must return to 3. Quit and relaunch; both values persist.

- [ ] **Step 9: Commit**

```bash
git add src/RegionSelector.cpp src/ScreenCanvasSession.cpp src/PinWindow.cpp \
        include/settings/AnnotationSettingsManager.h src/settings/AnnotationSettingsManager.cpp \
        tests/Settings/tst_AnnotationSettingsManager.cpp
git commit -m "feat: persist mosaic brush size separately from stroke width"
```

---

### Task 4: `WidthSection` stepper UI

The visible affordance. QML only — `PinToolOptionsViewModel::handleWidthChanged` already clamps and emits, so no C++ change is needed.

**Files:**
- Modify: `src/qml/toolbar/WidthSection.qml`
- Test: `tests/Qml/tst_WidthSectionQml.cpp`

**Interfaces:**
- Consumes: `viewModel.currentWidth`, `viewModel.minWidth`, `viewModel.maxWidth`, `viewModel.handleWidthChanged(int)`.
- Produces: named QML items `widthPreviewContainer`, `widthPreviewDot` (both pre-existing), plus new `widthValueLabel`, `widthStepUp`, `widthStepDown`.

- [ ] **Step 1: Write the failing stepper test**

Add to `tests/Qml/tst_WidthSectionQml.cpp`. First extend `FakeWidthViewModel` with the slot QML will call:

```cpp
public slots:
    void handleWidthChanged(int width)
    {
        setCurrentWidth(qBound(minWidth(), width, maxWidth()));
    }
```

Then add the declarations and bodies:

```cpp
private slots:
    void testPreviewDotCenterStaysFixedAcrossWidthChanges();
    void testStepperButtonsChangeWidthByOne();
    void testStepperDimsAtBounds();

void tst_WidthSectionQml::testStepperButtonsChangeWidthByOne()
{
    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/SnapTrayQml/toolbar/WidthSection.qml")));
    QVERIFY2(component.status() == QQmlComponent::Ready, qPrintable(component.errorString()));

    FakeWidthViewModel viewModel;
    viewModel.setCurrentWidth(10);
    const QScopedPointer<QObject> created(component.create());
    auto* rootItem = qobject_cast<QQuickItem*>(created.get());
    QVERIFY(rootItem);
    QVERIFY(rootItem->setProperty("viewModel", QVariant::fromValue(static_cast<QObject*>(&viewModel))));

    auto* stepUp = rootItem->findChild<QQuickItem*>(QStringLiteral("widthStepUp"));
    auto* stepDown = rootItem->findChild<QQuickItem*>(QStringLiteral("widthStepDown"));
    QVERIFY(stepUp);
    QVERIFY(stepDown);

    QMetaObject::invokeMethod(stepUp, "activate");
    QCOMPARE(viewModel.currentWidth(), 11);

    QMetaObject::invokeMethod(stepDown, "activate");
    QMetaObject::invokeMethod(stepDown, "activate");
    QCOMPARE(viewModel.currentWidth(), 9);
}

void tst_WidthSectionQml::testStepperDimsAtBounds()
{
    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/SnapTrayQml/toolbar/WidthSection.qml")));
    QVERIFY2(component.status() == QQmlComponent::Ready, qPrintable(component.errorString()));

    FakeWidthViewModel viewModel;
    const QScopedPointer<QObject> created(component.create());
    auto* rootItem = qobject_cast<QQuickItem*>(created.get());
    QVERIFY(rootItem);
    QVERIFY(rootItem->setProperty("viewModel", QVariant::fromValue(static_cast<QObject*>(&viewModel))));

    auto* stepUp = rootItem->findChild<QQuickItem*>(QStringLiteral("widthStepUp"));
    auto* stepDown = rootItem->findChild<QQuickItem*>(QStringLiteral("widthStepDown"));
    QVERIFY(stepUp);
    QVERIFY(stepDown);

    viewModel.setCurrentWidth(1);
    QCoreApplication::processEvents();
    QVERIFY(!stepDown->property("stepEnabled").toBool());
    QVERIFY(stepUp->property("stepEnabled").toBool());

    QMetaObject::invokeMethod(stepDown, "activate");
    QCOMPARE(viewModel.currentWidth(), 1);

    viewModel.setCurrentWidth(30);
    QCoreApplication::processEvents();
    QVERIFY(!stepUp->property("stepEnabled").toBool());
    QVERIFY(stepDown->property("stepEnabled").toBool());

    QMetaObject::invokeMethod(stepUp, "activate");
    QCOMPARE(viewModel.currentWidth(), 30);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `./scripts/build.sh && ctest --test-dir build -R Qml_WidthSectionQml --output-on-failure`
Expected: FAIL — `QVERIFY(stepUp)` fails, no such child.

- [ ] **Step 3: Implement the stepper**

Replace `src/qml/toolbar/WidthSection.qml` with:

```qml
import QtQuick
import SnapTrayQml

/**
 * WidthSection: Stroke width preview, value readout, and up/down stepper.
 *
 * The preview dot scales with the current width. The stepper makes the value
 * visibly adjustable; the mouse wheel (handled by ToolOptionsStrip) still works
 * and is the fine-adjustment path.
 */
Item {
    id: root
    property var viewModel: null
    readonly property bool hasViewModel: root.viewModel !== null && root.viewModel !== undefined
    readonly property int currentWidthValue: root.hasViewModel ? root.viewModel.currentWidth : 1
    readonly property int minWidthValue: root.hasViewModel ? root.viewModel.minWidth : 1
    readonly property int maxWidthValue: root.hasViewModel ? root.viewModel.maxWidth : 1

    readonly property int repeatDelayMs: 400
    readonly property int repeatIntervalMs: 60

    implicitWidth: 48
    implicitHeight: 28
    width: implicitWidth
    height: implicitHeight

    function stepBy(delta) {
        if (!root.hasViewModel)
            return
        var next = root.currentWidthValue + delta
        if (next < root.minWidthValue || next > root.maxWidthValue)
            return
        root.viewModel.handleWidthChanged(next)
    }

    component StepButton: Item {
        id: stepButton

        required property int delta
        required property bool stepEnabled

        signal activated()

        function activate() {
            if (!stepButton.stepEnabled)
                return
            root.stepBy(stepButton.delta)
            stepButton.activated()
        }

        width: 10
        height: 9
        opacity: stepButton.stepEnabled ? (stepMouse.containsMouse ? 1.0 : 0.55) : 0.25

        ToolbarChevron {
            anchors.centerIn: parent
            rotation: stepButton.delta > 0 ? 180 : 0
        }

        MouseArea {
            id: stepMouse
            anchors.fill: parent
            hoverEnabled: true
            enabled: stepButton.stepEnabled
            onPressed: {
                stepButton.activate()
                repeatTimer.interval = root.repeatDelayMs
                repeatTimer.restart()
            }
            onReleased: repeatTimer.stop()
            onCanceled: repeatTimer.stop()
            onExited: repeatTimer.stop()
        }

        Timer {
            id: repeatTimer
            repeat: true
            onTriggered: {
                if (!stepButton.stepEnabled) {
                    repeatTimer.stop()
                    return
                }
                repeatTimer.interval = root.repeatIntervalMs
                stepButton.activate()
            }
        }
    }

    Row {
        anchors.centerIn: parent
        spacing: 4

        Rectangle {
            id: widthPreviewContainer
            objectName: "widthPreviewContainer"
            anchors.verticalCenter: parent.verticalCenter
            width: 22
            height: 22
            radius: 5
            color: DesignSystem.accentDefault

            Rectangle {
                id: widthDot
                objectName: "widthPreviewDot"

                readonly property real minDot: 4
                readonly property real maxDot: 20
                readonly property real ratio: (root.currentWidthValue - root.minWidthValue) /
                                              Math.max(1, root.maxWidthValue - root.minWidthValue)

                width: Math.round(minDot + ratio * (maxDot - minDot))
                height: width
                x: (parent.width - width) / 2
                y: (parent.height - height) / 2
                radius: width / 2
                color: "white"
            }
        }

        Text {
            objectName: "widthValueLabel"
            anchors.verticalCenter: parent.verticalCenter
            text: root.currentWidthValue
            font.pixelSize: 12
            color: ComponentTokens.toolbarIcon
            horizontalAlignment: Text.AlignHCenter
            width: 15
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1

            StepButton {
                objectName: "widthStepUp"
                delta: 1
                stepEnabled: root.currentWidthValue < root.maxWidthValue
            }

            StepButton {
                objectName: "widthStepDown"
                delta: -1
                stepEnabled: root.currentWidthValue > root.minWidthValue
            }
        }
    }
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `./scripts/build.sh && ctest --test-dir build -R Qml_ --output-on-failure`
Expected: PASS, including the pre-existing dot-centering test.

- [ ] **Step 5: Manual layout check**

Run `./scripts/build-and-run.sh`, capture a region, and select pencil, arrow, shape, and mosaic in turn. The sub-toolbar must grow to fit without clipping or overlapping the colour palette or auto-blur sections, on both 1x and 2x displays. Wheeling over the strip must still change the value, and the label must track it.

- [ ] **Step 6: Commit**

```bash
git add src/qml/toolbar/WidthSection.qml tests/Qml/tst_WidthSectionQml.cpp
git commit -m "feat(toolbar): add value readout and stepper to width section"
```

---

### Task 5: Extract the shared tooltip controller

Pure refactor: one implementation replaces two near-identical copies. No user-visible change. Do this before Task 6 so the sub-toolbar has something to call.

**Files:**
- Create: `include/qml/ToolbarTooltipController.h`
- Create: `src/qml/ToolbarTooltipController.mm`
- Modify: `src/qml/QmlFloatingToolbar.mm:256, 322-345, 405-420, 483, 498, 510, 542, 924, 958-971`, `include/qml/QmlFloatingToolbar.h`
- Modify: `src/qml/QmlWindowedToolbar.mm:105, 147-165, 227-255, 282, 297, 311, 331, 389-404, 484, 551, 556-650`, `include/qml/QmlWindowedToolbar.h`
- Modify: `CMakeLists.txt:998` (add to `SNAPTRAY_QML_NATIVE_SOURCES`)

**Interfaces:**
- Consumes: `QmlOverlayManager::createParentOverlay`, `QmlOverlayManager::applyShownOverlayWindowPolicy`, `qrc:/SnapTrayQml/components/RecordingTooltip.qml`.
- Produces: `SnapTray::TooltipPlacement` and `SnapTray::ToolbarTooltipController` — full
  declaration in Step 2 below. Both `QmlWindowedToolbar` and `QmlFloatingSubToolbar` already
  live in `namespace SnapTray`, so they use the names unqualified.

- [ ] **Step 1: Read both existing implementations side by side**

Run: `sed -n '322,420p' src/qml/QmlFloatingToolbar.mm` and `sed -n '147,255p;556,660p' src/qml/QmlWindowedToolbar.mm`

Note every difference before extracting. The known ones: `QmlWindowedToolbar` raises the tooltip above an associated `QWidget` pin window via `nsWindowForWidget(m_associatedPinWindow)`; `QmlFloatingToolbar` does not. The controller must keep that behaviour behind `setAssociatedWidget`, which `QmlFloatingToolbar` simply never calls.

- [ ] **Step 2: Create the controller with the union of both behaviours**

Create `include/qml/ToolbarTooltipController.h`:

```cpp
#pragma once

#include <QObject>
#include <QRect>
#include <QString>

class QQuickView;
class QQuickItem;
class QWidget;
class QWindow;

namespace SnapTray {

enum class TooltipPlacement {
    Above,
    Below,
};

/**
 * @brief Shared hover tooltip for QML toolbar overlays.
 *
 * Owns a transparent-for-input QQuickView running RecordingTooltip.qml,
 * positioned relative to an anchor rect in global coordinates. Used by the
 * capture toolbar, the Pin Window toolbar, and the tool options sub-toolbar.
 */
class ToolbarTooltipController : public QObject
{
    Q_OBJECT

public:
    explicit ToolbarTooltipController(QObject* parent = nullptr);
    ~ToolbarTooltipController() override;

    /**
     * @brief Show the tooltip near an anchor.
     * @param text Tooltip text; an empty string hides instead.
     * @param anchorGlobalRect Anchor rect in global screen coordinates.
     * @param owner The toolbar view; becomes the transient parent.
     * @param preferred Side to try first. Flips to the other side when the
     *        preferred side would leave the screen.
     */
    void showFor(const QString& text,
                 const QRect& anchorGlobalRect,
                 QQuickView* owner,
                 TooltipPlacement preferred);

    void hide();

    /**
     * @brief Extra window whose level the tooltip must stay above (macOS).
     *
     * Set by the Pin Window toolbar so the tooltip is not covered by the pin
     * window. Leave unset elsewhere.
     */
    void setAssociatedWidget(QWidget* widget);

    QWindow* window() const;

private:
    void ensureView();
    void applyWindowFlags(QQuickView* owner);

    QQuickView* m_view = nullptr;
    QQuickItem* m_rootItem = nullptr;
    QWidget* m_associatedWidget = nullptr;
    quint64 m_requestId = 0;
};

} // namespace SnapTray
```

Then create `src/qml/ToolbarTooltipController.mm`, moving verbatim where possible:

- lazy `QQuickView` creation with `WindowDoesNotAcceptFocus`, `WindowTransparentForInput`, `SizeRootObjectToView`, and the `QQuickView::Error` logging loop
- `setTransientParent(owner)` plus `applyShownOverlayWindowPolicy`
- the macOS block: `NSPopUpMenuWindowLevel`, `qMax` against the owner's and the associated widget's window levels, `hidesOnDeactivate:NO`, `ignoresMouseEvents:YES`, `hasShadow:YES`, `sharingType:NSWindowSharingNone`
- the `m_tooltipRequestId` guard and the `QTimer::singleShot(0, …)` measure-then-place sequence
- horizontal clamping to `screen->availableGeometry()`
- the off-screen fallback that flips to the opposite side

`showFor` places above the anchor when `preferred == TooltipPlacement::Above` (offset `-tipHeight - 6`) and below when `Below` (offset `+6`), keeping the existing 6 px gap. The fallback flips to the other side when the preferred side lands outside the screen.

Add to `CMakeLists.txt` after line 998:

```cmake
    src/qml/ToolbarTooltipController.mm
```

- [ ] **Step 3: Migrate `QmlWindowedToolbar`**

Replace `m_tooltipView` / `m_tooltipRootItem` / `m_tooltipRequestId` with a single `ToolbarTooltipController m_tooltip;`. Delete `ensureTooltipView`, `applyTooltipWindowFlags`, `syncTooltipTransientParent`, `showTooltip`, `hideTooltip`, and the destructor teardown at line 105. `onButtonHovered` keeps its ViewModel lookup and becomes:

```cpp
    m_tooltip.setAssociatedWidget(m_associatedPinWindow);
    m_tooltip.showFor(tip, anchorRect, m_view, TooltipPlacement::Above);
```

`onButtonUnhovered` becomes `m_tooltip.hide();`. `tooltipWindow()` returns `m_tooltip.window()`. Every existing `hideTooltip()` call site (lines 282, 297, 484, 551) becomes `m_tooltip.hide()`.

- [ ] **Step 4: Migrate `QmlFloatingToolbar`**

Same substitution, passing `TooltipPlacement::Above` and never calling `setAssociatedWidget`.

- [ ] **Step 5: Verify no tooltip code is left behind**

Run: `grep -n "m_tooltipView\|ensureTooltipView\|applyTooltipWindowFlags" src/qml/QmlFloatingToolbar.mm src/qml/QmlWindowedToolbar.mm`
Expected: no output.

- [ ] **Step 6: Build and run the full suite**

Run: `./scripts/build.sh && ./scripts/run-tests.sh`
Expected: all tests pass, including `Qml_QmlOverlayManagerLifetime` and `Qml_WindowPolicyGuard`.

- [ ] **Step 7: Manual regression check — this is the riskiest step in the plan**

Run `./scripts/build-and-run.sh` and verify tooltips still work on both migrated toolbars:

- Capture toolbar: hover each button, tooltip appears **above**, follows the pointer between buttons, disappears on leave.
- Pin Window toolbar: same, and the tooltip must render above the pin window rather than behind it.
- Drag a toolbar to the very top of the screen and hover — the tooltip must flip below instead of going off-screen.
- Drag to the left and right screen edges — the tooltip must stay fully on screen.
- Click straight through where a tooltip was showing — it must not swallow the click.
- On a multi-monitor setup, repeat on the secondary display.

- [ ] **Step 8: Commit**

```bash
git add include/qml/ToolbarTooltipController.h src/qml/ToolbarTooltipController.mm \
        include/qml/QmlFloatingToolbar.h src/qml/QmlFloatingToolbar.mm \
        include/qml/QmlWindowedToolbar.h src/qml/QmlWindowedToolbar.mm CMakeLists.txt
git commit -m "refactor(qml): extract shared ToolbarTooltipController"
```

---

### Task 6: Sub-toolbar tooltip on the width control

**Files:**
- Modify: `include/qml/PinToolOptionsViewModel.h`, `src/qml/PinToolOptionsViewModel.cpp`
- Modify: `src/qml/toolbar/WidthSection.qml`
- Modify: `src/qml/toolbar/ToolOptionsStrip.qml`
- Modify: `include/qml/QmlFloatingSubToolbar.h`, `src/qml/QmlFloatingSubToolbar.mm`
- Modify: all 24 files in `translations/`
- Test: `tests/Qml/tst_PinToolOptionsViewModel.cpp`

**Interfaces:**
- Consumes: `ToolbarTooltipController` and `TooltipPlacement` from Task 5; `widthSlotForTool` from Task 1.
- Produces: `Q_PROPERTY(QString widthTooltip …)` on `PinToolOptionsViewModel`; QML signals `ToolOptionsStrip.widthHovered(real, real, real, real)` and `ToolOptionsStrip.widthUnhovered()`.

- [ ] **Step 1: Write the failing tooltip-text test**

Add to `tests/Qml/tst_PinToolOptionsViewModel.cpp`:

```cpp
void tst_PinToolOptionsViewModel::testWidthTooltipTracksToolAndValue()
{
    PinToolOptionsViewModel viewModel;

    viewModel.showForTool(static_cast<int>(ToolId::Mosaic));
    viewModel.setCurrentWidth(18);
    QVERIFY(viewModel.widthTooltip().contains(QStringLiteral("18")));
    const QString mosaicTip = viewModel.widthTooltip();

    viewModel.setCurrentWidth(24);
    QVERIFY(viewModel.widthTooltip().contains(QStringLiteral("24")));

    viewModel.showForTool(static_cast<int>(ToolId::Pencil));
    viewModel.setCurrentWidth(3);
    QVERIFY(viewModel.widthTooltip().contains(QStringLiteral("3")));
    QVERIFY(viewModel.widthTooltip() != mosaicTip);
}

void tst_PinToolOptionsViewModel::testWidthTooltipChangesNotify()
{
    PinToolOptionsViewModel viewModel;
    viewModel.showForTool(static_cast<int>(ToolId::Pencil));

    QSignalSpy spy(&viewModel, &PinToolOptionsViewModel::widthTooltipChanged);
    viewModel.setCurrentWidth(9);
    QCOMPARE(spy.count(), 1);
}
```

Declare both in the class's `private slots:` block.

- [ ] **Step 2: Run the test to verify it fails**

Run: `./scripts/build.sh`
Expected: build FAILS with `no member named 'widthTooltip'`.

- [ ] **Step 3: Add the tooltip text to the ViewModel**

In `include/qml/PinToolOptionsViewModel.h`, add near the width section (line 38):

```cpp
    Q_PROPERTY(QString widthTooltip READ widthTooltip NOTIFY widthTooltipChanged)
```

Declare `QString widthTooltip() const;`, add `void widthTooltipChanged();` to `signals:`, and add `ToolId m_currentToolId = ToolId::Selection;` to the members.

In `src/qml/PinToolOptionsViewModel.cpp`, store the tool inside `showForTool` (after the `ToolId id = …` conversion near line 77):

```cpp
    m_currentToolId = id;
    emit widthTooltipChanged();
```

Implement the accessor, and emit the change signal from `setCurrentWidth` alongside `currentWidthChanged`:

```cpp
QString PinToolOptionsViewModel::widthTooltip() const
{
    if (widthSlotForTool(m_currentToolId) == WidthSlot::MosaicBrush) {
        return tr("Mosaic size: %1 (scroll to adjust)").arg(m_currentWidth);
    }
    return tr("Line width: %1 (scroll to adjust)").arg(m_currentWidth);
}
```

Add `#include "tools/ToolWidthSlot.h"`.

- [ ] **Step 4: Run the test to verify it passes**

Run: `./scripts/build.sh && ctest --test-dir build -R Qml_PinToolOptionsViewModel --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Emit hover from the QML strip**

In `src/qml/toolbar/WidthSection.qml`, add a hover-only `MouseArea` **below** the `Row` in z-order so the stepper still receives presses:

```qml
    signal hoverEntered()
    signal hoverExited()

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
        z: -1
        onEntered: root.hoverEntered()
        onExited: root.hoverExited()
    }
```

In `src/qml/toolbar/ToolOptionsStrip.qml`, add signals on the root item:

```qml
    signal widthHovered(real globalX, real globalY, real w, real h)
    signal widthUnhovered()
```

and wire them on the existing `WidthSection` instance (line 104):

```qml
            WidthSection {
                id: widthSection
                visible: root.hasViewModel && root.viewModel.showWidthSection
                viewModel: root.viewModel
                anchors.verticalCenter: parent.verticalCenter
                onHoverEntered: {
                    var mapped = widthSection.mapToGlobal(0, 0)
                    root.widthHovered(mapped.x, mapped.y, widthSection.width, widthSection.height)
                }
                onHoverExited: root.widthUnhovered()
            }
```

- [ ] **Step 6: Wire the controller into `QmlFloatingSubToolbar`**

In `include/qml/QmlFloatingSubToolbar.h`, add `#include "qml/ToolbarTooltipController.h"` and a `ToolbarTooltipController m_tooltip;` member. In `src/qml/QmlFloatingSubToolbar.mm`, connect the two root-item signals after the view is created:

```cpp
    connect(rootItem, SIGNAL(widthHovered(double, double, double, double)),
            this, SLOT(onWidthHovered(double, double, double, double)));
    connect(rootItem, SIGNAL(widthUnhovered()), this, SLOT(onWidthUnhovered()));
```

```cpp
void QmlFloatingSubToolbar::onWidthHovered(double globalX, double globalY,
                                           double w, double h)
{
    const QString tip = m_viewModel->widthTooltip();
    if (tip.isEmpty())
        return;

    const QRect anchor(QPoint(qRound(globalX), qRound(globalY)),
                       QSize(qRound(w), qRound(h)));
    m_tooltip.showFor(tip, anchor, m_view, TooltipPlacement::Below);
}

void QmlFloatingSubToolbar::onWidthUnhovered()
{
    m_tooltip.hide();
}
```

Call `m_tooltip.hide()` from `hide()`, `close()`, and `showForTool()` so the tooltip never outlives the strip.

Keep the tooltip current while the wheel turns — connect once in the constructor:

```cpp
    connect(m_viewModel, &PinToolOptionsViewModel::widthTooltipChanged,
            this, [this]() {
        if (m_tooltip.window() && m_tooltip.window()->isVisible())
            onWidthHovered(m_lastWidthAnchor.x(), m_lastWidthAnchor.y(),
                           m_lastWidthAnchor.width(), m_lastWidthAnchor.height());
    });
```

Store the anchor in `onWidthHovered` as `m_lastWidthAnchor` so the refresh can reuse it.

- [ ] **Step 7: Add the strings to the translation files**

Run: `cmake --build build --target SnapTray_lupdate`

Then add a translation for `Mosaic size: %1 (scroll to adjust)` and `Line width: %1 (scroll to adjust)` in each of the 24 `translations/snaptray_*.ts` files. Keep the `%1` placeholder in every language.

- [ ] **Step 8: Build and run the full suite**

Run: `./scripts/build.sh && ./scripts/run-tests.sh`
Expected: all tests pass, including `Settings_QmlTranslations`.

- [ ] **Step 9: Manual check**

Run `./scripts/build-and-run.sh`:

- Hover the width control in Region Capture, Screen Canvas, and Pin Window. The tooltip appears **below** the sub-toolbar and never covers the main toolbar.
- While hovering, scroll — the number in the tooltip updates in place without flicker.
- Click the chevrons while the tooltip is showing — the tooltip must not block the click.
- Switch pencil → mosaic — the tooltip text changes between "Line width" and "Mosaic size".
- Move the toolbar to the bottom screen edge — the tooltip flips above rather than going off-screen.
- Switch the app language and confirm the tooltip is translated.

- [ ] **Step 10: Commit**

```bash
git add include/qml/PinToolOptionsViewModel.h src/qml/PinToolOptionsViewModel.cpp \
        src/qml/toolbar/WidthSection.qml src/qml/toolbar/ToolOptionsStrip.qml \
        include/qml/QmlFloatingSubToolbar.h src/qml/QmlFloatingSubToolbar.mm \
        translations/ tests/Qml/tst_PinToolOptionsViewModel.cpp
git commit -m "feat(toolbar): show brush size tooltip on sub-toolbar hover"
```

---

## Final verification

- [ ] `./scripts/build.sh && ./scripts/run-tests.sh` — full suite green
- [ ] Cross-platform build check: the new `.mm` file must compile as C++ on Windows and Linux (`if(NOT APPLE) … LANGUAGE CXX` covers it, but verify in CI or a local Windows build)
- [ ] Update `CHANGELOG.md` with two entries: the mosaic brush size fix and the sub-toolbar size control
- [ ] Confirm against the spec's acceptance points: mosaic size adjustable and persisted on Region Capture and Pin Window; cursor preview matches actual coverage at both bounds; per-tool widths do not interfere; no regression in `Pixelate` / `Gaussian`, undo/redo, or edge drawing
