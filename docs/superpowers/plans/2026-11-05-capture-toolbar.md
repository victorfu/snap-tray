# Capture Toolbar Grouping, Priority Overflow, and Selected-Object Styles — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Group the annotation toolbars into draw / assist / finish, collapse low-priority buttons into **More** on every toolbar, and make the style strip edit the selected annotation with undo.

**Architecture:** A data table (`ToolbarTraits`) gives each button a group and a collapse priority; view models stamp them into button entries, the shared `computeToolbarOverflow` collapses by priority, and a shared `ToolbarOverflowPopup` serves both toolbar windows. For styles, `AnnotationLayer` gains a `Modify` history command; a pure `AnnotationStyle` module reads/applies style values; `PinToolOptionsViewModel` gets a "selection target" mode that emits `selectionStyleEdited` instead of the default signals; `AnnotationStyleEditor` glues the view model to the layer (coalescing, selection tracking) and is created by each host.

**Tech Stack:** C++17, Qt 6.12 (Widgets, Quick/QML, Qt Test), CMake/Ninja.

**Spec:** `docs/superpowers/specs/2026-11-05-capture-toolbar-design.md`

## Global Constraints

- Qt 6.12.0; build with `./scripts/build.sh`, test with `./scripts/run-tests.sh` (macOS). Single tests: `ctest --test-dir build -R <Name> --output-on-failure`.
- Button hit targets stay 28×24 (`buttonWidth` 28, `buttonHeight` 24). Nothing shrinks to fit.
- Never collapsed: Copy, Save, Pin, Pin's Done (`PinToolbarViewModel::ButtonDone`), Undo, Redo, Cancel, Exit, Multi-Region Done, and the active tool.
- Collapse order (first to go): QR Code, OCR, Multi-Region, Emoji, Step Badge, Eraser, Mosaic, then remaining tools from the end of their group.
- Styles go through `AnnotationSettingsManager` for defaults only; edits to a selected annotation never change saved defaults.
- `Modify` commands obey `kMaxHistoryCommands` (100) and `kMaxHistoryOwnedBytes` (64 MB), clear redo, and are refused while an erase transaction is active.
- Coalescing idle timeout: 500 ms.
- Strip label strings: `"Selected: %1"` and `"New object"`; type names `Text`, `Arrow`, `Line`, `Rectangle`, `Ellipse`.
- No `switch` for tool data — use lookup tables (CLAUDE.md). No magic numbers — named constants.
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. Undo/redo pressed while a coalesced modify is still open → the open command must close first; the next style change must push a new command, not mutate an already-undone one. (Task 6 test `modifyCoalescingEndsOnUndo`.)
2. The selected annotation's index shifts (another annotation is undone/erased) while it stays selected → the editor must address it by id, not a stale index. (Task 9 test `targetFollowsIdAfterIndexShift`.)
3. Text annotation being typed inline while also selected → style must go through the inline editor path only, never also a `Modify` command. (Task 9 test `inlineTextEditingBypassesSelectionTarget`.)
4. The active tool is a low-priority tool (e.g. Emoji) on a narrow toolbar → it stays visible. (Task 2 test `activeToolStaysVisible`.)
5. Screen/selection change while the **More** menu is open → the menu closes rather than showing stale buttons. (Task 4 test `overflowClosesOnRelayout`.)

---

## File Map

| File | Responsibility |
| --- | --- |
| Create `include/tools/ToolbarTraits.h` | `ToolbarGroup`, `toolbarGroup(ToolId)`, `toolbarCollapsePriority(ToolId)`, constants |
| Modify `include/qml/ToolbarViewModelBase.h`, `src/qml/ToolbarViewModelBase.cpp` | Stamp `group`, `groupStart`, `collapsePriority` into entries |
| Modify `src/qml/RegionToolbarViewModel.cpp`, `PinToolbarViewModel.cpp`, `CanvasToolbarViewModel.cpp` | Drop hand-set cross-group separators; custom buttons pass group |
| Modify `include/qml/ToolbarOverflowLayout.h` | Priority collapse, group gap in width |
| Modify `src/qml/toolbar/FloatingToolbar.qml`, `src/qml/tokens/ComponentTokens.qml` | Group gap, finish tint |
| Create `include/qml/ToolbarOverflowPopup.h`, `src/qml/ToolbarOverflowPopup.mm` | More-menu window shared by both toolbar classes |
| Modify `src/qml/QmlWindowedToolbar.mm`, `include/qml/QmlWindowedToolbar.h` | Use `ToolbarOverflowPopup` |
| Modify `src/qml/QmlFloatingToolbar.mm`, `include/qml/QmlFloatingToolbar.h` | Overflow layout + popup |
| Modify `include/annotations/AnnotationLayer.h`, `src/AnnotationLayer.cpp` | `Modify` command, `selectionChanged`, id helpers |
| Modify `include/annotations/{Shape,Arrow,Polyline}Annotation.h` (+ .cpp) | Missing style setters |
| Create `include/annotations/AnnotationStyle.h`, `src/annotations/AnnotationStyle.cpp` | Pure read/apply of style values |
| Modify `include/qml/PinToolOptionsViewModel.h`, `src/qml/PinToolOptionsViewModel.cpp`, `src/qml/toolbar/ToolOptionsStrip.qml` | Selection target mode + label |
| Create `include/annotation/AnnotationStyleEditor.h`, `src/annotation/AnnotationStyleEditor.cpp` | Glue: VM ↔ layer, coalescing timer, selection tracking |
| Modify `src/RegionSelector.cpp`, `src/PinWindow.cpp`, `src/ScreenCanvasSession.cpp` (+ headers) | Create editor, route dropdown results, refresh strip |
| Tests | `tests/Qml/tst_ToolbarOverflow.cpp`, `tests/Qml/tst_{Region,Pin,Canvas}ToolbarViewModel.cpp`, `tests/Annotations/tst_AnnotationLayer.cpp`, new `tests/Annotations/tst_AnnotationStyle.cpp`, `tests/Qml/tst_PinToolOptionsViewModel.cpp`, new `tests/Annotations/tst_AnnotationStyleEditor.cpp`, `tests/{RegionSelector,PinWindow,ScreenCanvas}/tst_StyleSync.cpp`, new `tests/Qml/tst_ToolbarEvidence.cpp` |

---

### Task 1: Toolbar groups and collapse priority in button entries

**Files:**
- Create: `include/tools/ToolbarTraits.h`
- Modify: `include/qml/ToolbarViewModelBase.h`, `src/qml/ToolbarViewModelBase.cpp`
- Modify: `src/qml/RegionToolbarViewModel.cpp`, `src/qml/PinToolbarViewModel.cpp`, `src/qml/CanvasToolbarViewModel.cpp`
- Test: `tests/Qml/tst_RegionToolbarViewModel.cpp`, `tests/Qml/tst_PinToolbarViewModel.cpp`, `tests/Qml/tst_CanvasToolbarViewModel.cpp`

**Interfaces:**
- Produces: `enum class ToolbarGroup { Draw = 0, Assist = 1, Finish = 2 }`; `ToolbarGroup toolbarGroup(ToolId)`; `int toolbarCollapsePriority(ToolId)`; `constexpr int kNeverCollapse`. Button entry keys `"group"` (int), `"groupStart"` (bool), `"collapsePriority"` (int). `ToolButtonOptions` gains `std::optional<ToolbarGroup> group` and `std::optional<int> collapsePriority`.

- [ ] **Step 1: Write the failing tests**

Add to `tests/Qml/tst_RegionToolbarViewModel.cpp` (new private slot):

```cpp
void TestRegionToolbarViewModel::buttonsCarryGroups()
{
    RegionToolbarViewModel vm;
    QHash<int, int> groupById;
    int previousGroup = -1;
    for (const QVariant& v : vm.buttons()) {
        const QVariantMap b = v.toMap();
        const int group = b.value("group").toInt();
        groupById.insert(b.value("id").toInt(), group);
        QCOMPARE(b.value("groupStart").toBool(), previousGroup != -1 && group != previousGroup);
        if (b.value("groupStart").toBool()) QVERIFY(b.value("separatorBefore").toBool());
        QVERIFY(group >= previousGroup); // groups are contiguous and ordered
        previousGroup = group;
    }
    QCOMPARE(groupById.value(int(ToolId::Arrow)), int(ToolbarGroup::Draw));
    QCOMPARE(groupById.value(int(ToolId::EmojiSticker)), int(ToolbarGroup::Draw));
    QCOMPARE(groupById.value(int(ToolId::Undo)), int(ToolbarGroup::Assist));
    QCOMPARE(groupById.value(int(ToolId::MultiRegion)), int(ToolbarGroup::Assist));
    QCOMPARE(groupById.value(int(ToolId::Pin)), int(ToolbarGroup::Finish));
    QCOMPARE(groupById.value(int(ToolId::Copy)), int(ToolbarGroup::Finish));
}

void TestRegionToolbarViewModel::collapsePriorityOrder()
{
    QVERIFY(toolbarCollapsePriority(ToolId::QRCode) < toolbarCollapsePriority(ToolId::OCR));
    QVERIFY(toolbarCollapsePriority(ToolId::OCR) < toolbarCollapsePriority(ToolId::MultiRegion));
    QVERIFY(toolbarCollapsePriority(ToolId::MultiRegion) < toolbarCollapsePriority(ToolId::EmojiSticker));
    QVERIFY(toolbarCollapsePriority(ToolId::EmojiSticker) < toolbarCollapsePriority(ToolId::StepBadge));
    QVERIFY(toolbarCollapsePriority(ToolId::StepBadge) < toolbarCollapsePriority(ToolId::Eraser));
    QVERIFY(toolbarCollapsePriority(ToolId::Eraser) < toolbarCollapsePriority(ToolId::Mosaic));
    QVERIFY(toolbarCollapsePriority(ToolId::Mosaic) < toolbarCollapsePriority(ToolId::Text));
    for (ToolId id : {ToolId::Copy, ToolId::Save, ToolId::Pin, ToolId::Undo, ToolId::Redo,
                      ToolId::Cancel, ToolId::Exit, ToolId::MultiRegionDone})
        QCOMPARE(toolbarCollapsePriority(id), kNeverCollapse);
}
```

Add `#include "tools/ToolbarTraits.h"` to the test. In `tests/Qml/tst_PinToolbarViewModel.cpp` add:

```cpp
void TestPinToolbarViewModel::buttonsCarryGroups()
{
    PinToolbarViewModel vm;
    QHash<int, QVariantMap> byId;
    for (const QVariant& v : vm.buttons()) byId.insert(v.toMap().value("id").toInt(), v.toMap());
    QCOMPARE(byId.value(int(ToolId::Beautify)).value("group").toInt(), int(ToolbarGroup::Assist));
    QVERIFY(byId.value(int(ToolId::Beautify)).value("groupStart").toBool());
    // Crop stays in Beautify's block with no separator, as today.
    QVERIFY(!byId.value(int(ToolId::Crop)).value("separatorBefore").toBool());
    // Undo keeps its intra-group separator.
    QVERIFY(byId.value(int(ToolId::Undo)).value("separatorBefore").toBool());
    QVERIFY(!byId.value(int(ToolId::Undo)).value("groupStart").toBool());
    const QVariantMap done = byId.value(PinToolbarViewModel::ButtonDone);
    QCOMPARE(done.value("group").toInt(), int(ToolbarGroup::Finish));
    QCOMPARE(done.value("collapsePriority").toInt(), kNeverCollapse);
    QCOMPARE(byId.value(int(ToolId::Save)).value("group").toInt(), int(ToolbarGroup::Finish));
}
```

In `tests/Qml/tst_CanvasToolbarViewModel.cpp` add:

```cpp
void TestCanvasToolbarViewModel::buttonsCarryGroups()
{
    CanvasToolbarViewModel vm;
    QHash<int, QVariantMap> byId;
    for (const QVariant& v : vm.buttons()) byId.insert(v.toMap().value("id").toInt(), v.toMap());
    constexpr int kLaser = 10001;
    QCOMPARE(byId.value(kLaser).value("group").toInt(), int(ToolbarGroup::Draw));
    QCOMPARE(byId.value(int(ToolId::CanvasWhiteboard)).value("group").toInt(), int(ToolbarGroup::Assist));
    QCOMPARE(byId.value(int(ToolId::Clear)).value("group").toInt(), int(ToolbarGroup::Assist));
    QCOMPARE(byId.value(int(ToolId::Copy)).value("group").toInt(), int(ToolbarGroup::Finish));
    QCOMPARE(byId.value(int(ToolId::Exit)).value("collapsePriority").toInt(), kNeverCollapse);
}
```

Declare each new slot in its test class.

- [ ] **Step 2: Run tests to verify they fail**

Run: `./scripts/build.sh && ctest --test-dir build -R "Qml_(Region|Pin|Canvas)ToolbarViewModel" --output-on-failure`
Expected: build FAILS — `ToolbarTraits.h` not found.

- [ ] **Step 3: Create `include/tools/ToolbarTraits.h`**

```cpp
#pragma once

#include <array>
#include <climits>

#include "tools/ToolId.h"

// Toolbar grouping and collapse order (#224). One table, no switch.
enum class ToolbarGroup { Draw = 0, Assist = 1, Finish = 2 };

constexpr int kNeverCollapse = INT_MAX;

namespace ToolbarTraitsDetail {
struct Entry {
    ToolbarGroup group;
    int collapsePriority; // lower collapses first
};

// Base priority for drawing tools without an explicit rank; their position in
// the row is added so later buttons collapse first.
constexpr int kDrawBasePriority = 1000;

constexpr std::array<Entry, static_cast<int>(ToolId::Count)> makeTable()
{
    std::array<Entry, static_cast<int>(ToolId::Count)> t{};
    for (auto& e : t) e = {ToolbarGroup::Draw, kDrawBasePriority};
    auto set = [&t](ToolId id, ToolbarGroup g, int p) { t[static_cast<int>(id)] = {g, p}; };
    // Assist group
    set(ToolId::QRCode, ToolbarGroup::Assist, 10);
    set(ToolId::OCR, ToolbarGroup::Assist, 20);
    set(ToolId::MultiRegion, ToolbarGroup::Assist, 30);
    set(ToolId::Measure, ToolbarGroup::Assist, 35);
    set(ToolId::Crop, ToolbarGroup::Assist, 36);
    set(ToolId::Beautify, ToolbarGroup::Assist, 37);
    set(ToolId::CanvasBlackboard, ToolbarGroup::Assist, 38);
    set(ToolId::CanvasWhiteboard, ToolbarGroup::Assist, 39);
    set(ToolId::Clear, ToolbarGroup::Assist, 45);
    set(ToolId::Undo, ToolbarGroup::Assist, kNeverCollapse);
    set(ToolId::Redo, ToolbarGroup::Assist, kNeverCollapse);
    // Draw group, explicit ranks
    set(ToolId::EmojiSticker, ToolbarGroup::Draw, 40);
    set(ToolId::StepBadge, ToolbarGroup::Draw, 50);
    set(ToolId::Eraser, ToolbarGroup::Draw, 60);
    set(ToolId::Mosaic, ToolbarGroup::Draw, 70);
    // Finish group
    set(ToolId::Pin, ToolbarGroup::Finish, kNeverCollapse);
    set(ToolId::Save, ToolbarGroup::Finish, kNeverCollapse);
    set(ToolId::Copy, ToolbarGroup::Finish, kNeverCollapse);
    set(ToolId::Cancel, ToolbarGroup::Finish, kNeverCollapse);
    set(ToolId::Exit, ToolbarGroup::Finish, kNeverCollapse);
    set(ToolId::MultiRegionDone, ToolbarGroup::Finish, kNeverCollapse);
    return t;
}
inline constexpr auto kTable = makeTable();
} // namespace ToolbarTraitsDetail

constexpr ToolbarGroup toolbarGroup(ToolId id)
{
    return ToolbarTraitsDetail::kTable[static_cast<int>(id)].group;
}

constexpr int toolbarCollapsePriority(ToolId id)
{
    return ToolbarTraitsDetail::kTable[static_cast<int>(id)].collapsePriority;
}
```

Note: the "position in the row" rule for unranked drawing tools is applied in `ToolbarViewModelBase::setButtons` (Step 4), so Shape/Arrow/Pencil/Marker/Text get `1000 - rowIndex`, making the last one in the row collapse first.

- [ ] **Step 4: Stamp group data in `ToolbarViewModelBase`**

In `ToolbarViewModelBase.h`, add `#include <optional>` and `#include "tools/ToolbarTraits.h"`, and extend `ToolButtonOptions`:

```cpp
        std::optional<ToolbarGroup> group;      // custom buttons must set it
        std::optional<int> collapsePriority;    // custom buttons must set it
```

In `buildToolButtonEntry`, before calling `buildCustomButtonEntry`, fill the defaults from the table:

```cpp
    ToolButtonOptions resolved = options;
    if (!resolved.group) resolved.group = toolbarGroup(toolId);
    if (!resolved.collapsePriority) resolved.collapsePriority = toolbarCollapsePriority(toolId);
```

and pass `resolved`. In `buildCustomButtonEntry` add:

```cpp
    entry[QStringLiteral("group")] = static_cast<int>(options.group.value_or(ToolbarGroup::Assist));
    entry[QStringLiteral("collapsePriority")] = options.collapsePriority.value_or(kNeverCollapse);
```

Replace `setButtons` so it derives `groupStart`, forces a separator at group boundaries, and ranks unranked drawing tools:

```cpp
void ToolbarViewModelBase::setButtons(QVariantList buttons)
{
    int previousGroup = -1;
    for (int i = 0; i < buttons.size(); ++i) {
        QVariantMap entry = buttons[i].toMap();
        const int group = entry.value(QStringLiteral("group")).toInt();
        const bool groupStart = previousGroup != -1 && group != previousGroup;
        entry[QStringLiteral("groupStart")] = groupStart;
        if (groupStart) entry[QStringLiteral("separatorBefore")] = true;
        if (entry.value(QStringLiteral("collapsePriority")).toInt()
                == ToolbarTraitsDetail::kDrawBasePriority)
            entry[QStringLiteral("collapsePriority")] = ToolbarTraitsDetail::kDrawBasePriority - i;
        buttons[i] = entry;
        previousGroup = group;
    }
    m_buttons = std::move(buttons);
    emit buttonsChanged();
}
```

- [ ] **Step 5: Update the three view models**

`RegionToolbarViewModel::buildButtonList`: delete the `options.separatorBefore = ...` line (group boundaries now produce Undo's and Pin's separators). The multi-region list needs nothing (Done and Cancel are both Finish).

`PinToolbarViewModel::buildButtonList`: replace the separator expression with intra-group separators only:

```cpp
        // Group boundaries add their own separator; keep the sub-blocks inside Assist.
        options.separatorBefore = (toolId == ToolId::Undo || toolId == ToolId::OCR);
```

For Done: replace `doneOptions.separatorBefore = true;` with

```cpp
    doneOptions.group = ToolbarGroup::Finish;
    doneOptions.collapsePriority = kNeverCollapse;
```

`CanvasToolbarViewModel::buildButtonList`: set `laserOptions.group = ToolbarGroup::Draw; laserOptions.collapsePriority = ToolbarTraitsDetail::kDrawBasePriority;` and replace the separator line with `options.separatorBefore = (toolId == ToolId::Undo);`.

- [ ] **Step 6: Run tests to verify they pass**

Run: `./scripts/build.sh && ctest --test-dir build -R "Qml_(Region|Pin|Canvas)ToolbarViewModel|Qml_ToolbarOverflow|ToolbarState" --output-on-failure`
Expected: PASS. If an existing test asserted the old `separatorBefore` pattern, update its expectation to the group-derived one and note it in the commit message.

- [ ] **Step 7: Commit**

```bash
git add include/tools/ToolbarTraits.h include/qml/ToolbarViewModelBase.h src/qml/ToolbarViewModelBase.cpp src/qml/*ToolbarViewModel.cpp tests/Qml/tst_*ToolbarViewModel.cpp
git commit -m "feat(toolbar): group buttons into draw/assist/finish with collapse priority (#224)"
```

---

### Task 2: Priority overflow and group gap in layout math

**Files:**
- Modify: `include/qml/ToolbarOverflowLayout.h`
- Modify: `src/qml/QmlWindowedToolbar.mm:490-510` (`applyOverflowLayout`)
- Test: `tests/Qml/tst_ToolbarOverflow.cpp`

**Interfaces:**
- Consumes: entry keys `group`, `groupStart`, `collapsePriority` (Task 1).
- Produces: `ToolbarLayoutMetrics::groupGap` (int, default 4); `computeToolbarOverflow(const QVariantList&, int maximumWidth, int activeToolId, const ToolbarLayoutMetrics& = {})`. The `pinned` parameter is removed; pinning is `collapsePriority == kNeverCollapse` or `id == activeToolId`.

- [ ] **Step 1: Write the failing tests**

Add to `TestToolbarOverflow` (declare slots):

```cpp
static QVariantList fakeButtons()
{
    // id, group, priority — mirrors the Region Selector row.
    const QList<std::tuple<int, int, int>> spec = {
        {int(ToolId::Shape), 0, 999}, {int(ToolId::Arrow), 0, 998}, {int(ToolId::Text), 0, 996},
        {int(ToolId::Mosaic), 0, 70}, {int(ToolId::Eraser), 0, 60}, {int(ToolId::StepBadge), 0, 50},
        {int(ToolId::EmojiSticker), 0, 40}, {int(ToolId::Undo), 1, kNeverCollapse},
        {int(ToolId::Redo), 1, kNeverCollapse}, {int(ToolId::OCR), 1, 20},
        {int(ToolId::QRCode), 1, 10}, {int(ToolId::MultiRegion), 1, 30},
        {int(ToolId::Pin), 2, kNeverCollapse}, {int(ToolId::Save), 2, kNeverCollapse},
        {int(ToolId::Copy), 2, kNeverCollapse}};
    QVariantList out;
    int previous = -1;
    for (const auto& [id, group, priority] : spec) {
        out.append(QVariantMap{{"id", id}, {"group", group}, {"collapsePriority", priority},
                               {"groupStart", previous != -1 && previous != group},
                               {"separatorBefore", previous != -1 && previous != group}});
        previous = group;
    }
    return out;
}

static QList<int> ids(const QVariantList& list)
{
    QList<int> r;
    for (const auto& v : list) r.append(v.toMap().value("id").toInt());
    return r;
}

void TestToolbarOverflow::collapsesByPriority()
{
    const QVariantList buttons = fakeButtons();
    const int full = toolbarContentWidth(buttons, true, false, {});
    // One button too narrow: QR goes first, not Copy (the tail).
    auto layout = computeToolbarOverflow(buttons, full - 1, -1);
    QCOMPARE(ids(layout.overflowButtons), QList<int>{int(ToolId::QRCode)});
    // Exactly four collapse: QR, OCR, Multi-Region, Emoji. Overflow keeps row order.
    QVariantList withoutFour;
    for (const auto& v : buttons) {
        const int id = v.toMap().value("id").toInt();
        if (id != int(ToolId::QRCode) && id != int(ToolId::OCR) && id != int(ToolId::MultiRegion)
            && id != int(ToolId::EmojiSticker))
            withoutFour.append(v);
    }
    const int width = toolbarContentWidth(restampGroupStarts(withoutFour), true, true, {});
    layout = computeToolbarOverflow(buttons, width, -1);
    QCOMPARE(ids(layout.overflowButtons),
             (QList<int>{int(ToolId::EmojiSticker), int(ToolId::OCR), int(ToolId::QRCode),
                         int(ToolId::MultiRegion)}));
    for (int id : {int(ToolId::Pin), int(ToolId::Save), int(ToolId::Copy), int(ToolId::Undo),
                   int(ToolId::Redo)})
        QVERIFY(ids(layout.visibleButtons).contains(id));
    QVERIFY(layout.width <= width);
}

void TestToolbarOverflow::activeToolStaysVisible()
{
    const QVariantList buttons = fakeButtons();
    const int width = toolbarContentWidth(buttons, true, false, {}) - 5 * 30;
    const auto layout = computeToolbarOverflow(buttons, width, int(ToolId::EmojiSticker));
    QVERIFY(ids(layout.visibleButtons).contains(int(ToolId::EmojiSticker)));
    QVERIFY(!ids(layout.overflowButtons).contains(int(ToolId::EmojiSticker)));
}

void TestToolbarOverflow::groupGapCountsInWidth()
{
    const QVariantList buttons = fakeButtons();
    ToolbarLayoutMetrics m;
    const int withGap = toolbarContentWidth(buttons, true, false, m);
    m.groupGap = 0;
    QCOMPARE(withGap - toolbarContentWidth(buttons, true, false, m), 2 * ToolbarLayoutMetrics{}.groupGap);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `./scripts/build.sh && ctest --test-dir build -R Qml_ToolbarOverflow --output-on-failure`
Expected: compile FAILS (`groupGap` missing, signature mismatch).

- [ ] **Step 3: Implement**

In `ToolbarOverflowLayout.h`: add `#include "tools/ToolbarTraits.h"`, add `int groupGap = 4;` to `ToolbarLayoutMetrics`, and in `toolbarContentWidth` add `if (map.value("groupStart").toBool()) width += m.groupGap;` next to the separator addition. Replace `computeToolbarOverflow`:

```cpp
inline ToolbarOverflowLayout computeToolbarOverflow(const QVariantList& buttons, int maximumWidth,
                                                     int activeToolId,
                                                     const ToolbarLayoutMetrics& metrics = {})
{
    ToolbarOverflowLayout result;
    result.visibleButtons = buttons;
    result.width = toolbarContentWidth(buttons, true, false, metrics);
    if (result.width <= maximumWidth) return result;

    // Candidates in collapse order: lowest priority first, later in row first on ties.
    QList<int> order;
    for (int i = 0; i < buttons.size(); ++i) {
        const QVariantMap b = buttons[i].toMap();
        if (b.value("collapsePriority").toInt() == kNeverCollapse) continue;
        if (b.value("id").toInt() == activeToolId) continue;
        order.append(i);
    }
    std::stable_sort(order.begin(), order.end(), [&buttons](int a, int b) {
        const int pa = buttons[a].toMap().value("collapsePriority").toInt();
        const int pb = buttons[b].toMap().value("collapsePriority").toInt();
        return pa != pb ? pa < pb : a > b;
    });

    QSet<int> removed;
    for (int index : order) {
        removed.insert(index);
        QVariantList visible;
        for (int i = 0; i < buttons.size(); ++i)
            if (!removed.contains(i)) visible.append(buttons[i]);
        result.visibleButtons = visible;
        result.width = toolbarContentWidth(visible, true, true, metrics);
        if (result.width <= maximumWidth) break;
    }
    if (result.width > maximumWidth) {
        result.visibleButtons.clear();
        result.overflowButtons = buttons;
        result.showDragHandle = false;
        result.width = qMax(1, qMin(maximumWidth, toolbarContentWidth({}, false, true, metrics)));
        return result;
    }
    for (int i = 0; i < buttons.size(); ++i)
        if (removed.contains(i)) result.overflowButtons.append(buttons[i]);
    return result;
}
```

Note: a group whose first visible button changed may lose its `groupStart` flag after collapse; `toolbarContentWidth` and QML both read the flag from the entry, so recompute it on the visible list. Add a helper and call it on `result.visibleButtons` before each width computation:

```cpp
inline QVariantList restampGroupStarts(QVariantList list)
{
    int previous = -1;
    for (auto& v : list) {
        QVariantMap m = v.toMap();
        const int g = m.value("group").toInt();
        const bool start = previous != -1 && g != previous;
        m["groupStart"] = start;
        if (start) m["separatorBefore"] = true;
        v = m;
        previous = g;
    }
    return list;
}
```

Use `visible = restampGroupStarts(visible);` inside the loop.

In `QmlWindowedToolbar::applyOverflowLayout`, change the call to `computeToolbarOverflow(available, usableBounds.width(), m_viewModel->activeTool(), metrics)` and add `m_rootItem->property("groupGap").toInt()` as the last metrics field (Task 3 adds the QML property; until then the property reads 0 — keep the metrics initializer field order: buttonWidth, buttonSpacing, separatorWidth, margin, dragHandleWidth, contentSpacing, groupGap). Also re-run layout when the active tool changes: in the windowed toolbar's view-model connections add `connect(m_viewModel, &ToolbarViewModelBase::activeToolChanged, this, [this] { if (m_layoutScreen) applyOverflowLayout(toolbarUsableBounds(m_layoutScreen->availableGeometry())); });`.

Update the existing `layout()` test: it calls nothing directly, but its pinned assertions stay valid (Save/Copy/Done are `kNeverCollapse`).

- [ ] **Step 4: Run tests**

Run: `./scripts/build.sh && ctest --test-dir build -R Qml_ToolbarOverflow --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add include/qml/ToolbarOverflowLayout.h src/qml/QmlWindowedToolbar.mm tests/Qml/tst_ToolbarOverflow.cpp
git commit -m "feat(toolbar): collapse by priority and keep the active tool visible (#224)"
```

---

### Task 3: Group gap and finish-group tint in QML

**Files:**
- Modify: `src/qml/toolbar/FloatingToolbar.qml`
- Modify: `src/qml/tokens/ComponentTokens.qml`
- Test: `tests/Qml/tst_ToolbarOverflow.cpp` (existing `layout()` rendered-geometry check + new slot)

**Interfaces:**
- Consumes: entry keys from Task 1; `groupGap` metric from Task 2.
- Produces: QML root property `readonly property int groupGap: 4`; delegate objectName `toolbarFinishTint_<id>`.

- [ ] **Step 1: Write the failing test**

```cpp
void TestToolbarOverflow::finishGroupIsTinted()
{
    QmlWindowedToolbar toolbar;
    toolbar.ensureView();
    toolbar.applyOverflowLayout(QRect(10, 10, 1900, 1000));
    QCoreApplication::processEvents();
    QCOMPARE(toolbar.m_rootItem->property("groupGap").toInt(), ToolbarLayoutMetrics{}.groupGap);
    auto* saveTint = toolbar.m_rootItem->findChild<QQuickItem*>(
        QStringLiteral("toolbarFinishTint_%1").arg(int(ToolId::Save)));
    QVERIFY(saveTint);
    QVERIFY(saveTint->isVisible());
    QVERIFY(!toolbar.m_rootItem->findChild<QQuickItem*>(
        QStringLiteral("toolbarFinishTint_%1").arg(int(ToolId::Arrow)))->isVisible());
}
```

- [ ] **Step 2: Run to verify failure**

Run: `./scripts/build.sh && ctest --test-dir build -R Qml_ToolbarOverflow --output-on-failure`
Expected: FAIL (`groupGap` is 0 / tint not found).

- [ ] **Step 3: Implement**

`ComponentTokens.qml` — add next to the other toolbar tokens:

```qml
    // Finishing actions (Copy / Save / Pin) sit on a faint accent band (#224).
    readonly property color toolbarFinishGroupBackground: Qt.alpha(
        SemanticTokens.isDarkMode ? DesignSystem.accentLight : DesignSystem.accentDefault, 0.14)
```

`FloatingToolbar.qml`:
- Add `readonly property int groupGap: 4` beside `separatorWidth`.
- In the delegate, `readonly property bool isGroupStart: buttonData.groupStart === true` and `readonly property bool isFinish: buttonData.group === 2`.
- Separator item width: `delegateLoader.isSeparatorBefore ? root.separatorWidth + (delegateLoader.isGroupStart ? root.groupGap : 0) : 0`.
- Add a tint `Rectangle` before `ToolbarButton` inside the delegate item:

```qml
                        Rectangle {
                            objectName: "toolbarFinishTint_" + delegateLoader.buttonId
                            visible: delegateLoader.isFinish
                            x: separatorItem.width - 1
                            width: root.buttonWidth + 2 + root.buttonSpacing
                            height: root.buttonHeight + 4
                            anchors.verticalCenter: parent.verticalCenter
                            radius: 6
                            color: ComponentTokens.toolbarFinishGroupBackground
                        }
```

- [ ] **Step 4: Run tests**

Run: `ctest --test-dir build -R "Qml_ToolbarOverflow|Qml_FloatingToolbar" --output-on-failure`
Expected: PASS; the existing rendered-geometry assertion in `layout()` proves the QML widths still match `toolbarContentWidth`.

- [ ] **Step 5: Commit**

```bash
git add src/qml/toolbar/FloatingToolbar.qml src/qml/tokens/ComponentTokens.qml tests/Qml/tst_ToolbarOverflow.cpp
git commit -m "feat(toolbar): wider gap between groups and a tinted finish group (#224)"
```

---

### Task 4: Shared More menu and overflow on the capture toolbar

**Files:**
- Create: `include/qml/ToolbarOverflowPopup.h`, `src/qml/ToolbarOverflowPopup.mm`
- Modify: `include/qml/QmlWindowedToolbar.h`, `src/qml/QmlWindowedToolbar.mm` (move code out)
- Modify: `include/qml/QmlFloatingToolbar.h`, `src/qml/QmlFloatingToolbar.mm`
- Modify: `CMakeLists.txt` (add the new `.mm` to the same target list as `QmlWindowedToolbar.mm`)
- Test: `tests/Qml/tst_ToolbarOverflow.cpp`

**Interfaces:**
- Produces:

```cpp
namespace SnapTray {
class ToolbarOverflowPopup : public QObject {
    Q_OBJECT
public:
    ToolbarOverflowPopup(ToolbarViewModelBase* viewModel, QWindow* owner, QObject* parent = nullptr);
    ~ToolbarOverflowPopup() override;
    // Toggles: shows the menu for `buttons` anchored to `anchorGlobal` inside `bounds`, or hides it.
    void toggle(const QVariantList& buttons, const QRect& anchorGlobal, const QRect& bounds);
    void hide();
    bool isVisible() const;
    QWindow* window() const;
    // Handles Esc/Up/Down/Enter while visible; returns true if consumed.
    bool handleKey(QKeyEvent* event);
signals:
    void closed();   // emitted on every hide; owners return focus to their More button
private slots:
    void onActionTriggered(int buttonId);
    // ... members moved from QmlWindowedToolbar: m_view, m_rootItem, m_anchor, m_bounds, cursor surface id
};
}
```

- `QmlFloatingToolbar::applyOverflowLayout(const QRect& usableBounds)` (public for tests via `friend class TestToolbarOverflow`).

- [ ] **Step 1: Write the failing tests**

```cpp
void TestToolbarOverflow::captureToolbarOverflows()
{
    RegionToolbarViewModel vm;
    QmlFloatingToolbar toolbar(&vm);
    toolbar.ensureView();
    toolbar.applyOverflowLayout(QRect(0, 0, 300, 400));
    QCoreApplication::processEvents();
    QVERIFY(toolbar.m_view->width() <= 300);
    const QVariantList overflow = toolbar.m_rootItem->property("overflowButtons").toList();
    QVERIFY(!overflow.isEmpty());
    QVERIFY(ids(overflow).contains(int(ToolId::QRCode)));
    for (int id : {int(ToolId::Copy), int(ToolId::Save), int(ToolId::Pin)})
        QVERIFY(ids(toolbar.m_rootItem->property("displayButtons").toList()).contains(id));
}

void TestToolbarOverflow::selectionPlacementStaysOnScreen_data()
{
    QTest::addColumn<QRect>("selection");
    QTest::newRow("left-edge") << QRect(0, 200, 120, 80);
    QTest::newRow("right-edge") << QRect(1800, 200, 120, 80);
    QTest::newRow("bottom-edge") << QRect(600, 1000, 200, 80);
    QTest::newRow("tiny") << QRect(900, 500, 20, 20);
}

void TestToolbarOverflow::selectionPlacementStaysOnScreen()
{
    QFETCH(QRect, selection);
    const QRect viewport(0, 0, 1920, 1080);
    RegionToolbarViewModel vm;
    QmlFloatingToolbar toolbar(&vm);
    toolbar.ensureView();
    toolbar.applyOverflowLayout(viewport.adjusted(10, 10, -10, -10));
    const QSize size = toolbar.m_view->size();
    const QPoint topLeft = QmlFloatingToolbar::resolveTopLeftForSelection(
        selection, size, viewport, QmlFloatingToolbar::HorizontalAlignment::Center);
    QVERIFY(viewport.contains(QRect(topLeft, size)));
}

void TestToolbarOverflow::overflowClosesOnRelayout()
{
    RegionToolbarViewModel vm;
    QmlFloatingToolbar toolbar(&vm);
    toolbar.ensureView();
    toolbar.applyOverflowLayout(QRect(0, 0, 300, 400));
    toolbar.onOverflowRequested(0, 0, 28, 24);
    QVERIFY(toolbar.m_overflow && toolbar.m_overflow->isVisible());
    toolbar.applyOverflowLayout(QRect(0, 0, 260, 400));
    QVERIFY(!toolbar.m_overflow->isVisible());
}

void TestToolbarOverflow::overflowReturnsFocusToMore()
{
    RegionToolbarViewModel vm;
    QmlFloatingToolbar toolbar(&vm);
    toolbar.ensureView();
    toolbar.applyOverflowLayout(QRect(0, 0, 300, 400));
    toolbar.show();
    toolbar.onOverflowRequested(0, 0, 28, 24);
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QVERIFY(toolbar.m_overflow->handleKey(&esc));
    auto* more = toolbar.m_rootItem->findChild<QQuickItem*>("toolbarMoreButton");
    QTRY_VERIFY(more->hasActiveFocus());
}
```

Add `friend class TestToolbarOverflow;` to `QmlFloatingToolbar`. Add includes for `RegionToolbarViewModel.h` and `QmlFloatingToolbar.h`.

- [ ] **Step 2: Run to verify failure**

Run: `./scripts/build.sh` → compile FAILS (`applyOverflowLayout`, `m_overflow` missing).

- [ ] **Step 3: Extract `ToolbarOverflowPopup`**

Move from `QmlWindowedToolbar.mm` into `ToolbarOverflowPopup.mm`, unchanged in behaviour: the creation block in `onOverflowRequested` (popup window via `QmlOverlayManager::instance().createPopupWindow()`, `ToolbarOverflowMenu.qml`, cursor surface registration, `actionTriggered` connection), `positionOverflow`, `hideOverflow`, and the key-handling branch of `eventFilter`. `toggle()` = the old body after the "already visible → hide" check, using `anchorGlobal`/`bounds` passed in. `onActionTriggered(id)` hides, then calls `m_viewModel->handleButtonClicked(id)` via `QMetaObject::invokeMethod(m_viewModel, "handleButtonClicked", Q_ARG(int, id))` (the slot exists on all three view models). `hide()` emits `closed()` when it actually hid.

`QmlWindowedToolbar` then holds `std::unique_ptr<ToolbarOverflowPopup> m_overflow;` created in `ensureView()`; `onOverflowRequested` computes the anchor as before and calls `m_overflow->toggle(m_rootItem->property("overflowButtons").toList(), anchor, toolbarUsableBounds(screen->availableGeometry()))`; `eventFilter` calls `m_overflow->handleKey(key)` while visible; `overflowWindow()` returns `m_overflow ? m_overflow->window() : nullptr`. Connect `closed` to give the More button focus:

```cpp
connect(m_overflow.get(), &ToolbarOverflowPopup::closed, this, [this] {
    if (auto* more = m_rootItem ? m_rootItem->findChild<QQuickItem*>("toolbarMoreButton") : nullptr)
        more->forceActiveFocus(Qt::PopupFocusReason);
});
```

Run `ctest -R Qml_ToolbarOverflow` now — existing `menuActionsAndLifetime` must still pass before continuing.

- [ ] **Step 4: Overflow on `QmlFloatingToolbar`**

Add members `std::unique_ptr<ToolbarOverflowPopup> m_overflow; QRect m_overflowBounds;` and:

```cpp
void QmlFloatingToolbar::applyOverflowLayout(const QRect& usableBounds)
{
    ensureView();
    auto* vm = qobject_cast<ToolbarViewModelBase*>(m_viewModel);
    if (!m_rootItem || !vm || usableBounds.isEmpty()) return;
    m_overflowBounds = usableBounds;
    QVariantList available;
    for (const auto& b : vm->buttons())
        if (!b.toMap().value("isOCR").toBool() || vm->ocrAvailable()) available.append(b);
    const ToolbarLayoutMetrics metrics{
        m_rootItem->property("buttonWidth").toInt(), m_rootItem->property("buttonSpacing").toInt(),
        m_rootItem->property("separatorWidth").toInt(), m_rootItem->property("margin").toInt(),
        m_rootItem->property("dragHandleWidth").toInt(), m_rootItem->property("contentSpacing").toInt(),
        m_rootItem->property("groupGap").toInt()};
    const auto layout = computeToolbarOverflow(available, usableBounds.width(), vm->activeTool(), metrics);
    if (m_overflow && m_rootItem->property("overflowButtons").toList() != layout.overflowButtons)
        m_overflow->hide();
    m_rootItem->setProperty("displayButtons", layout.visibleButtons);
    m_rootItem->setProperty("overflowButtons", layout.overflowButtons);
    m_rootItem->setProperty("showDragHandle", layout.showDragHandle);
    m_rootItem->setProperty("constrainedWidth", layout.width);
    m_view->resize(layout.width, m_rootItem->property("barHeight").toInt());
}
```

Note the existing `overflowClosesOnRelayout` expectation: close whenever the overflow list changes. Also hide on any relayout where the menu is visible and bounds changed (`if (m_overflow && m_overflow->isVisible() && usableBounds != previousBounds) m_overflow->hide();`) — required by the test, which narrows 300 → 260.

Call it at the top of `positionForSelection`, before reading `m_view->width()`:

```cpp
    applyOverflowLayout(insetViewportRect(QRect(0, 0, viewportWidth, viewportHeight),
                                          kToolbarPlacementViewportInset));
```

Re-run on `activeToolChanged` and `buttonsChanged` (connect in `setupConnections`, guarded by `!m_overflowBounds.isEmpty()`). Connect the QML `overflowRequested` signal to a new slot `onOverflowRequested(double, double, double, double)` that maps the anchor with `m_view->position()` and calls `m_overflow->toggle(...)` using `m_overflowBounds` translated by the parent widget's `mapToGlobal(QPoint())`. Create `m_overflow` lazily in `ensureView()` with owner `m_view`, and connect `closed` → focus More (same lambda as Step 3). Install the same key handling: in the floating toolbar's existing event filter (or `m_view` key events), forward to `m_overflow->handleKey` while visible. Hide the popup in `hide()` and `close()`.

- [ ] **Step 5: Run tests**

Run: `./scripts/build.sh && ctest --test-dir build -R "Qml_ToolbarOverflow|FloatingOverlayCaptureExclusion|EraserHostRouting|RegionSelector" --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add include/qml/ToolbarOverflowPopup.h src/qml/ToolbarOverflowPopup.mm include/qml/QmlWindowedToolbar.h src/qml/QmlWindowedToolbar.mm include/qml/QmlFloatingToolbar.h src/qml/QmlFloatingToolbar.mm CMakeLists.txt tests/Qml/tst_ToolbarOverflow.cpp
git commit -m "feat(toolbar): More menu and priority overflow on the capture and canvas toolbars (#224)"
```

---

### Task 5: Style setters on shapes, arrows, and polylines

**Files:**
- Modify: `include/annotations/ShapeAnnotation.h`, `src/annotations/ShapeAnnotation.cpp`
- Modify: `include/annotations/ArrowAnnotation.h`, `src/annotations/ArrowAnnotation.cpp`
- Modify: `include/annotations/PolylineAnnotation.h`, `src/annotations/PolylineAnnotation.cpp`
- Test: `tests/Annotations/tst_ShapeAnnotation.cpp`, `tst_ArrowAnnotation.cpp`, `tst_PolylineAnnotation.cpp`

**Interfaces:**
- Produces: `ShapeAnnotation::setColor(const QColor&)`, `setWidth(int)`, `setFilled(bool)`; `ArrowAnnotation::setColor`, `setWidth`, `setLineStyle(LineStyle)`; `PolylineAnnotation::setColor`, `setWidth`, `setLineStyle(LineStyle)`.

- [ ] **Step 1: Write the failing tests** (one per class, same shape)

```cpp
void tst_ArrowAnnotation::styleSettersChangeRendering()
{
    ArrowAnnotation arrow(QPoint(10, 10), QPoint(90, 10), Qt::red, 3);
    const QRect before = arrow.boundingRect();
    arrow.setColor(Qt::blue);
    arrow.setWidth(12);
    arrow.setLineStyle(LineStyle::Dashed);
    QCOMPARE(arrow.color(), QColor(Qt::blue));
    QCOMPARE(arrow.width(), 12);
    QCOMPARE(arrow.lineStyle(), LineStyle::Dashed);
    QVERIFY(arrow.boundingRect().height() > before.height()); // stroke width grows bounds
    QImage image(100, 40, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    arrow.draw(p);
    p.end();
    QCOMPARE(QColor(image.pixel(30, 10)).blue(), 255);
}
```

Shape: `ShapeAnnotation shape(QRect(10, 10, 60, 40), ShapeType::Rectangle, Qt::red, 2); shape.setColor(Qt::blue); shape.setWidth(6); shape.setFilled(true);` then assert getters and that pixel `(40, 30)` (inside) is blue when filled. Polyline: as arrow, with points `{(10,10),(50,10),(90,10)}`.

- [ ] **Step 2: Run to verify failure** — `./scripts/build.sh` → compile FAILS.

- [ ] **Step 3: Implement**

Each setter assigns the member and invalidates any cached geometry/bounds the class keeps. Read each `.cpp` for cached members (search for `mutable` in the header and for functions that compute from `m_width`); if a class caches a path or bounding rect, reset it in the setter the same way its existing geometry setters (`setStart`, `setRect`, `setPoint`) do. Example for `ArrowAnnotation`:

```cpp
void ArrowAnnotation::setColor(const QColor& color) { m_color = color; }
void ArrowAnnotation::setWidth(int width) { m_width = qMax(1, width); invalidateGeometryCache(); }
void ArrowAnnotation::setLineStyle(LineStyle style) { m_lineStyle = style; }
```

Use the class's real invalidation call (whatever `setEnd` calls); if it calls none, the setter needs none.

- [ ] **Step 4: Run tests** — `ctest --test-dir build -R "Annotations_(Shape|Arrow|Polyline)" --output-on-failure` → PASS.

- [ ] **Step 5: Commit**

```bash
git add include/annotations/{Shape,Arrow,Polyline}Annotation.h src/annotations/{Shape,Arrow,Polyline}Annotation.cpp tests/Annotations/tst_{Shape,Arrow,Polyline}Annotation.cpp
git commit -m "feat(annotations): colour, width, line-style and fill setters (#224)"
```

---

### Task 6: `Modify` history command and `selectionChanged`

**Files:**
- Modify: `include/annotations/AnnotationLayer.h`, `src/AnnotationLayer.cpp`
- Test: `tests/Annotations/tst_AnnotationLayer.cpp`

**Interfaces:**
- Produces:

```cpp
    // Applies `mutate` to the annotation with `id` as one undoable Modify command.
    // Consecutive calls with the same id and non-empty coalesceKey extend the open
    // command instead of pushing a new one. Returns false (and changes nothing) if
    // the id is unknown, the history is locked, or mutate returns false.
    bool modifyItem(AnnotationId id, const QString& coalesceKey,
                    const std::function<bool(AnnotationItem&)>& mutate);
    void endModifyCoalescing();
    AnnotationId idAt(int index) const;          // 0 if out of range
    int indexOfId(AnnotationId id) const;        // -1 if absent
    AnnotationId selectedId() const;             // 0 if none
signals:
    void selectionChanged();
```

`clearSelection()` moves from the header into the .cpp so it can emit.

- [ ] **Step 1: Write the failing tests**

```cpp
void tst_AnnotationLayer::modifyIsUndoable()
{
    AnnotationLayer layer;
    layer.addItem(std::make_unique<ArrowAnnotation>(QPoint(0, 0), QPoint(50, 0), Qt::red, 3));
    const auto id = layer.idAt(0);
    QVERIFY(layer.modifyItem(id, {}, [](AnnotationItem& item) {
        static_cast<ArrowAnnotation&>(item).setColor(Qt::blue);
        return true;
    }));
    auto* arrow = static_cast<ArrowAnnotation*>(layer.itemAt(0));
    QCOMPARE(arrow->color(), QColor(Qt::blue));
    layer.undo();
    QCOMPARE(static_cast<ArrowAnnotation*>(layer.itemAt(0))->color(), QColor(Qt::red));
    QCOMPARE(layer.itemCount(), size_t(1));
    QCOMPARE(layer.idAt(0), id);
    layer.redo();
    QCOMPARE(static_cast<ArrowAnnotation*>(layer.itemAt(0))->color(), QColor(Qt::blue));
    layer.undo();
    layer.undo(); // the Add
    QCOMPARE(layer.itemCount(), size_t(0));
}

void tst_AnnotationLayer::modifyKeepsSelectionAcrossUndo()
{
    AnnotationLayer layer;
    layer.addItem(std::make_unique<ArrowAnnotation>(QPoint(0, 0), QPoint(50, 0), Qt::red, 3));
    layer.setSelectedIndex(0);
    layer.modifyItem(layer.idAt(0), {}, [](AnnotationItem& i) {
        static_cast<ArrowAnnotation&>(i).setColor(Qt::blue); return true; });
    layer.undo();
    QCOMPARE(layer.selectedIndex(), 0);
}

void tst_AnnotationLayer::modifyCoalesces()
{
    AnnotationLayer layer;
    layer.addItem(std::make_unique<ArrowAnnotation>(QPoint(0, 0), QPoint(50, 0), Qt::red, 3));
    const auto id = layer.idAt(0);
    for (int w : {4, 5, 6})
        layer.modifyItem(id, QStringLiteral("width"), [w](AnnotationItem& i) {
            static_cast<ArrowAnnotation&>(i).setWidth(w); return true; });
    QCOMPARE(layer.historyCommandCount(), size_t(2)); // Add + one Modify
    layer.undo();
    QCOMPARE(static_cast<ArrowAnnotation*>(layer.itemAt(0))->width(), 3);
    // A different key starts a new command.
    layer.redo();
    layer.modifyItem(id, QStringLiteral("color"), [](AnnotationItem& i) {
        static_cast<ArrowAnnotation&>(i).setColor(Qt::green); return true; });
    QCOMPARE(layer.historyCommandCount(), size_t(3));
}

void tst_AnnotationLayer::modifyCoalescingEndsOnUndo()
{
    AnnotationLayer layer;
    layer.addItem(std::make_unique<ArrowAnnotation>(QPoint(0, 0), QPoint(50, 0), Qt::red, 3));
    const auto id = layer.idAt(0);
    layer.modifyItem(id, QStringLiteral("width"), [](AnnotationItem& i) {
        static_cast<ArrowAnnotation&>(i).setWidth(8); return true; });
    layer.undo();
    layer.modifyItem(id, QStringLiteral("width"), [](AnnotationItem& i) {
        static_cast<ArrowAnnotation&>(i).setWidth(9); return true; });
    QVERIFY(!layer.canRedo());
    layer.undo();
    QCOMPARE(static_cast<ArrowAnnotation*>(layer.itemAt(0))->width(), 3);
}

void tst_AnnotationLayer::modifyRefusedDuringErase()
{
    AnnotationLayer layer;
    layer.addItem(std::make_unique<ArrowAnnotation>(QPoint(0, 0), QPoint(50, 0), Qt::red, 3));
    layer.beginEraseTransaction();
    QVERIFY(!layer.modifyItem(layer.idAt(0), {}, [](AnnotationItem&) { return true; }));
    layer.endEraseTransaction();
}

void tst_AnnotationLayer::modifyRejectedMutationLeavesNoHistory()
{
    AnnotationLayer layer;
    layer.addItem(std::make_unique<ArrowAnnotation>(QPoint(0, 0), QPoint(50, 0), Qt::red, 3));
    QVERIFY(!layer.modifyItem(layer.idAt(0), {}, [](AnnotationItem&) { return false; }));
    QCOMPARE(layer.historyCommandCount(), size_t(1));
    QVERIFY(!layer.modifyItem(9999, {}, [](AnnotationItem&) { return true; }));
}

void tst_AnnotationLayer::selectionChangedSignal()
{
    AnnotationLayer layer;
    layer.addItem(std::make_unique<ArrowAnnotation>(QPoint(0, 0), QPoint(50, 0), Qt::red, 3));
    QSignalSpy spy(&layer, &AnnotationLayer::selectionChanged);
    layer.setSelectedIndex(0);
    layer.setSelectedIndex(0); // no change, no signal
    layer.clearSelection();
    QCOMPARE(spy.count(), 2);
    QCOMPARE(layer.selectedId(), AnnotationLayer::AnnotationId(0));
}
```

These tests use the setters added in Task 5.

- [ ] **Step 2: Run to verify failure**

Run: `./scripts/build.sh` → compile FAILS (`modifyItem` missing).

- [ ] **Step 3: Implement**

Header: add `Modify` to `HistoryCommandKind`; add member `QString m_openModifyKey; AnnotationId m_openModifyId = 0;`; declare the API above; move `clearSelection()` out of line.

`.cpp`:

```cpp
AnnotationLayer::AnnotationId AnnotationLayer::idAt(int index) const
{
    return (index >= 0 && index < int(m_items.size())) ? m_items[index].id : 0;
}

int AnnotationLayer::indexOfId(AnnotationId id) const
{
    const auto it = findSceneItem(id);
    return it == m_items.cend() ? -1 : int(std::distance(m_items.cbegin(), it));
}

AnnotationLayer::AnnotationId AnnotationLayer::selectedId() const
{
    return selectedIndex() >= 0 ? idAt(selectedIndex()) : 0;
}

void AnnotationLayer::endModifyCoalescing()
{
    m_openModifyId = 0;
    m_openModifyKey.clear();
}

bool AnnotationLayer::modifyItem(AnnotationId id, const QString& coalesceKey,
                                 const std::function<bool(AnnotationItem&)>& mutate)
{
    if (m_eraseTransactionActive || !mutate) return false;
    auto sceneIt = findSceneItem(id);
    if (sceneIt == m_items.end() || !sceneIt->item) return false;

    const bool extend = !coalesceKey.isEmpty() && m_openModifyId == id
        && m_openModifyKey == coalesceKey && !m_undoStack.empty()
        && m_undoStack.back().kind == HistoryCommandKind::Modify
        && m_undoStack.back().items.front().id == id;

    std::unique_ptr<AnnotationItem> before = extend ? nullptr : sceneIt->item->clone();
    if (!mutate(*sceneIt->item)) {
        // Mutators must not change the item when they return false.
        return false;
    }
    if (extend) {
        refreshCommandRetainedBytes(m_undoStack.back());
    } else {
        HistoryCommand command;
        command.kind = HistoryCommandKind::Modify;
        command.items.push_back({size_t(std::distance(m_items.begin(), sceneIt)),
                                 std::move(before), id});
        pushAppliedCommand(std::move(command));  // clears redo, trims history
        m_openModifyId = coalesceKey.isEmpty() ? 0 : id;
        m_openModifyKey = coalesceKey;
    }
    invalidateCache();
    emit changed();
    return true;
}
```

Note: `pushAppliedCommand` is also called by `addItemNow` and erase commits — call `endModifyCoalescing()` at the top of `pushAppliedCommand` *before* the new `Modify` sets the open id (so the order inside `modifyItem` is: push, then set open id — as written). Also call `endModifyCoalescing()` at the top of `undo()`, `redo()`, `clear()`, `replaceItems()`, and `beginEraseTransaction()`. Fix the byte accounting for the extend path: capture `before = m_undoStack.back().retainedBytes` before refreshing and call `adjustHistoryBytes(before, after)`.

In `undoCommand` and `redoCommand`, handle `Modify` first (the same swap both ways):

```cpp
    if (command.kind == HistoryCommandKind::Modify) {
        if (command.items.size() != 1 || !command.items.front().item) return false;
        auto sceneIt = findSceneItem(command.items.front().id);
        if (sceneIt == m_items.end()) return false;
        std::swap(sceneIt->item, command.items.front().item);
        return true;
    }
```

In `undo()`/`redo()`, keep the selection for Modify: before moving the command, read `const bool isModify = <stack>.back().kind == HistoryCommandKind::Modify; const AnnotationId modifiedId = isModify ? <stack>.back().items.front().id : 0;` and replace the unconditional `clearSelection();` with:

```cpp
    if (isModify && selectedId() == modifiedId) {
        // keep selection: the same annotation is still there with the other state
    } else {
        clearSelection();
    }
```

`selectedId()` is read after the swap; the swap keeps the id, so the check holds. `setSelectedIndex`: emit only when `m_selectedIndex` actually changes. `clearSelection()`: `if (m_selectedIndex != -1) { m_selectedIndex = -1; emit selectionChanged(); }`.

`calculateCommandRetainedBytes` and `visitCommandOwnedItems` already count/visit `items[].item`, so the stored clone is translated by `translateOwnedItems` and counted in the byte limit with no change. `trimHistory` treats commands generically — no change.

- [ ] **Step 4: Run tests**

Run: `./scripts/build.sh && ctest --test-dir build -R "Annotations_|EraserHostRouting|CropUndo" --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add include/annotations/AnnotationLayer.h src/AnnotationLayer.cpp tests/Annotations/tst_AnnotationLayer.cpp
git commit -m "feat(annotations): undoable Modify history command with coalescing (#224)"
```

---

### Task 7: `AnnotationStyle` — read and apply style values

**Files:**
- Create: `include/annotations/AnnotationStyle.h`, `src/annotations/AnnotationStyle.cpp`
- Modify: `CMakeLists.txt` (add the `.cpp` to `snaptray_core` next to `src/annotations/ShapeAnnotation.cpp`)
- Create: `tests/Annotations/tst_AnnotationStyle.cpp`; register in `tests/CMakeLists.txt` like `Annotations_AnnotationLayer`

**Interfaces:**
- Produces:

```cpp
enum class StyleProperty { Color, Width, Fill, ArrowStyle, LineStyle,
                           FontFamily, FontSize, Bold, Italic, Underline };

enum class StyleTargetKind { None, Text, Arrow, Polyline, Shape };

struct AnnotationStyleValues {
    StyleTargetKind kind = StyleTargetKind::None;
    QColor color;
    int width = 0;
    bool filled = false;
    int shapeType = 0;    // ShapeType, for the strip label
    int arrowStyle = 0;   // LineEndStyle
    int lineStyle = 0;    // LineStyle
    QFont font;
};

namespace AnnotationStyle {
StyleTargetKind kindOf(const AnnotationItem& item);
AnnotationStyleValues read(const AnnotationItem& item);
bool supports(StyleTargetKind kind, StyleProperty property);
// Returns false without changing the item when unsupported or unchanged.
bool apply(AnnotationItem& item, StyleProperty property, const QVariant& value);
}
```

Support table (a `constexpr` array indexed by kind, holding a bitmask of properties):

| Kind | Properties |
| --- | --- |
| Text | Color, FontFamily, FontSize, Bold, Italic, Underline |
| Arrow, Polyline | Color, Width, ArrowStyle, LineStyle |
| Shape | Color, Width, Fill |

- [ ] **Step 1: Write the failing tests**

```cpp
class tst_AnnotationStyle : public QObject {
    Q_OBJECT
private slots:
    void readsEachKind();
    void appliesSupportedProperties();
    void ignoresUnsupportedAndUnchanged();
};

void tst_AnnotationStyle::readsEachKind()
{
    ArrowAnnotation arrow(QPoint(0, 0), QPoint(10, 0), Qt::red, 4, LineEndStyle::BothArrow, LineStyle::Dotted);
    const auto a = AnnotationStyle::read(arrow);
    QCOMPARE(a.kind, StyleTargetKind::Arrow);
    QCOMPARE(a.color, QColor(Qt::red));
    QCOMPARE(a.width, 4);
    QCOMPARE(a.arrowStyle, int(LineEndStyle::BothArrow));
    QCOMPARE(a.lineStyle, int(LineStyle::Dotted));
    ShapeAnnotation shape(QRect(0, 0, 10, 10), ShapeType::Ellipse, Qt::green, 2, true);
    QCOMPARE(AnnotationStyle::read(shape).kind, StyleTargetKind::Shape);
    QVERIFY(AnnotationStyle::read(shape).filled);
    QCOMPARE(AnnotationStyle::read(shape).shapeType, int(ShapeType::Ellipse));
    QFont font("Helvetica", 18); font.setBold(true);
    TextBoxAnnotation text(QPointF(0, 0), "Hi", font, Qt::black);
    const auto t = AnnotationStyle::read(text);
    QCOMPARE(t.kind, StyleTargetKind::Text);
    QVERIFY(t.font.bold());
    EmojiStickerAnnotation emoji(QPoint(0, 0), QStringLiteral("😀"));
    QCOMPARE(AnnotationStyle::kindOf(emoji), StyleTargetKind::None);
}

void tst_AnnotationStyle::appliesSupportedProperties()
{
    ArrowAnnotation arrow(QPoint(0, 0), QPoint(10, 0), Qt::red, 4);
    QVERIFY(AnnotationStyle::apply(arrow, StyleProperty::Color, QColor(Qt::blue)));
    QVERIFY(AnnotationStyle::apply(arrow, StyleProperty::Width, 9));
    QVERIFY(AnnotationStyle::apply(arrow, StyleProperty::ArrowStyle, int(LineEndStyle::None)));
    QCOMPARE(arrow.color(), QColor(Qt::blue));
    QCOMPARE(arrow.width(), 9);
    QCOMPARE(arrow.lineEndStyle(), LineEndStyle::None);
    TextBoxAnnotation text(QPointF(0, 0), "Hi", QFont("Helvetica", 12), Qt::black);
    QVERIFY(AnnotationStyle::apply(text, StyleProperty::FontSize, 24));
    QVERIFY(AnnotationStyle::apply(text, StyleProperty::Italic, true));
    QCOMPARE(text.font().pointSize(), 24);
    QVERIFY(text.font().italic());
}

void tst_AnnotationStyle::ignoresUnsupportedAndUnchanged()
{
    ShapeAnnotation shape(QRect(0, 0, 10, 10), ShapeType::Rectangle, Qt::green, 2);
    QVERIFY(!AnnotationStyle::apply(shape, StyleProperty::ArrowStyle, 1));
    QVERIFY(!AnnotationStyle::apply(shape, StyleProperty::Color, QColor(Qt::green)));
    QVERIFY(!AnnotationStyle::supports(StyleTargetKind::None, StyleProperty::Color));
}
```

Check `EmojiStickerAnnotation`'s real constructor in its header and adjust that one line if it differs. Check how `TextBoxAnnotation` stores size (`pointSize` vs `pixelSize`) and how the existing text path (`AnnotationContext::applyTextFontSize`) sets size; use the same unit in both test and implementation.

- [ ] **Step 2: Run to verify failure** — build FAILS (header missing).

- [ ] **Step 3: Implement** `AnnotationStyle.cpp` with `dynamic_cast` per kind for `kindOf`, a `constexpr std::array<unsigned, 5>` support mask (bit = `1u << int(StyleProperty)`), `read()` filling the struct per kind, and `apply()`:

```cpp
bool AnnotationStyle::apply(AnnotationItem& item, StyleProperty property, const QVariant& value)
{
    const StyleTargetKind kind = kindOf(item);
    if (!supports(kind, property)) return false;
    const AnnotationStyleValues current = read(item);
    switch (kind) { // per-kind dispatch on a small closed enum; properties use the table above
    case StyleTargetKind::Text: return applyText(static_cast<TextBoxAnnotation&>(item), current, property, value);
    case StyleTargetKind::Arrow: return applyLine(static_cast<ArrowAnnotation&>(item), current, property, value);
    case StyleTargetKind::Polyline: return applyLine(static_cast<PolylineAnnotation&>(item), current, property, value);
    case StyleTargetKind::Shape: return applyShape(static_cast<ShapeAnnotation&>(item), current, property, value);
    case StyleTargetKind::None: return false;
    }
    return false;
}
```

`applyLine` is a template over Arrow/Polyline; each helper compares with `current` and returns false when the value is unchanged. Text font changes copy `current.font`, modify one attribute, then `setFont`.

- [ ] **Step 4: Run** — `ctest --test-dir build -R Annotations_AnnotationStyle --output-on-failure` → PASS.

- [ ] **Step 5: Commit**

```bash
git add include/annotations/AnnotationStyle.h src/annotations/AnnotationStyle.cpp CMakeLists.txt tests/Annotations/tst_AnnotationStyle.cpp tests/CMakeLists.txt
git commit -m "feat(annotations): read and apply style values per annotation type (#224)"
```

---

### Task 8: Selection-target mode in `PinToolOptionsViewModel` and the strip label

**Files:**
- Modify: `include/qml/PinToolOptionsViewModel.h`, `src/qml/PinToolOptionsViewModel.cpp`
- Modify: `src/qml/toolbar/ToolOptionsStrip.qml`
- Test: `tests/Qml/tst_PinToolOptionsViewModel.cpp`

**Interfaces:**
- Consumes: `AnnotationStyleValues`, `StyleProperty`, `StyleTargetKind` (Task 7).
- Produces:

```cpp
    Q_PROPERTY(QString targetLabel READ targetLabel NOTIFY targetChanged)
    Q_PROPERTY(bool editingSelection READ editingSelection NOTIFY targetChanged)
    QString targetLabel() const;            // "Selected: Arrow" / "New object"
    bool editingSelection() const;
    void showForSelection(const AnnotationStyleValues& values);
    void clearSelectionTarget();            // back to "New object"; caller re-runs showForTool
    // For host-side dropdown/dialog results: emits selectionStyleEdited and returns
    // true when a selection is targeted; otherwise returns false and does nothing.
    bool routeToSelection(StyleProperty property, const QVariant& value);
signals:
    void targetChanged();
    void selectionStyleEdited(int property, const QVariant& value); // property = int(StyleProperty)
```

Behaviour: while `editingSelection`, the QML-facing handlers (`handleColorClicked`, `handleWidthChanged`, `handleBoldToggled`, `handleItalicToggled`, `handleUnderlineToggled`, `handleArrowStyleSelected`, `handleLineStyleSelected`, `handleShapeFillModeSelected`) update the VM's own displayed value and emit `selectionStyleEdited` **instead of** their default signal (`colorSelected`, `widthValueChanged`, …). Dropdown *requests* still emit as today (the host shows the menu).

- [ ] **Step 1: Write the failing tests**

```cpp
void TestPinToolOptionsViewModel::selectionTargetRoutesEdits()
{
    PinToolOptionsViewModel vm;
    vm.showForTool(int(ToolId::Pencil));
    QCOMPARE(vm.targetLabel(), QStringLiteral("New object"));
    AnnotationStyleValues arrow;
    arrow.kind = StyleTargetKind::Arrow;
    arrow.color = Qt::blue;
    arrow.width = 7;
    arrow.arrowStyle = int(LineEndStyle::BothArrow);
    QSignalSpy edited(&vm, &PinToolOptionsViewModel::selectionStyleEdited);
    QSignalSpy defaultColor(&vm, &PinToolOptionsViewModel::colorSelected);
    vm.showForSelection(arrow);
    QVERIFY(vm.editingSelection());
    QCOMPARE(vm.targetLabel(), QStringLiteral("Selected: Arrow"));
    QVERIFY(vm.showColorSection());
    QVERIFY(vm.showWidthSection());
    QVERIFY(vm.showArrowStyleSection());
    QVERIFY(!vm.showTextSection());
    QCOMPARE(vm.currentColor(), QColor(Qt::blue));
    QCOMPARE(vm.currentWidth(), 7);
    QCOMPARE(vm.arrowStyle(), int(LineEndStyle::BothArrow));
    vm.handleWidthChanged(9);
    QCOMPARE(edited.count(), 1);
    QCOMPARE(edited.last().at(0).toInt(), int(StyleProperty::Width));
    QCOMPARE(edited.last().at(1).toInt(), 9);
    vm.handleColorClicked(0);
    QCOMPARE(defaultColor.count(), 0);
    QVERIFY(vm.routeToSelection(StyleProperty::LineStyle, int(LineStyle::Dashed)));
}

void TestPinToolOptionsViewModel::clearingTargetRestoresDefaults()
{
    PinToolOptionsViewModel vm;
    vm.showForTool(int(ToolId::Pencil));
    vm.setCurrentColor(Qt::red);
    vm.setCurrentWidth(3);
    AnnotationStyleValues shape;
    shape.kind = StyleTargetKind::Shape;
    shape.color = Qt::green;
    shape.width = 11;
    vm.showForSelection(shape);
    QCOMPARE(vm.targetLabel(), QStringLiteral("Selected: Rectangle")); // see note
    vm.clearSelectionTarget();
    QVERIFY(!vm.editingSelection());
    QCOMPARE(vm.currentColor(), QColor(Qt::red));
    QCOMPARE(vm.currentWidth(), 3);
    QSignalSpy defaultColor(&vm, &PinToolOptionsViewModel::colorSelected);
    vm.handleColorClicked(0);
    QCOMPARE(defaultColor.count(), 1);
    QVERIFY(!vm.routeToSelection(StyleProperty::Color, QColor(Qt::blue)));
}
```

The label uses `shapeType` for shapes ("Rectangle"/"Ellipse") and says "Line" for polylines.

- [ ] **Step 2: Run to verify failure** — build FAILS.

- [ ] **Step 3: Implement**

- Members: `bool m_editingSelection = false; StyleTargetKind m_targetKind = StyleTargetKind::None; int m_targetShapeType = 0;` and a saved-defaults struct `{QColor color; int width; bool bold, italic, underline; int fontSize; QString fontFamily; int arrowStyle, lineStyle, shapeFillMode;}` captured on entering selection mode and restored by `clearSelectionTarget()` (restore via the existing setters so QML updates, without emitting the action signals).
- Section table for selection kinds (lookup array, not a switch): Text → color+text; Arrow/Polyline → color+width+arrowStyle+lineStyle; Shape → color+width+shape (the Shape section shows the fill choice; hide its type buttons with a new `Q_PROPERTY(bool shapeTypeEditable ...)` that is false in selection mode, and bind the type `Repeater`'s `visible` in `ShapeSection.qml` to it).
- `targetLabel()`: `m_editingSelection ? tr("Selected: %1").arg(typeName) : tr("New object")`, type names from a table: `tr("Text")`, `tr("Arrow")`, `tr("Line")`, `tr("Rectangle")`/`tr("Ellipse")`.
- In each listed handler, add at the top, e.g. for width:

```cpp
void PinToolOptionsViewModel::handleWidthChanged(int width)
{
    if (m_editingSelection) {
        setCurrentWidth(width);
        emit selectionStyleEdited(int(StyleProperty::Width), width);
        return;
    }
    // ... existing body unchanged
}
```

  Do the same for color (`StyleProperty::Color`, value `QColor`), bold/italic/underline (toggle the VM value, emit the new bool), arrow style, line style, fill mode (`StyleProperty::Fill`, value `mode == int(ShapeFillMode::Filled)`).
- `routeToSelection` updates the matching displayed VM value (same setters) and emits.

`ToolOptionsStrip.qml`: add a leading label as the first child of `sectionRow`:

```qml
        Text {
            id: targetLabel
            objectName: "toolOptionsTargetLabel"
            visible: root.hasViewModel && root.viewModel.hasContent
            text: root.hasViewModel ? root.viewModel.targetLabel : ""
            color: root.hasViewModel && root.viewModel.editingSelection
                   ? ComponentTokens.toolbarIconActive : ComponentTokens.toolbarIcon
            font.pixelSize: 11
            font.weight: root.hasViewModel && root.viewModel.editingSelection ? Font.DemiBold : Font.Normal
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
```

and set `Accessible.name` on the strip root to the same `targetLabel`. The label counts toward `contentWidth` automatically (it is a visible child of `sectionRow`).

- [ ] **Step 4: Run** — `ctest --test-dir build -R "Qml_PinToolOptionsViewModel|Annotations_AnnotationStyle|FloatingSubToolbar|MultiRegionSubToolbar" --output-on-failure` → PASS.

- [ ] **Step 5: Commit**

```bash
git add include/qml/PinToolOptionsViewModel.h src/qml/PinToolOptionsViewModel.cpp src/qml/toolbar/ToolOptionsStrip.qml src/qml/toolbar/ShapeSection.qml tests/Qml/tst_PinToolOptionsViewModel.cpp
git commit -m "feat(toolbar): style strip targets the selected annotation and says so (#224)"
```

---

### Task 9: `AnnotationStyleEditor` — glue, coalescing, selection tracking

**Files:**
- Create: `include/annotation/AnnotationStyleEditor.h`, `src/annotation/AnnotationStyleEditor.cpp`
- Modify: `CMakeLists.txt` (add to `snaptray_ui` after `src/annotation/AnnotationContext.cpp`)
- Create: `tests/Annotations/tst_AnnotationStyleEditor.cpp`; register in `tests/CMakeLists.txt` linking `snaptray_ui Qt6::Widgets Qt6::Test`

**Interfaces:**
- Consumes: `AnnotationLayer::modifyItem/endModifyCoalescing/selectedId/indexOfId/selectionChanged` (Task 6), `AnnotationStyle` (Task 7), `PinToolOptionsViewModel::showForSelection/clearSelectionTarget/selectionStyleEdited` (Task 8).
- Produces:

```cpp
class AnnotationStyleEditor : public QObject {
    Q_OBJECT
public:
    static constexpr int kCoalesceIdleMs = 500;
    AnnotationStyleEditor(AnnotationLayer* layer, PinToolOptionsViewModel* viewModel,
                          QObject* parent = nullptr);
    // Host predicate: true while inline text editing owns styling (no selection target then).
    void setInlineEditingPredicate(std::function<bool()> isInlineEditing);
    // Re-evaluates the target from the layer's selection (call after host-side state changes).
    void refreshTarget();
    bool hasTarget() const;
signals:
    void styleApplied();      // host repaints
    void targetCleared();     // host re-runs its normal showForTool path
    void targetShown();       // host makes sure the strip is visible
private:
    void onStyleEdited(int property, const QVariant& value);
    AnnotationLayer* m_layer;
    PinToolOptionsViewModel* m_viewModel;
    AnnotationLayer::AnnotationId m_targetId = 0;
    QTimer m_idle;            // single-shot, kCoalesceIdleMs → m_layer->endModifyCoalescing()
    std::function<bool()> m_isInlineEditing;
};
```

- [ ] **Step 1: Write the failing tests**

```cpp
void tst_AnnotationStyleEditor::editsSelectedArrowWithUndo()
{
    AnnotationLayer layer;
    PinToolOptionsViewModel vm;
    AnnotationStyleEditor editor(&layer, &vm);
    layer.addItem(std::make_unique<ArrowAnnotation>(QPoint(0, 0), QPoint(50, 0), Qt::red, 3));
    layer.setSelectedIndex(0);
    QVERIFY(editor.hasTarget());
    QCOMPARE(vm.targetLabel(), QStringLiteral("Selected: Arrow"));
    vm.handleWidthChanged(5);
    vm.handleWidthChanged(6);
    vm.handleWidthChanged(7);
    QCOMPARE(static_cast<ArrowAnnotation*>(layer.itemAt(0))->width(), 7);
    QCOMPARE(layer.historyCommandCount(), size_t(2)); // coalesced
    layer.undo();
    QCOMPARE(static_cast<ArrowAnnotation*>(layer.itemAt(0))->width(), 3);
    QCOMPARE(vm.currentWidth(), 3);  // strip refreshed after undo
}

void tst_AnnotationStyleEditor::idleEndsCoalescing()
{
    AnnotationLayer layer;
    PinToolOptionsViewModel vm;
    AnnotationStyleEditor editor(&layer, &vm);
    layer.addItem(std::make_unique<ArrowAnnotation>(QPoint(0, 0), QPoint(50, 0), Qt::red, 3));
    layer.setSelectedIndex(0);
    vm.handleWidthChanged(5);
    QTest::qWait(AnnotationStyleEditor::kCoalesceIdleMs + 100);
    vm.handleWidthChanged(6);
    QCOMPARE(layer.historyCommandCount(), size_t(3));
}

void tst_AnnotationStyleEditor::deselectRestoresDefaults()
{
    AnnotationLayer layer;
    PinToolOptionsViewModel vm;
    vm.showForTool(int(ToolId::Arrow));
    vm.setCurrentWidth(2);
    AnnotationStyleEditor editor(&layer, &vm);
    QSignalSpy cleared(&editor, &AnnotationStyleEditor::targetCleared);
    layer.addItem(std::make_unique<ArrowAnnotation>(QPoint(0, 0), QPoint(50, 0), Qt::red, 9));
    layer.setSelectedIndex(0);
    layer.clearSelection();
    QCOMPARE(cleared.count(), 1);
    QVERIFY(!vm.editingSelection());
    QCOMPARE(vm.currentWidth(), 2);
}

void tst_AnnotationStyleEditor::targetFollowsIdAfterIndexShift()
{
    AnnotationLayer layer;
    PinToolOptionsViewModel vm;
    AnnotationStyleEditor editor(&layer, &vm);
    layer.addItem(std::make_unique<ArrowAnnotation>(QPoint(0, 0), QPoint(50, 0), Qt::red, 3));
    layer.addItem(std::make_unique<ArrowAnnotation>(QPoint(0, 20), QPoint(50, 20), Qt::red, 3));
    layer.setSelectedIndex(1);
    const auto id = layer.idAt(1);
    // Erase the first arrow; the selected one moves to index 0.
    layer.beginEraseTransaction();
    auto removed = layer.removeItemsIntersecting(QPoint(25, 0), 4);
    layer.commitEraseTransaction(std::move(removed));
    layer.setSelectedIndex(layer.indexOfId(id));
    vm.handleColorClicked(1);
    const QColor expected = vm.colorPalette().at(1).value<QColor>();
    QCOMPARE(static_cast<ArrowAnnotation*>(layer.itemAt(layer.indexOfId(id)))->color(), expected);
}

void tst_AnnotationStyleEditor::inlineTextEditingBypassesSelectionTarget()
{
    AnnotationLayer layer;
    PinToolOptionsViewModel vm;
    AnnotationStyleEditor editor(&layer, &vm);
    bool editing = true;
    editor.setInlineEditingPredicate([&editing] { return editing; });
    layer.addItem(std::make_unique<TextBoxAnnotation>(QPointF(0, 0), "Hi", QFont(), Qt::black));
    layer.setSelectedIndex(0);
    QVERIFY(!editor.hasTarget());
    QVERIFY(!vm.editingSelection());
    editing = false;
    editor.refreshTarget();
    QVERIFY(editor.hasTarget());
}

void tst_AnnotationStyleEditor::nonStylableSelectionHasNoTarget()
{
    AnnotationLayer layer;
    PinToolOptionsViewModel vm;
    AnnotationStyleEditor editor(&layer, &vm);
    layer.addItem(std::make_unique<EmojiStickerAnnotation>(QPoint(0, 0), QStringLiteral("😀")));
    layer.setSelectedIndex(0);
    QVERIFY(!editor.hasTarget());
}
```

Check the erase call signature in `AnnotationLayer.h` (`removeItemsIntersecting(const QPoint&, int)`) and that an arrow at y=0 is hit at `(25, 0)` with width 4; adjust the point if the hit test needs the stroke centre.

- [ ] **Step 2: Run to verify failure** — build FAILS.

- [ ] **Step 3: Implement**

```cpp
AnnotationStyleEditor::AnnotationStyleEditor(AnnotationLayer* layer, PinToolOptionsViewModel* vm,
                                             QObject* parent)
    : QObject(parent), m_layer(layer), m_viewModel(vm)
{
    m_idle.setSingleShot(true);
    m_idle.setInterval(kCoalesceIdleMs);
    connect(&m_idle, &QTimer::timeout, this, [this] { m_layer->endModifyCoalescing(); });
    connect(m_layer, &AnnotationLayer::selectionChanged, this, &AnnotationStyleEditor::refreshTarget);
    connect(m_layer, &AnnotationLayer::changed, this, [this] {
        // Undo/redo/other edits: keep the strip's values in sync with the target.
        if (m_targetId && m_layer->selectedId() == m_targetId)
            if (auto* item = m_layer->itemAt(m_layer->indexOfId(m_targetId)))
                m_viewModel->showForSelection(AnnotationStyle::read(*item));
    });
    connect(m_viewModel, &PinToolOptionsViewModel::selectionStyleEdited,
            this, &AnnotationStyleEditor::onStyleEdited);
}

void AnnotationStyleEditor::refreshTarget()
{
    m_layer->endModifyCoalescing();
    m_idle.stop();
    const auto id = m_layer->selectedId();
    AnnotationItem* item = id ? m_layer->itemAt(m_layer->indexOfId(id)) : nullptr;
    const bool stylable = item && AnnotationStyle::kindOf(*item) != StyleTargetKind::None
                          && !(m_isInlineEditing && m_isInlineEditing());
    if (stylable) {
        m_targetId = id;
        m_viewModel->showForSelection(AnnotationStyle::read(*item));
        emit targetShown();
        return;
    }
    if (m_targetId) {
        m_targetId = 0;
        m_viewModel->clearSelectionTarget();
        emit targetCleared();
    }
}

void AnnotationStyleEditor::onStyleEdited(int property, const QVariant& value)
{
    if (!m_targetId) return;
    const auto prop = static_cast<StyleProperty>(property);
    const QString key = QString::number(property);
    if (m_layer->modifyItem(m_targetId, key, [prop, &value](AnnotationItem& item) {
            return AnnotationStyle::apply(item, prop, value);
        })) {
        m_idle.start();
        emit styleApplied();
    }
}
```

`hasTarget()` returns `m_targetId != 0`. Note `showForSelection` must not emit `selectionStyleEdited` (it only sets displayed values) — otherwise the `changed` handler would loop. Add `QSignalBlocker`-free protection by making `showForSelection` use the plain setters only (Task 8 already does).

- [ ] **Step 4: Run** — `ctest --test-dir build -R "Annotations_AnnotationStyleEditor" --output-on-failure` → PASS.

- [ ] **Step 5: Commit**

```bash
git add include/annotation/AnnotationStyleEditor.h src/annotation/AnnotationStyleEditor.cpp CMakeLists.txt tests/Annotations/tst_AnnotationStyleEditor.cpp tests/CMakeLists.txt
git commit -m "feat(annotations): AnnotationStyleEditor applies strip edits to the selection (#224)"
```

---

### Task 10: Wire the editor into Region Selector

**Files:**
- Modify: `include/RegionSelector.h`, `src/RegionSelector.cpp`
- Test: `tests/RegionSelector/tst_StyleSync.cpp`

**Interfaces:**
- Consumes: `AnnotationStyleEditor` (Task 9), `PinToolOptionsViewModel::routeToSelection` (Task 8).

- [ ] **Step 1: Write the failing test**

```cpp
void TestRegionSelectorStyleSync::selectedArrowStyleIsUndoable()
{
    ScopedWidthSettings restoreWidths;
    auto& settings = AnnotationSettingsManager::instance();
    const QColor savedColor = settings.loadColor();
    const auto restore = qScopeGuard([&] { settings.saveColor(savedColor); });
    RegionSelector selector;
    RegionSelectorTestAccess::setSelectionRect(selector, kSelectionRect);
    selector.m_annotationLayer->addItem(
        std::make_unique<ArrowAnnotation>(QPoint(60, 60), QPoint(150, 60), Qt::red, 3));
    selector.m_annotationLayer->setSelectedIndex(0);
    auto* vm = selector.m_toolOptionsViewModel;
    QVERIFY(vm->editingSelection());
    const int paletteIndex = 2;
    const QColor chosen = vm->colorPalette().at(paletteIndex).value<QColor>();
    vm->handleColorClicked(paletteIndex);
    auto arrow = [&] { return static_cast<ArrowAnnotation*>(selector.m_annotationLayer->itemAt(0)); };
    QCOMPARE(arrow()->color(), chosen);
    QCOMPARE(settings.loadColor(), savedColor); // defaults untouched
    selector.m_annotationLayer->undo();
    QCOMPARE(arrow()->color(), QColor(Qt::red));
    selector.m_annotationLayer->redo();
    QCOMPARE(arrow()->color(), chosen);
    selector.m_annotationLayer->clearSelection();
    QVERIFY(!vm->editingSelection());
}
```

Use the selector's real undo entry point if one exists (search `RegionSelector.cpp` for the Undo handler that calls `m_annotationLayer->undo()` and call that instead, so canUndo/redo toolbar state is exercised too).

- [ ] **Step 2: Run to verify failure** — `ctest --test-dir build -R RegionSelector_StyleSync --output-on-failure` → FAIL (`editingSelection()` false).

- [ ] **Step 3: Implement**

In `RegionSelector.h` add `AnnotationStyleEditor* m_styleEditor = nullptr;` (Qt-parented). In the constructor after `m_toolOptionsViewModel` and `m_annotationLayer` exist (near line 516 where the VM signals are connected):

```cpp
    m_styleEditor = new AnnotationStyleEditor(m_annotationLayer, m_toolOptionsViewModel, this);
    m_styleEditor->setInlineEditingPredicate([this] { return m_textEditor && m_textEditor->isEditing(); });
    connect(m_styleEditor, &AnnotationStyleEditor::styleApplied, this, [this] {
        requestCaptureSceneUpdate();
        updateUndoRedoState();   // use the existing function that syncs canUndo/canRedo
    });
    connect(m_styleEditor, &AnnotationStyleEditor::targetShown, this, [this] {
        if (m_qmlSubToolbar) m_qmlSubToolbar->show();
        syncRegionSubToolbar(false);
    });
    connect(m_styleEditor, &AnnotationStyleEditor::targetCleared, this, [this] {
        syncRegionSubToolbar(true);
    });
```

(Find the real name of the canUndo/canRedo sync function by searching for `setCanUndo(` in `RegionSelector.cpp`; call that.) In `syncRegionSubToolbar`, when `m_styleEditor->hasTarget()` is true, skip the `showForTool` refresh and keep the strip visible:

```cpp
    if (m_styleEditor && m_styleEditor->hasTarget()) {
        if (!m_qmlSubToolbar->isVisible()) m_qmlSubToolbar->show();
        if (m_qmlToolbar && m_qmlToolbar->isVisible())
            m_qmlSubToolbar->positionBelow(m_qmlToolbar->geometry());
        return;
    }
```

placed after the `allowSubToolbar` check. Route the host-side results through the selection first — at the top of `onFontSizeSelected`, `onFontFamilySelected`, the arrow/line style menu result handlers (search for the slots `RegionSettingsHelper` emits on selection, e.g. `arrowStyleSelected`/`lineStyleSelected` connections), and the custom colour picker callback in `onMoreColorsRequested`:

```cpp
    if (m_toolOptionsViewModel && m_toolOptionsViewModel->routeToSelection(StyleProperty::FontSize, size))
        return;
```

with the matching `StyleProperty` and value type for each. When text inline editing starts or ends, call `m_styleEditor->refreshTarget()` (connect to `TextAnnotationEditor`'s editing-started/finished signals; search its header for them).

- [ ] **Step 4: Run** — `ctest --test-dir build -R "RegionSelector_" --output-on-failure` → PASS.

- [ ] **Step 5: Commit**

```bash
git add include/RegionSelector.h src/RegionSelector.cpp tests/RegionSelector/tst_StyleSync.cpp
git commit -m "feat(region): edit the selected annotation's style from the strip (#224)"
```

---

### Task 11: Wire the editor into Pin Window

**Files:**
- Modify: `include/PinWindow.h`, `src/PinWindow.cpp`
- Test: `tests/PinWindow/tst_StyleSync.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
void TestPinWindowStyleSync::selectedArrowStyleIsUndoable()
{
    auto& settings = AnnotationSettingsManager::instance();
    const QColor savedColor = settings.loadColor();
    const auto restore = qScopeGuard([&] { settings.saveColor(savedColor); });
    QPixmap pixmap(300, 200);
    pixmap.fill(Qt::white);
    PinWindow window(pixmap, QPoint(100, 100));
    window.enterAnnotationMode();          // use the real entry used by other tests in this file
    window.m_annotationLayer->addItem(
        std::make_unique<ArrowAnnotation>(QPoint(20, 20), QPoint(120, 20), Qt::red, 3));
    window.m_annotationLayer->setSelectedIndex(0);
    auto* vm = window.m_subToolbar->viewModel();
    QVERIFY(vm->editingSelection());
    vm->handleWidthChanged(11);
    auto arrow = [&] { return static_cast<ArrowAnnotation*>(window.m_annotationLayer->itemAt(0)); };
    QCOMPARE(arrow()->width(), 11);
    window.m_annotationLayer->undo();
    QCOMPARE(arrow()->width(), 3);
    QCOMPARE(settings.loadColor(), savedColor);
}
```

Match the `PinWindow` constructor and the annotation-mode entry to what the other tests in `tests/PinWindow/tst_StyleSync.cpp` already do (copy their setup lines verbatim).

- [ ] **Step 2: Run to verify failure** — `ctest --test-dir build -R PinWindow_StyleSync --output-on-failure` → FAIL.

- [ ] **Step 3: Implement** — same pattern as Task 10, in the block that connects `optionsVM` signals (`src/PinWindow.cpp` ~line 3443): create `m_styleEditor = new AnnotationStyleEditor(m_annotationLayer, optionsVM, this);`, predicate `m_textAnnotationEditor && m_textAnnotationEditor->isEditing()`, `styleApplied` → `update()` plus the existing undo/redo toolbar sync (search `setCanUndo(`), `targetShown` → `m_subToolbar->show()`, `targetCleared` → re-run the current tool's `m_subToolbar->showForTool(int(m_currentToolId))` (or hide if not in annotation mode). Guard the existing tool-change path (`handleToolbarToolSelected`) so selecting a tool first clears the layer selection (`m_annotationLayer->clearSelection()`), which ends targeting through the signal. Add `routeToSelection` guards to the font size/family and arrow/line style dropdown result handlers and the custom colour callback, as in Task 10.

- [ ] **Step 4: Run** — `ctest --test-dir build -R "PinWindow_" --output-on-failure` → PASS.

- [ ] **Step 5: Commit**

```bash
git add include/PinWindow.h src/PinWindow.cpp tests/PinWindow/tst_StyleSync.cpp
git commit -m "feat(pin): edit the selected annotation's style from the strip (#224)"
```

---

### Task 12: Wire the editor into Screen Canvas

**Files:**
- Modify: `include/ScreenCanvasSession.h`, `src/ScreenCanvasSession.cpp`
- Test: `tests/ScreenCanvas/tst_StyleSync.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
void TestScreenCanvasStyleSync::selectedArrowStyleIsUndoable()
{
    auto& settings = AnnotationSettingsManager::instance();
    const QColor savedColor = settings.loadColor();
    const auto restore = qScopeGuard([&] { settings.saveColor(savedColor); });
    ScreenCanvasSession session;
    auto* surface = new ScreenCanvas;
    session.m_surfaces = {surface};
    session.m_activeSurface = surface;
    session.m_annotationLayer->addItem(
        std::make_unique<ArrowAnnotation>(QPoint(20, 20), QPoint(120, 20), Qt::red, 3));
    session.m_annotationLayer->setSelectedIndex(0);
    QVERIFY(session.m_toolOptionsViewModel->editingSelection());
    session.m_toolOptionsViewModel->handleArrowStyleSelected(int(LineEndStyle::None));
    auto arrow = [&] { return static_cast<ArrowAnnotation*>(session.m_annotationLayer->itemAt(0)); };
    QCOMPARE(arrow()->lineEndStyle(), LineEndStyle::None);
    session.m_annotationLayer->undo();
    QCOMPARE(arrow()->lineEndStyle(), LineEndStyle::EndArrow);
    QCOMPARE(settings.loadColor(), savedColor);
}
```

- [ ] **Step 2: Run to verify failure** — FAIL.

- [ ] **Step 3: Implement** — same pattern: create the editor where the session connects `m_toolOptionsViewModel` signals; predicate checks the active surface's `inlineTextEditor()->isEditing()`; `styleApplied` → `updateAllSurfaces()` plus `updateQmlToolbarState()`; `targetShown` → `m_qmlSubToolbar->show()`; `targetCleared` → `m_qmlSubToolbar->showForTool(int(m_currentToolId))`. Add `routeToSelection` guards to the dropdown/colour-dialog result paths (`onColorSelected` is used by the custom colour dialog callback — guard there with `StyleProperty::Color`; it is safe because palette clicks no longer reach it in selection mode).

- [ ] **Step 4: Run** — `ctest --test-dir build -R "ScreenCanvas_" --output-on-failure` → PASS.

- [ ] **Step 5: Commit**

```bash
git add include/ScreenCanvasSession.h src/ScreenCanvasSession.cpp tests/ScreenCanvas/tst_StyleSync.cpp
git commit -m "feat(canvas): edit the selected annotation's style from the strip (#224)"
```

---

### Task 13: Translations and user docs

**Files:**
- Modify: `translations/snaptray_*.ts` (24 files)
- Modify: `docs/docs/annotation-tools.md`, `docs/zh-tw/docs/annotation-tools.md` (if the page that describes the toolbar is different, use that page; search `docs/docs` for "toolbar")

- [ ] **Step 1: Regenerate sources**

Run: `cmake --build build --target update_translations`
Expected: new unfinished entries for `Selected: %1`, `New object`, `Text`, `Arrow`, `Line`, `Rectangle`, `Ellipse`, `More` (if changed). Keep vanished entries unmarked (the repo ships them as normal entries); do not delete them.

- [ ] **Step 2: Translate**

Fill every new `type="unfinished"` entry in all 24 files and remove the `type` attribute. Keep `%1` in every translation. Reuse an existing translation of the same English word in the same file when one exists (search the file first) so terms stay consistent.

- [ ] **Step 3: Verify**

Run: `grep -c 'type="unfinished"' translations/*.ts` → all `0`. Run: `./scripts/build.sh && ctest --test-dir build -R "Translations|Settings_QmlTranslations|App_MainApplicationTrayMenu" --output-on-failure` → PASS.

- [ ] **Step 4: User docs**

Add to the annotation page (English and zh-tw) a short subsection:

```markdown
### Changing an annotation you already drew

Select a text, arrow, line, or shape annotation. The style bar then says **Selected: Arrow** (or the type you picked), and colour, width, and style changes apply to that annotation. Press **Ctrl+Z** (**Cmd+Z** on macOS) to undo a change. With nothing selected the bar says **New object**, and changes apply to the next annotation you draw.

When the toolbar does not fit, less-used buttons move into **More** (⋯). Copy, Save, Pin, Undo, and Redo always stay on the toolbar.
```

zh-tw uses the translated UI labels from `snaptray_zh_TW.ts`.

- [ ] **Step 5: Commit**

```bash
git add translations docs/docs docs/zh-tw/docs
git commit -m "docs(i18n): selected-object style and More menu in 24 languages and user docs (#224)"
```

---

### Task 14: Evidence screenshots, operation video, design guidelines

**Files:**
- Create: `tests/Qml/tst_ToolbarEvidence.cpp`; register in `tests/CMakeLists.txt` like `Qml_ToolbarOverflow` (labels `"unit"`)
- Create: `docs/assets/images/developer/toolbar/*.png`, `docs/assets/images/developer/toolbar/videos/toolbar-selected-style.{gif,mp4}`
- Modify: `docs/developer/ui-design-guidelines.md`

**Interfaces:**
- Env vars: `SNAPTRAY_TOOLBAR_DESIGN_DIR` (screenshots), `SNAPTRAY_TOOLBAR_TRANSLATION` (.qm path), `SNAPTRAY_TOOLBAR_VIDEO_DIR` (frames). Without them every slot `QSKIP`s.

- [ ] **Step 1: Write the evidence test**

Follow `tests/Qml/tst_RecordingEditor.cpp`'s `designEvidence` and `FrameRecorder` (read both first; reuse `FrameRecorder` by moving it to `tests/Qml/FrameRecorder.h` if it is file-local there, and include it from both tests). Slots:

- `toolbarStates_data/toolbarStates`: rows `normal` (1920 wide), `narrow` (300 wide, More menu open), `selected` (strip with an arrow selected), each × `light`/`dark` via `ThemeManager`; grab `QQuickWindow::grabWindow()` of the toolbar + strip and save `NN-<row>-<theme>.png`.
- `longTranslation`: load `SNAPTRAY_TOOLBAR_TRANSLATION` with `QTranslator`, render `selected` and `narrow` in light, save `20-long-translation-*.png`.
- `screenScale`: grab the `selected` state at the window's `devicePixelRatio()` and put it in the file name (`30-dpr-<n>.png`). On a Retina Mac this is 2×; other scales are left to the manual Windows/Ubuntu pass.
- `operationVideo`: select arrow → change colour → change width → Ctrl+Z → Ctrl+Shift+Z, recording frames with captions.

- [ ] **Step 2: Generate**

```bash
mkdir -p /tmp/toolbar-evidence
SNAPTRAY_TOOLBAR_DESIGN_DIR=docs/assets/images/developer/toolbar ctest --test-dir build -R Qml_ToolbarEvidence --output-on-failure
SNAPTRAY_TOOLBAR_DESIGN_DIR=docs/assets/images/developer/toolbar SNAPTRAY_TOOLBAR_TRANSLATION=build/translations/snaptray_de.qm ctest --test-dir build -R Qml_ToolbarEvidence --output-on-failure
SNAPTRAY_TOOLBAR_VIDEO_DIR=/tmp/toolbar-evidence ctest --test-dir build -R Qml_ToolbarEvidence --output-on-failure
ffmpeg -y -framerate 10 -i /tmp/toolbar-evidence/frame-%04d.png -pix_fmt yuv420p docs/assets/images/developer/toolbar/videos/toolbar-selected-style.mp4
ffmpeg -y -i docs/assets/images/developer/toolbar/videos/toolbar-selected-style.mp4 -vf "fps=8,scale=960:-1:flags=lanczos" docs/assets/images/developer/toolbar/videos/toolbar-selected-style.gif
```

(Use the `.qm` path the build actually produces; find it with `find build -name 'snaptray_de.qm'`.) Open every PNG and check: no clipped labels, More button visible on narrow, tint on Copy/Save/Pin, label reads "Selected: Arrow".

- [ ] **Step 3: Design guidelines**

Add a "Capture toolbar" section to `docs/developer/ui-design-guidelines.md` after "Regenerating the evidence": the three groups, collapse order, the "Selected / New object" contract, an acceptance table with columns macOS / Windows / Ubuntu X11 (macOS: the screenshots and video above; Windows and Ubuntu: "Pending manual pass"), the images, and the regeneration commands from Step 2. Replace the sentence in "Rollout boundaries" that says capture/annotation grouping is still governed only by the spec with one that points to this section.

- [ ] **Step 4: Full verification**

Run: `./scripts/run-tests.sh`
Expected: all pass. If `tst_RecordingCropOverlay::previewNoHighlightOverExistingDraft` fails, re-run it alone three times and record the result (known flaky from #228).

- [ ] **Step 5: Commit**

```bash
git add tests/Qml/tst_ToolbarEvidence.cpp tests/Qml/FrameRecorder.h tests/Qml/tst_RecordingEditor.cpp tests/CMakeLists.txt docs/assets/images/developer/toolbar docs/developer/ui-design-guidelines.md
git commit -m "docs(toolbar): capture toolbar evidence and acceptance matrix (#224)"
```

---

## After the tasks

- Ask the user before: pushing the branch, opening the PR (`Closes #224`), and opening the follow-up issue "Undo for move, resize, rotate, and text re-edit of existing annotations" (reuses `Modify`).
