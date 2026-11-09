# Undoable Annotation Geometry Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Moving, rotating, scaling, dragging endpoints/vertices and re-editing text become one undo step each, on Region Capture, Pin Window and Screen Canvas; Escape during a drag restores the annotation.

**Architecture:**
- **Copying geometry.** A new `AnnotationGeometry` table copies and compares geometry fields per annotation kind, mirroring `AnnotationStyle`.
- **Modify parts.** `AnnotationLayer`'s `Modify` command gains a `parts` mask (Style / Geometry). Undo and redo exchange only those parts.
- **Gestures.** A gesture API (`beginGesture` / `commitGesture` / `cancelGesture`) snapshots the item on the first mutation and records one command on release.
- **Hosts.** The shared Text/Shape editors and the three per-surface Emoji/Arrow/Polyline copies call that API. Each surface gets one `cancelAnnotationGesture()` that Escape (and Region's right-click) calls first.

**Tech Stack:** Qt 6.12, C++17, Qt Test, CMake/Ninja. Build with `./scripts/build.sh`, test with `./scripts/run-tests.sh`.

**Spec:** `docs/superpowers/specs/2026-11-05-annotation-geometry-undo-design.md`

## Global Constraints

- Geometry undo restores geometry only, and style undo restores style only. Never swap the whole object (#236 review).
- Visibility is never part of history. The text re-edit flow owns it.
- Undo and redo do nothing while a gesture is active; `canUndo()` and `canRedo()` are false then.
- Respect the erase-transaction lock, `kMaxHistoryCommands`, `kMaxHistoryOwnedBytes`, and Pin's `matchesCropAnnotationBoundary` ordering. Commits go through `pushAppliedCommand`.
- Use lookup tables, not repetitive `switch` chains (CLAUDE.md). Use `nullptr`, and prefer `QPoint`/`QRect`.
- No new UI strings.
- Do not merge the triplicated Emoji/Arrow/Polyline code. Leave `SelectionToolHandler` alone.

## Review Focus

1. **A gesture that never ends** (mouse released outside the window, focus lost, window closed mid-drag) must not block undo forever.
   - Expected: the next `beginGesture` commits the stale one.
   - `clear()`, `replaceItems()`, `beginEraseTransaction()` and removing the item drop it.
2. **Text re-edit that ends with empty text, or ends while the item was erased or undone underneath.**
   - Expected: no command, no crash, and the text stays visible if it still exists.
3. **Pin crop while history holds geometry commands.**
   - Expected: undoing a move after a crop lands at the cropped coordinates, because stored clones are translated by `translateAll`.
4. **Arrow endpoint drag:** `setStart`/`setEnd` shift the control point.
   - Expected: undo restores the exact control point (restore start, end, then control point).
5. **Toolbar Undo/Redo enabled state while dragging.**
   - Expected: it turns disabled when the gesture begins and back on at release, on all three surfaces (`changed()` is emitted).

Each line has a test in the task that owns it (marked **[RF n]**).

---

### Task 1: Geometry copy and compare

**Files:**
- Create: `include/annotations/AnnotationGeometry.h`, `src/annotations/AnnotationGeometry.cpp`
- Modify:
  - `include/annotations/ShapeAnnotation.h` and `src/annotations/ShapeAnnotation.cpp`: add `setRectF`.
  - `include/annotations/PolylineAnnotation.h` and `src/annotations/PolylineAnnotation.cpp`: add `setPoints`.
  - `include/annotations/TextBoxAnnotation.h` and `src/annotations/TextBoxAnnotation.cpp`: add `restoreLayout`.
  - `include/annotations/AnnotationStyle.h` and `src/annotations/AnnotationStyle.cpp`: add `equalStyle`.
  - `CMakeLists.txt`: add the new source next to `src/annotations/AnnotationStyle.cpp` (line ~307) and the header to the header list.
  - `tests/CMakeLists.txt`: register the new test after `Annotations_AnnotationStyle` (line ~1387).
- Test: `tests/Annotations/tst_AnnotationGeometry.cpp`

**Interfaces:**
- Produces:
  - `namespace AnnotationGeometry { bool copy(const AnnotationItem& from, AnnotationItem& to); bool equal(const AnnotationItem& a, const AnnotationItem& b); bool supports(const AnnotationItem& item); }`
  - `bool AnnotationStyle::equalStyle(const AnnotationItem& a, const AnnotationItem& b)`
  - `void ShapeAnnotation::setRectF(const QRectF&)`
  - `void PolylineAnnotation::setPoints(const QVector<QPoint>&)`
  - `void TextBoxAnnotation::restoreLayout(const QString& text, const QRectF& box, qreal wrapWidth)`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/Annotations/tst_AnnotationGeometry.cpp
#include <QtTest>
#include "annotations/AnnotationGeometry.h"
#include "annotations/AnnotationStyle.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/EmojiStickerAnnotation.h"
#include "annotations/PolylineAnnotation.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/StepBadgeAnnotation.h"
#include "annotations/TextBoxAnnotation.h"

class tst_AnnotationGeometry : public QObject
{
    Q_OBJECT
private slots:
    void shapeCopiesGeometryNotStyle();
    void arrowRestoresExactControlPoint();   // [RF 4]
    void polylineCopiesPoints();
    void textCopiesContentAndLayout();
    void emojiCopiesTransform();
    void mismatchedKindsAreRefused();
    void unsupportedKindsAreRefused();
    void equalMatchesCopy();
    void equalStyleComparesStyleOnly();
};

void tst_AnnotationGeometry::shapeCopiesGeometryNotStyle()
{
    ShapeAnnotation from(QRect(10, 10, 50, 40), ShapeType::Rectangle, Qt::red, 3, false);
    from.setRotation(30);
    from.setScale(1.5, 2.0);
    from.moveBy(QPointF(0.5, 0.25));
    ShapeAnnotation to(QRect(0, 0, 5, 5), ShapeType::Rectangle, Qt::blue, 9, true);
    QVERIFY(AnnotationGeometry::copy(from, to));
    QCOMPARE(to.rectF(), from.rectF());
    QCOMPARE(to.rotation(), from.rotation());
    QCOMPARE(to.scaleX(), 1.5);
    QCOMPARE(to.scaleY(), 2.0);
    QCOMPARE(to.color(), QColor(Qt::blue));   // style untouched
    QCOMPARE(to.width(), 9);
    QVERIFY(to.filled());
}

void tst_AnnotationGeometry::arrowRestoresExactControlPoint()
{
    ArrowAnnotation from(QPoint(0, 0), QPoint(100, 0), Qt::red, 3);
    from.setControlPoint(QPointF(50, 40));
    ArrowAnnotation to(QPoint(10, 10), QPoint(20, 90), Qt::red, 3);
    to.setControlPoint(QPointF(-7, 3));
    QVERIFY(AnnotationGeometry::copy(from, to));
    QCOMPARE(to.start(), QPoint(0, 0));
    QCOMPARE(to.end(), QPoint(100, 0));
    QCOMPARE(to.controlPoint(), QPointF(50, 40));
}

void tst_AnnotationGeometry::polylineCopiesPoints()
{
    PolylineAnnotation from(QVector<QPoint>{{0, 0}, {10, 10}, {20, 0}}, Qt::red, 3);
    PolylineAnnotation to(QVector<QPoint>{{5, 5}, {6, 6}}, Qt::green, 7);
    QVERIFY(AnnotationGeometry::copy(from, to));
    QCOMPARE(to.points(), from.points());
    QCOMPARE(to.width(), 7);
}

void tst_AnnotationGeometry::textCopiesContentAndLayout()
{
    QFont small;
    small.setPointSize(10);
    QFont big;
    big.setPointSize(30);
    TextBoxAnnotation from(QPointF(40, 50), QStringLiteral("Before"), small, Qt::red);
    from.setWrapWidth(120);
    from.setRotation(15);
    from.setScale(1.25);
    from.setMirror(true, false);
    TextBoxAnnotation to(QPointF(0, 0), QStringLiteral("After, longer"), big, Qt::blue);
    // Style first (as undo does), then geometry: the box must be from's, not
    // recomputed from `to`'s old text.
    QVERIFY(AnnotationStyle::copyStyle(from, to));
    QVERIFY(AnnotationGeometry::copy(from, to));
    QCOMPARE(to.text(), QStringLiteral("Before"));
    QCOMPARE(to.position(), QPointF(40, 50));
    QCOMPARE(to.box(), from.box());
    QCOMPARE(to.wrapWidth(), from.wrapWidth());
    QCOMPARE(to.rotation(), 15.0);
    QCOMPARE(to.scale(), 1.25);
    QVERIFY(to.mirrorX());
    // Geometry alone keeps to's font.
    TextBoxAnnotation other(QPointF(0, 0), QStringLiteral("x"), big, Qt::blue);
    QVERIFY(AnnotationGeometry::copy(from, other));
    QCOMPARE(other.font().pointSize(), 30);
}

void tst_AnnotationGeometry::emojiCopiesTransform()
{
    EmojiStickerAnnotation from(QPoint(30, 40), QStringLiteral("🙂"), 1.5);
    from.setRotation(45);
    from.setMirror(false, true);
    EmojiStickerAnnotation to(QPoint(0, 0), QStringLiteral("🙂"), 1.0);
    QVERIFY(AnnotationGeometry::copy(from, to));
    QCOMPARE(to.position(), QPoint(30, 40));
    QCOMPARE(to.scale(), 1.5);
    QCOMPARE(to.rotation(), 45.0);
    QVERIFY(to.mirrorY());
}

void tst_AnnotationGeometry::mismatchedKindsAreRefused()
{
    ShapeAnnotation shape(QRect(0, 0, 10, 10), ShapeType::Rectangle, Qt::red, 3, false);
    ArrowAnnotation arrow(QPoint(0, 0), QPoint(10, 0), Qt::red, 3);
    QVERIFY(!AnnotationGeometry::copy(shape, arrow));
    QVERIFY(!AnnotationGeometry::equal(shape, arrow));
}

void tst_AnnotationGeometry::unsupportedKindsAreRefused()
{
    StepBadgeAnnotation a(QPoint(0, 0), Qt::red, 1);
    StepBadgeAnnotation b(QPoint(5, 5), Qt::red, 2);
    QVERIFY(!AnnotationGeometry::supports(a));
    QVERIFY(!AnnotationGeometry::copy(a, b));
    QCOMPARE(b.position(), QPoint(5, 5));
}

void tst_AnnotationGeometry::equalMatchesCopy()
{
    ShapeAnnotation a(QRect(0, 0, 10, 10), ShapeType::Ellipse, Qt::red, 3, false);
    ShapeAnnotation b(QRect(4, 4, 10, 10), ShapeType::Ellipse, Qt::blue, 3, false);
    QVERIFY(!AnnotationGeometry::equal(a, b));
    QVERIFY(AnnotationGeometry::copy(a, b));
    QVERIFY(AnnotationGeometry::equal(a, b));   // colour differs, geometry equal
}

void tst_AnnotationGeometry::equalStyleComparesStyleOnly()
{
    ArrowAnnotation a(QPoint(0, 0), QPoint(10, 0), Qt::red, 3);
    ArrowAnnotation b(QPoint(50, 50), QPoint(90, 0), Qt::red, 3);
    QVERIFY(AnnotationStyle::equalStyle(a, b));
    b.setWidth(4);
    QVERIFY(!AnnotationStyle::equalStyle(a, b));
}

QTEST_MAIN(tst_AnnotationGeometry)
#include "tst_AnnotationGeometry.moc"
```

Constructor signatures must match the headers. Check each class's constructor before compiling and adjust the test calls to match (the StepBadge and Polyline constructors especially). The assertions stay the same.

Register it in `tests/CMakeLists.txt`:

```cmake
add_executable(Annotations_AnnotationGeometry Annotations/tst_AnnotationGeometry.cpp)
target_link_libraries(Annotations_AnnotationGeometry PRIVATE snaptray_core Qt6::Widgets Qt6::Test)
add_test(NAME Annotations_AnnotationGeometry COMMAND Annotations_AnnotationGeometry)
set_tests_properties(Annotations_AnnotationGeometry PROPERTIES TIMEOUT 60 LABELS "unit")
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cd build && ninja Annotations_AnnotationGeometry`
Expected: compile error, `annotations/AnnotationGeometry.h` not found.

- [ ] **Step 3: Implement**

The setters bypass the recalculations done by `setRect(QRect)` / `setText` / `setWrapWidth` / `setBox`:

```cpp
// ShapeAnnotation.h
void setRectF(const QRectF& rect) { m_rect = rect; }
// PolylineAnnotation.h
void setPoints(const QVector<QPoint>& points) { m_points = points; }
// TextBoxAnnotation.h (public)
// Restores a recorded layout exactly (#237): no box recalculation.
void restoreLayout(const QString& text, const QRectF& box, qreal wrapWidth);
// TextBoxAnnotation.cpp
void TextBoxAnnotation::restoreLayout(const QString& text, const QRectF& box, qreal wrapWidth)
{
    m_text = text;
    m_box = box;
    m_wrapWidth = wrapWidth;
    invalidateCache();
}
```

If `PolylineAnnotation` keeps derived caches, invalidate them in `setPoints` the same way `setPoint` does.

```cpp
// include/annotations/AnnotationGeometry.h
#pragma once

class AnnotationItem;

// Geometry and content of interactively editable annotations (#237): what a
// move, rotate, scale, endpoint/vertex drag, or text re-edit changes. Style
// (AnnotationStyle) and visibility are never included.
namespace AnnotationGeometry {
bool supports(const AnnotationItem& item);
// Copies `from`'s geometry onto `to`. False, with `to` unchanged, when the
// kinds differ or the kind has no editable geometry.
bool copy(const AnnotationItem& from, AnnotationItem& to);
bool equal(const AnnotationItem& a, const AnnotationItem& b);
} // namespace AnnotationGeometry
```

```cpp
// src/annotations/AnnotationGeometry.cpp
#include "annotations/AnnotationGeometry.h"

#include <array>

#include "annotations/ArrowAnnotation.h"
#include "annotations/EmojiStickerAnnotation.h"
#include "annotations/PolylineAnnotation.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/TextBoxAnnotation.h"

namespace {

struct Ops {
    bool (*matches)(const AnnotationItem&);
    void (*copy)(const AnnotationItem&, AnnotationItem&);
    bool (*equal)(const AnnotationItem&, const AnnotationItem&);
};

template <typename T> bool is(const AnnotationItem& item) { return dynamic_cast<const T*>(&item) != nullptr; }
template <typename T> const T& as(const AnnotationItem& item) { return static_cast<const T&>(item); }
template <typename T> T& as(AnnotationItem& item) { return static_cast<T&>(item); }

const std::array<Ops, 5> kOps = {{
    {&is<ShapeAnnotation>,
     [](const AnnotationItem& f, AnnotationItem& t) {
         const auto& a = as<ShapeAnnotation>(f);
         auto& b = as<ShapeAnnotation>(t);
         b.setRectF(a.rectF());
         b.setRotation(a.rotation());
         b.setScale(a.scaleX(), a.scaleY());
     },
     [](const AnnotationItem& x, const AnnotationItem& y) {
         const auto& a = as<ShapeAnnotation>(x);
         const auto& b = as<ShapeAnnotation>(y);
         return a.rectF() == b.rectF() && a.rotation() == b.rotation()
             && a.scaleX() == b.scaleX() && a.scaleY() == b.scaleY();
     }},
    {&is<ArrowAnnotation>,
     [](const AnnotationItem& f, AnnotationItem& t) {
         const auto& a = as<ArrowAnnotation>(f);
         auto& b = as<ArrowAnnotation>(t);
         b.setStart(a.start());
         b.setEnd(a.end());
         b.setControlPoint(a.controlPoint());   // last: setStart/setEnd shift it
     },
     [](const AnnotationItem& x, const AnnotationItem& y) {
         const auto& a = as<ArrowAnnotation>(x);
         const auto& b = as<ArrowAnnotation>(y);
         return a.start() == b.start() && a.end() == b.end() && a.controlPoint() == b.controlPoint();
     }},
    {&is<PolylineAnnotation>,
     [](const AnnotationItem& f, AnnotationItem& t) {
         as<PolylineAnnotation>(t).setPoints(as<PolylineAnnotation>(f).points());
     },
     [](const AnnotationItem& x, const AnnotationItem& y) {
         return as<PolylineAnnotation>(x).points() == as<PolylineAnnotation>(y).points();
     }},
    {&is<TextBoxAnnotation>,
     [](const AnnotationItem& f, AnnotationItem& t) {
         const auto& a = as<TextBoxAnnotation>(f);
         auto& b = as<TextBoxAnnotation>(t);
         b.setRotation(a.rotation());
         b.setScale(a.scale());
         b.setMirror(a.mirrorX(), a.mirrorY());
         b.setPosition(a.position());
         b.restoreLayout(a.text(), a.box(), a.wrapWidth());   // last: no recalculation after it
     },
     [](const AnnotationItem& x, const AnnotationItem& y) {
         const auto& a = as<TextBoxAnnotation>(x);
         const auto& b = as<TextBoxAnnotation>(y);
         return a.text() == b.text() && a.position() == b.position() && a.box() == b.box()
             && a.wrapWidth() == b.wrapWidth() && a.rotation() == b.rotation()
             && a.scale() == b.scale() && a.mirrorX() == b.mirrorX() && a.mirrorY() == b.mirrorY();
     }},
    {&is<EmojiStickerAnnotation>,
     [](const AnnotationItem& f, AnnotationItem& t) {
         const auto& a = as<EmojiStickerAnnotation>(f);
         auto& b = as<EmojiStickerAnnotation>(t);
         b.setPosition(a.position());
         b.setScale(a.scale());
         b.setRotation(a.rotation());
         b.setMirror(a.mirrorX(), a.mirrorY());
     },
     [](const AnnotationItem& x, const AnnotationItem& y) {
         const auto& a = as<EmojiStickerAnnotation>(x);
         const auto& b = as<EmojiStickerAnnotation>(y);
         return a.position() == b.position() && a.scale() == b.scale() && a.rotation() == b.rotation()
             && a.mirrorX() == b.mirrorX() && a.mirrorY() == b.mirrorY();
     }},
}};

const Ops* opsFor(const AnnotationItem& item)
{
    for (const Ops& ops : kOps) {
        if (ops.matches(item)) return &ops;
    }
    return nullptr;
}

} // namespace

namespace AnnotationGeometry {

bool supports(const AnnotationItem& item) { return opsFor(item) != nullptr; }

bool copy(const AnnotationItem& from, AnnotationItem& to)
{
    const Ops* ops = opsFor(from);
    if (!ops || opsFor(to) != ops) return false;
    ops->copy(from, to);
    return true;
}

bool equal(const AnnotationItem& a, const AnnotationItem& b)
{
    const Ops* ops = opsFor(a);
    return ops && opsFor(b) == ops && ops->equal(a, b);
}

} // namespace AnnotationGeometry
```

Rotation and scale comparisons use plain `==`: the values are copied exactly.

The Emoji's `setPosition` sets the field directly; check that it invalidates the Emoji's cache and add `invalidateCache()` if it does not.

```cpp
// AnnotationStyle.h
// True when the two items are the same stylable kind with equal style values.
bool equalStyle(const AnnotationItem& a, const AnnotationItem& b);
// AnnotationStyle.cpp
bool equalStyle(const AnnotationItem& a, const AnnotationItem& b)
{
    const AnnotationStyleValues x = read(a);
    const AnnotationStyleValues y = read(b);
    return x.kind != StyleTargetKind::None && x.kind == y.kind && x.color == y.color
        && x.width == y.width && x.filled == y.filled && x.arrowStyle == y.arrowStyle
        && x.lineStyle == y.lineStyle && x.font == y.font;
}
```

- [ ] **Step 4: Run it to verify it passes**

Run: `cd build && ninja Annotations_AnnotationGeometry && ./bin/Annotations_AnnotationGeometry`
Expected: `Totals: 11 passed, 0 failed` (9 tests plus initTestCase and cleanupTestCase).

- [ ] **Step 5: Commit**

```bash
git add include/annotations src/annotations CMakeLists.txt tests/CMakeLists.txt tests/Annotations/tst_AnnotationGeometry.cpp
git commit -m "feat(annotations): copy and compare annotation geometry (#237)"
```

---

### Task 2: Modify parts and the gesture API in AnnotationLayer

**Files:**
- Modify: `include/annotations/AnnotationLayer.h`, `src/AnnotationLayer.cpp`
- Test: `tests/Annotations/tst_AnnotationLayer.cpp`

**Interfaces:**
- Consumes: Task 1's `AnnotationGeometry::copy` / `equal` and `AnnotationStyle::equalStyle`.
- Produces:
  ```cpp
  enum ModifyPart : unsigned { ModifyStyle = 1u, ModifyGeometry = 2u };  // AnnotationLayer::ModifyPart
  bool beginGesture(AnnotationId id);
  bool ensureGesture();                     // begins on the selected item if none active
  bool commitGesture(unsigned parts);       // ModifyPart mask; true if a command was recorded
  void cancelGesture();
  bool isGestureActive() const;
  AnnotationId gestureId() const;
  ```

Behaviour (from the spec, plus rulings):
- **`beginGesture(id)`**
  - Returns false for an unknown id, an id of 0, or while the erase lock is held.
  - If a gesture is already active, it is committed with `Style|Geometry` first. This guards against a release that never came **[RF 1]** (ruling: the spec said "false if one is active"; committing the stale gesture keeps undo usable).
  - Ends style coalescing and emits `changed()`.
- **`commitGesture(parts)`**
  - If the item still exists and any requested part differs from the snapshot, pushes a `Modify` command with `parts` and the snapshot as `items[0]`.
  - Always ends the gesture and emits `changed()`.
- **`cancelGesture()`**
  - Restores the style (if the kind is stylable) and geometry from the snapshot, ends the gesture, invalidates the cache, and emits `changed()`.
- **Undo and redo**
  - `undo()` and `redo()` return early while a gesture is active; `canUndo()` and `canRedo()` return false.
  - The `Modify` exchange applies style first (when the Style bit is set), then geometry (when the Geometry bit is set).
- **Dropping the gesture**
  - `clear()`, `replaceItems()`, `beginEraseTransaction()` and `removeSelectedItem()` (when it removes the gesture's item) drop it without a command.
- **Owned items**
  - Both `forEachOwnedItem` overloads also visit the snapshot, so `translateAll` keeps it aligned **[RF 3]**.

- [ ] **Step 1: Write the failing tests** (append to `tst_AnnotationLayer.cpp` and declare the slots)

```cpp
void gestureCommitIsOneStep();
void unchangedGestureRecordsNothing();
void cancelGestureRestoresWithoutHistory();
void styleThenMoveUndoIndependently();
void moveThenStyleUndoIndependently();
void undoRefusedDuringGesture();            // [RF 5]
void gestureRefusedDuringErase();
void destructiveOpsDropGesture();
void staleGestureIsCommittedByNextBegin();  // [RF 1]
void translatedSnapshotUndoesToTranslatedPosition();  // [RF 3]
void gestureAdvancesStateToken();
void geometryUndoKeepsSelection();
```

```cpp
namespace {
ShapeAnnotation* addShape(AnnotationLayer& layer, QRect rect = QRect(10, 10, 40, 30))
{
    layer.addItem(std::make_unique<ShapeAnnotation>(rect, ShapeType::Rectangle, Qt::red, 3, false));
    return static_cast<ShapeAnnotation*>(layer.itemAt(int(layer.itemCount()) - 1));
}
} // namespace

void TestAnnotationLayer::gestureCommitIsOneStep()
{
    AnnotationLayer layer;
    ShapeAnnotation* shape = addShape(layer);
    const auto id = layer.idAt(0);
    QVERIFY(layer.beginGesture(id));
    for (int i = 0; i < 5; ++i) shape->moveBy(QPointF(2, 0));   // many moves, one gesture
    QVERIFY(layer.commitGesture(AnnotationLayer::ModifyGeometry));
    QCOMPARE(shape->rectF().topLeft(), QPointF(20, 10));
    layer.undo();
    QCOMPARE(static_cast<ShapeAnnotation*>(layer.itemAt(0))->rectF().topLeft(), QPointF(10, 10));
    QCOMPARE(layer.itemCount(), size_t(1));   // undid the move, not the add
    layer.redo();
    QCOMPARE(static_cast<ShapeAnnotation*>(layer.itemAt(0))->rectF().topLeft(), QPointF(20, 10));
}

void TestAnnotationLayer::unchangedGestureRecordsNothing()
{
    AnnotationLayer layer;
    addShape(layer);
    const auto before = layer.historyStateToken();
    QVERIFY(layer.beginGesture(layer.idAt(0)));
    QVERIFY(!layer.commitGesture(AnnotationLayer::ModifyGeometry));
    QCOMPARE(layer.historyStateToken(), before);
    QVERIFY(!layer.isGestureActive());
}

void TestAnnotationLayer::cancelGestureRestoresWithoutHistory()
{
    AnnotationLayer layer;
    ShapeAnnotation* shape = addShape(layer);
    const auto before = layer.historyStateToken();
    QVERIFY(layer.beginGesture(layer.idAt(0)));
    shape->moveBy(QPointF(30, 30));
    shape->setRotation(45);
    layer.cancelGesture();
    QCOMPARE(shape->rectF().topLeft(), QPointF(10, 10));
    QCOMPARE(shape->rotation(), 0.0);
    QCOMPARE(layer.historyStateToken(), before);
}

void TestAnnotationLayer::styleThenMoveUndoIndependently()
{
    AnnotationLayer layer;
    addShape(layer);
    const auto id = layer.idAt(0);
    QVERIFY(layer.modifyItem(id, QString(), [](AnnotationItem& item) {
        static_cast<ShapeAnnotation&>(item).setColor(Qt::blue);
        return true;
    }));
    QVERIFY(layer.beginGesture(id));
    static_cast<ShapeAnnotation*>(layer.itemAt(0))->moveBy(QPointF(25, 0));
    QVERIFY(layer.commitGesture(AnnotationLayer::ModifyGeometry));
    layer.undo();   // the move only
    auto* shape = static_cast<ShapeAnnotation*>(layer.itemAt(0));
    QCOMPARE(shape->rectF().topLeft(), QPointF(10, 10));
    QCOMPARE(shape->color(), QColor(Qt::blue));
    layer.undo();   // then the style
    QCOMPARE(shape->color(), QColor(Qt::red));
    QCOMPARE(shape->rectF().topLeft(), QPointF(10, 10));
}

void TestAnnotationLayer::moveThenStyleUndoIndependently()
{
    AnnotationLayer layer;
    addShape(layer);
    const auto id = layer.idAt(0);
    QVERIFY(layer.beginGesture(id));
    static_cast<ShapeAnnotation*>(layer.itemAt(0))->moveBy(QPointF(25, 0));
    QVERIFY(layer.commitGesture(AnnotationLayer::ModifyGeometry));
    QVERIFY(layer.modifyItem(id, QString(), [](AnnotationItem& item) {
        static_cast<ShapeAnnotation&>(item).setColor(Qt::blue);
        return true;
    }));
    layer.undo();   // the style only
    auto* shape = static_cast<ShapeAnnotation*>(layer.itemAt(0));
    QCOMPARE(shape->color(), QColor(Qt::red));
    QCOMPARE(shape->rectF().topLeft(), QPointF(35, 10));
    layer.undo();   // then the move
    QCOMPARE(shape->rectF().topLeft(), QPointF(10, 10));
    QCOMPARE(shape->color(), QColor(Qt::red));
}

void TestAnnotationLayer::undoRefusedDuringGesture()
{
    AnnotationLayer layer;
    addShape(layer);
    QSignalSpy changed(&layer, &AnnotationLayer::changed);
    QVERIFY(layer.canUndo());
    QVERIFY(layer.beginGesture(layer.idAt(0)));
    QVERIFY(changed.count() >= 1);   // toolbars refresh
    QVERIFY(!layer.canUndo());
    layer.undo();
    QCOMPARE(layer.itemCount(), size_t(1));
    layer.cancelGesture();
    QVERIFY(layer.canUndo());
}

void TestAnnotationLayer::gestureRefusedDuringErase()
{
    AnnotationLayer layer;
    addShape(layer);
    layer.beginEraseTransaction();
    QVERIFY(!layer.beginGesture(layer.idAt(0)));
    QVERIFY(layer.endEraseTransaction());
}

void TestAnnotationLayer::destructiveOpsDropGesture()
{
    AnnotationLayer layer;
    addShape(layer);
    QVERIFY(layer.beginGesture(layer.idAt(0)));
    layer.clear();
    QVERIFY(!layer.isGestureActive());

    addShape(layer);
    layer.setSelectedIndex(0);
    QVERIFY(layer.beginGesture(layer.idAt(0)));
    QVERIFY(layer.removeSelectedItem());
    QVERIFY(!layer.isGestureActive());
    QVERIFY(!layer.commitGesture(AnnotationLayer::ModifyGeometry));

    addShape(layer);
    QVERIFY(layer.beginGesture(layer.idAt(0)));
    layer.beginEraseTransaction();
    QVERIFY(!layer.isGestureActive());
    QVERIFY(layer.endEraseTransaction());
}

void TestAnnotationLayer::staleGestureIsCommittedByNextBegin()
{
    AnnotationLayer layer;
    ShapeAnnotation* a = addShape(layer);
    addShape(layer, QRect(100, 100, 10, 10));
    QVERIFY(layer.beginGesture(layer.idAt(0)));
    a->moveBy(QPointF(5, 0));
    // The release never arrived; the next gesture starts.
    QVERIFY(layer.beginGesture(layer.idAt(1)));
    QVERIFY(layer.commitGesture(AnnotationLayer::ModifyGeometry) == false);   // b unchanged
    layer.undo();   // the stale move was recorded
    QCOMPARE(static_cast<ShapeAnnotation*>(layer.itemAt(0))->rectF().topLeft(), QPointF(10, 10));
}

void TestAnnotationLayer::translatedSnapshotUndoesToTranslatedPosition()
{
    AnnotationLayer layer;
    ShapeAnnotation* shape = addShape(layer);
    QVERIFY(layer.beginGesture(layer.idAt(0)));
    shape->moveBy(QPointF(20, 0));
    QVERIFY(layer.commitGesture(AnnotationLayer::ModifyGeometry));
    layer.translateAll(QPointF(-10, -10));   // a Pin crop
    layer.undo();
    QCOMPARE(static_cast<ShapeAnnotation*>(layer.itemAt(0))->rectF().topLeft(), QPointF(0, 0));
}

void TestAnnotationLayer::gestureAdvancesStateToken()
{
    AnnotationLayer layer;
    ShapeAnnotation* shape = addShape(layer);
    const auto before = layer.historyStateToken();
    QVERIFY(layer.beginGesture(layer.idAt(0)));
    shape->moveBy(QPointF(1, 0));
    QVERIFY(layer.commitGesture(AnnotationLayer::ModifyGeometry));
    QVERIFY(layer.historyStateToken() != before);
    QCOMPARE(layer.historyStateRelation(before), AnnotationLayer::HistoryStateRelation::UndoReachable);
}

void TestAnnotationLayer::geometryUndoKeepsSelection()
{
    AnnotationLayer layer;
    ShapeAnnotation* shape = addShape(layer);
    layer.setSelectedIndex(0);
    QVERIFY(layer.ensureGesture());
    shape->moveBy(QPointF(3, 0));
    QVERIFY(layer.commitGesture(AnnotationLayer::ModifyGeometry));
    layer.undo();
    QCOMPARE(layer.selectedIndex(), 0);
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cd build && ninja Annotations_AnnotationLayer`
Expected: compile errors, `beginGesture` is not a member of `AnnotationLayer`.

- [ ] **Step 3: Implement**

Header, public (next to `modifyItem`):

```cpp
    // Parts of an annotation a Modify command restores (#224 style, #237 geometry).
    enum ModifyPart : unsigned { ModifyStyle = 1u, ModifyGeometry = 2u };

    // A gesture is one interactive change of an existing annotation (a drag,
    // rotate, scale, endpoint/vertex drag, or text re-edit) recorded as one
    // Modify command (#237). The item is snapshotted at begin; commit records
    // the requested parts if they changed; cancel restores the snapshot.
    // Undo/redo are refused while a gesture is active. A begin while another
    // gesture is active commits that one first.
    bool beginGesture(AnnotationId id);
    bool ensureGesture();   // begins on the selected item unless one is active
    bool commitGesture(unsigned parts);
    void cancelGesture();
    bool isGestureActive() const { return m_gestureId != 0; }
    AnnotationId gestureId() const { return m_gestureId; }
```

Private:
- Rename the `Modify` comment to "items[0] holds the other state of the recorded parts".
- Add `unsigned parts = 0;` to `HistoryCommand`.
- Rename `swapModifiedStyle` to `swapModifiedParts`.
- Add these members:

```cpp
    void dropGesture();
    AnnotationId m_gestureId = 0;
    std::unique_ptr<AnnotationItem> m_gestureSnapshot;
```

Source:

```cpp
#include "annotations/AnnotationGeometry.h"

bool AnnotationLayer::swapModifiedParts(HistoryCommand& command)
{
    // Exchanges only the recorded parts between the scene item and the stored
    // state. The scene object, its id and its visibility stay: changes outside
    // these parts must survive (#224 review).
    if (command.items.size() != 1 || !command.items.front().item) return false;
    auto sceneIt = findSceneItem(command.items.front().id);
    if (sceneIt == m_items.end() || !sceneIt->item) return false;
    std::unique_ptr<AnnotationItem> current = sceneIt->item->clone();
    AnnotationItem& stored = *command.items.front().item;
    // Style first: a text's font sets its box, which geometry then restores.
    if ((command.parts & ModifyStyle) && !AnnotationStyle::copyStyle(stored, *sceneIt->item)) return false;
    if ((command.parts & ModifyGeometry) && !AnnotationGeometry::copy(stored, *sceneIt->item)) return false;
    command.items.front().item = std::move(current);
    return true;
}
```

- In `undoCommand` and `redoCommand`, call `swapModifiedParts` for `Modify`.
- In `modifyItem`, set `command.parts = ModifyStyle;` on the new command.
- In `undo()` and `redo()`, add `|| isGestureActive()` to the early-return condition.
- In `canUndo()` and `canRedo()`, add `&& !isGestureActive()`.

```cpp
bool AnnotationLayer::beginGesture(AnnotationId id)
{
    if (m_eraseTransactionActive || id == 0) return false;
    if (isGestureActive()) commitGesture(ModifyStyle | ModifyGeometry);   // a release that never came
    auto sceneIt = findSceneItem(id);
    if (sceneIt == m_items.end() || !sceneIt->item) return false;
    endModifyCoalescing();
    m_gestureId = id;
    m_gestureSnapshot = sceneIt->item->clone();
    emit changed();   // Undo/Redo availability
    return true;
}

bool AnnotationLayer::ensureGesture()
{
    return isGestureActive() || beginGesture(selectedId());
}

void AnnotationLayer::dropGesture()
{
    m_gestureId = 0;
    m_gestureSnapshot.reset();
}

bool AnnotationLayer::commitGesture(unsigned parts)
{
    if (!isGestureActive()) return false;
    const AnnotationId id = m_gestureId;
    std::unique_ptr<AnnotationItem> before = std::move(m_gestureSnapshot);
    dropGesture();
    bool recorded = false;
    auto sceneIt = findSceneItem(id);
    if (sceneIt != m_items.end() && sceneIt->item && before) {
        const AnnotationItem& now = *sceneIt->item;
        unsigned changedParts = 0;
        if ((parts & ModifyStyle) && AnnotationStyle::kindOf(now) != StyleTargetKind::None
            && !AnnotationStyle::equalStyle(*before, now))
            changedParts |= ModifyStyle;
        if ((parts & ModifyGeometry) && AnnotationGeometry::supports(now)
            && !AnnotationGeometry::equal(*before, now))
            changedParts |= ModifyGeometry;
        if (changedParts != 0) {
            HistoryCommand command;
            command.kind = HistoryCommandKind::Modify;
            command.parts = changedParts;
            command.items.push_back({static_cast<size_t>(std::distance(m_items.begin(), sceneIt)),
                                     std::move(before), id});
            pushAppliedCommand(std::move(command));
            recorded = true;
        }
    }
    invalidateCache();
    emit changed();
    return recorded;
}

void AnnotationLayer::cancelGesture()
{
    if (!isGestureActive()) return;
    auto sceneIt = findSceneItem(m_gestureId);
    if (sceneIt != m_items.end() && sceneIt->item && m_gestureSnapshot) {
        if (AnnotationStyle::kindOf(*sceneIt->item) != StyleTargetKind::None)
            AnnotationStyle::copyStyle(*m_gestureSnapshot, *sceneIt->item);
        AnnotationGeometry::copy(*m_gestureSnapshot, *sceneIt->item);
    }
    dropGesture();
    invalidateCache();
    emit changed();
}
```

- `clear()` and `replaceItems()`: call `dropGesture();` next to `endModifyCoalescing();`.
- `beginEraseTransaction()`: call `dropGesture();`.
- `removeSelectedItem()`: before erasing, `if (m_items[selected].id == m_gestureId) dropGesture();`.
- Both `forEachOwnedItem` overloads: after the deferred items, `if (m_gestureSnapshot) visitor(m_gestureSnapshot.get());`.

`selectionChanged` and the keep-selection logic need no change: a geometry command is still `Modify` on the selected id.

- [ ] **Step 4: Run to verify they pass**

Run: `cd build && ninja Annotations_AnnotationLayer && ./bin/Annotations_AnnotationLayer`
Expected: every test passes, including the #224 `modify*` tests (style-only behaviour unchanged).

- [ ] **Step 5: Commit**

```bash
git add include/annotations/AnnotationLayer.h src/AnnotationLayer.cpp tests/Annotations/tst_AnnotationLayer.cpp
git commit -m "feat(annotations): geometry Modify parts and gesture history (#237)"
```

---

### Task 3: Text and Shape editors record gestures; Escape cancels them

**Files:**
- Modify:
  - `include/region/ShapeAnnotationEditor.h` and `src/region/ShapeAnnotationEditor.cpp`
  - `include/region/TextAnnotationEditor.h` and `src/region/TextAnnotationEditor.cpp` (transform and drag only; re-edit is Task 4)
  - `src/tools/handlers/TextToolHandler.cpp` (`handleEscape`) and `src/tools/handlers/ShapeToolHandler.cpp` (`handleEscape`)
- Test: `tests/RegionSelector/tst_ShapeAnnotationEditor.cpp`, `tests/RegionSelector/tst_TextAnnotationEditor.cpp`, `tests/Tools/tst_ToolManagerEscape.cpp`

**Interfaces:**
- Consumes: Task 2's `ensureGesture()`, `commitGesture(ModifyGeometry)` and `cancelGesture()`.
- Produces: `void ShapeAnnotationEditor::cancelInteraction();` and `void TextAnnotationEditor::cancelInteraction();`. Each one restores the annotation, clears the drag/transform flags, and does nothing when idle.

Rules:
- `updateTransformation` and `updateDragging` call `m_annotationLayer->ensureGesture()` before the first mutation. The gesture starts lazily, so a click without movement records nothing.
- `finishTransformation` and `finishDragging` call `m_annotationLayer->commitGesture(AnnotationLayer::ModifyGeometry)`.
- `TextToolHandler::handleEscape` and `ShapeToolHandler::handleEscape` call `cancelInteraction()` instead of `finish…()` when transforming or dragging.

- [ ] **Step 1: Write the failing tests**

```cpp
// tst_ShapeAnnotationEditor.cpp
void dragIsOneUndoStep();
void escapeCancelRestores();

void tst_ShapeAnnotationEditor::dragIsOneUndoStep()
{
    AnnotationLayer layer;
    layer.addItem(std::make_unique<ShapeAnnotation>(QRect(10, 10, 40, 30), ShapeType::Rectangle, Qt::red, 3, false));
    layer.setSelectedIndex(0);
    ShapeAnnotationEditor editor;
    editor.setAnnotationLayer(&layer);
    editor.startDragging(QPoint(20, 20));
    editor.updateDragging(QPoint(25, 20));
    editor.updateDragging(QPoint(40, 20));
    editor.finishDragging();
    auto* shape = static_cast<ShapeAnnotation*>(layer.itemAt(0));
    QCOMPARE(shape->rectF().topLeft(), QPointF(30, 10));
    layer.undo();
    QCOMPARE(shape->rectF().topLeft(), QPointF(10, 10));
    QCOMPARE(layer.itemCount(), size_t(1));
}

void tst_ShapeAnnotationEditor::escapeCancelRestores()
{
    AnnotationLayer layer;
    layer.addItem(std::make_unique<ShapeAnnotation>(QRect(10, 10, 40, 30), ShapeType::Rectangle, Qt::red, 3, false));
    layer.setSelectedIndex(0);
    ShapeAnnotationEditor editor;
    editor.setAnnotationLayer(&layer);
    const auto token = layer.historyStateToken();
    editor.startTransformation(QPoint(60, 25), GizmoHandle::Rotation);
    editor.updateTransformation(QPoint(30, 60));
    editor.cancelInteraction();
    auto* shape = static_cast<ShapeAnnotation*>(layer.itemAt(0));
    QCOMPARE(shape->rotation(), 0.0);
    QVERIFY(!editor.isTransforming());
    QCOMPARE(layer.historyStateToken(), token);
}
```

Add the same two tests for `TextAnnotationEditor` in `tst_TextAnnotationEditor.cpp`: a `TextBoxAnnotation` dragged by (+15, 0) undoes to its start, and a rotation cancelled leaves `rotation() == 0`.

In `tst_ToolManagerEscape.cpp`, add `shapeEscapeRestoresTransform`:
- Configure the ToolManager with the Shape tool current, the same way the file's existing tests do.
- Start a transform through `shapeAnnotationEditor`, move, then call `toolManager.handleEscape()`.
- Assert that it returns true and the rotation is restored.

- [ ] **Step 2: Run them to verify they fail**

Run: `cd build && ninja RegionSelector_ShapeAnnotationEditor RegionSelector_TextAnnotationEditor Tools_ToolManagerEscape`
Expected: compile error, `cancelInteraction` is not a member.

- [ ] **Step 3: Implement**

```cpp
// ShapeAnnotationEditor.cpp
#include "annotations/AnnotationLayer.h"   // already included

// In updateTransformation, after the null/flag check and before the switch:
    m_annotationLayer->ensureGesture();
// In updateDragging, after its guard:
    m_annotationLayer->ensureGesture();

void ShapeAnnotationEditor::finishTransformation()
{
    if (m_isTransforming && m_annotationLayer) m_annotationLayer->commitGesture(AnnotationLayer::ModifyGeometry);
    m_isTransforming = false;
    m_activeGizmoHandle = GizmoHandle::None;
}

void ShapeAnnotationEditor::finishDragging()
{
    if (m_isDragging && m_annotationLayer) m_annotationLayer->commitGesture(AnnotationLayer::ModifyGeometry);
    m_isDragging = false;
}

void ShapeAnnotationEditor::cancelInteraction()
{
    if (!m_isTransforming && !m_isDragging) return;
    if (m_annotationLayer) m_annotationLayer->cancelGesture();
    m_isTransforming = false;
    m_isDragging = false;
    m_activeGizmoHandle = GizmoHandle::None;
}
```

Make the same changes in `TextAnnotationEditor`:
- `ensureGesture()` in `updateTransformation` and `updateDragging` after their guards.
- Commit in `finishTransformation` and `finishDragging`, inside their existing `if (!m_isX) return;` guards and before the flag is cleared.
- `cancelInteraction()` mirrors the Shape version and also emits `updateRequested()`.

Handlers:

```cpp
// TextToolHandler::handleEscape: replace the two finish branches
    if (ctx->textAnnotationEditor
        && (ctx->textAnnotationEditor->isTransforming() || ctx->textAnnotationEditor->isDragging())) {
        ctx->textAnnotationEditor->cancelInteraction();
        ctx->repaint();
        return true;
    }
// ShapeToolHandler::handleEscape: same with shapeAnnotationEditor.
```

- [ ] **Step 4: Run to verify they pass**

Run: `cd build && ninja RegionSelector_ShapeAnnotationEditor RegionSelector_TextAnnotationEditor Tools_ToolManagerEscape && ./bin/RegionSelector_ShapeAnnotationEditor && ./bin/RegionSelector_TextAnnotationEditor && ./bin/Tools_ToolManagerEscape`
Expected: all pass. Existing Escape tests that asserted "finish" semantics are updated to the new restore semantics; a test that needs updating is a ruling to ledger.

- [ ] **Step 5: Commit**

```bash
git add include/region src/region src/tools/handlers tests/RegionSelector tests/Tools
git commit -m "feat(annotations): text and shape drags are undoable; Escape restores (#237)"
```

---

### Task 4: Text re-edit is one undo step

**Files:**
- Modify: `include/region/TextAnnotationEditor.h`, `src/region/TextAnnotationEditor.cpp`
- Test: `tests/RegionSelector/tst_TextAnnotationEditor.cpp`

**Interfaces:**
- Consumes: Task 2's `beginGesture(id)`, `commitGesture(ModifyStyle|ModifyGeometry)` and `cancelGesture()`; `AnnotationLayer::idAt` and `indexOfId`.
- Produces: unchanged public API. `editingIndex()` now resolves the stored id (`m_editingId`) through `indexOfId`, so it stays correct when other items are removed.

- [ ] **Step 1: Write the failing tests**

```cpp
void reEditIsOneUndoStep();
void emptyReEditRecordsNothingAndStaysVisible();   // [RF 2]
void reEditSurvivesItemRemovedUnderneath();        // [RF 2]
```

Use the file's existing fixture that creates a `TextAnnotationEditor` with an `InlineTextEditor` and identity mappers:

- **`reEditIsOneUndoStep`**
  - Add a `TextBoxAnnotation("Old")`.
  - Call `startReEditing(0, Qt::red)` and check `layer.canUndo()` is false.
  - Call `finishEditing("New", <its baseline display point>, Qt::blue)`.
  - Check the text is "New" and blue.
  - `layer.undo()` restores "Old", red, the old box and the old position; the item is visible.
  - `layer.redo()` gives "New" again.
- **`emptyReEditRecordsNothingAndStaysVisible`**
  - Re-edit, then call `cancelEditing()`.
  - The token is unchanged, the text is visible, and `!layer.isGestureActive()`.
  - Also: `finishEditing(QString(), …)` leaves the text visible and records nothing.
- **`reEditSurvivesItemRemovedUnderneath`**
  - Add two texts and re-edit index 1.
  - Remove index 0 directly with `setSelectedIndex(0)` and `removeSelectedItem()`. `beginGesture` was on item 1's id, so the gesture survives.
  - Call `finishEditing("New", …)`. Item 1, now at index 0, says "New" and is visible.

- [ ] **Step 2: Run them to verify they fail**

Run: `cd build && ninja RegionSelector_TextAnnotationEditor && ./bin/RegionSelector_TextAnnotationEditor reEditIsOneUndoStep emptyReEditRecordsNothingAndStaysVisible reEditSurvivesItemRemovedUnderneath`
Expected: FAIL. `canUndo()` is true during the edit; undo removes the text instead of restoring "Old"; the index-shift test edits the wrong item.

- [ ] **Step 3: Implement**

- Replace `int m_editingIndex` with `AnnotationLayer::AnnotationId m_editingId = 0;` and keep `editingIndex()` working:

```cpp
int editingIndex() const;   // header: no longer inline
int TextAnnotationEditor::editingIndex() const
{
    return (m_annotationLayer && m_editingId != 0) ? m_annotationLayer->indexOfId(m_editingId) : -1;
}
```

- `startEditing` (a new text): `m_editingId = 0;`.
- `startReEditing(annotationIndex, …)`:
  - After the `textItem` check: `m_editingId = m_annotationLayer->idAt(annotationIndex); m_annotationLayer->beginGesture(m_editingId);`.
  - Begin before `setVisible(false)`. The snapshot's visibility is never restored, so the order only matters for clarity.
- `finishEditing`, re-edit branch:
  - Replace `m_editingIndex >= 0` with `m_editingId != 0`, and `itemAt(m_editingIndex)` with `itemAt(editingIndex())` (null when the item is gone).
  - After the existing mutations, and also when the text is empty: `m_annotationLayer->commitGesture(AnnotationLayer::ModifyStyle | AnnotationLayer::ModifyGeometry);`. The commit records nothing when nothing changed.
  - Select with `setSelectedIndex(editingIndex())` only when it is >= 0, then `m_editingId = 0;`.
- `cancelEditing`:
  - If `m_editingId != 0`: `m_annotationLayer->cancelGesture();`, then restore visibility of `itemAt(editingIndex())` if non-null, then `m_editingId = 0;`.
- `grep -rn "m_editingIndex" src include`: there must be no remaining uses.

- [ ] **Step 4: Run to verify they pass**

Run: `cd build && ./bin/RegionSelector_TextAnnotationEditor`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add include/region/TextAnnotationEditor.h src/region/TextAnnotationEditor.cpp tests/RegionSelector/tst_TextAnnotationEditor.cpp
git commit -m "feat(annotations): a text re-edit is one undo step (#237)"
```

---

### Task 5: Region Capture wiring

**Files:**
- Modify:
  - `include/region/RegionInputHandler.h` and `src/region/RegionInputHandler.cpp`: Emoji/Arrow/Polyline move and release; `cancelAnnotationManipulation()`; right-click.
  - `src/RegionSelector.cpp`: Escape at the two `m_toolManager->handleEscape()` sites (~4500 and ~4789).
- Test: `tests/RegionSelector/tst_StyleSync.cpp`

**Interfaces:**
- Consumes: Task 2's `ensureGesture()`, `commitGesture(ModifyGeometry)` and `cancelGesture()`; Task 3's `cancelInteraction()`.
- Produces: `bool RegionInputHandler::cancelAnnotationManipulation();`. It returns true if a gesture was cancelled. It cancels the Text and Shape editors' interactions, clears the Emoji/Arrow/Polyline flags and handles, calls `cancelGesture()`, and requests an update.

Rules:
- In `handleEmojiStickerMove`, `handleArrowAnnotationMove` and `handlePolylineAnnotationMove`:
  - When a drag flag is set and a selected item exists, call `m_annotationLayer->ensureGesture();` right before the first mutator.
- In each `…Release`, inside the "was dragging" branch, call `m_annotationLayer->commitGesture(AnnotationLayer::ModifyGeometry);`.
- In `handleRightButtonPress`, call `cancelAnnotationManipulation()` first. If it returns true, return true; the right-click only cancels.
- In both RegionSelector Escape sites, before `m_toolManager->handleEscape()`, call `if (m_inputHandler && m_inputHandler->cancelAnnotationManipulation()) { update(); event->accept(); return; }`. In the event filter, `return true`.
- `RegionInputHandler` needs pointers to the Text and Shape editors for `cancelAnnotationManipulation`. If it does not hold them, it reaches them through the ToolManager context it already uses (see `RegionInputHandler.cpp:196-219`). If neither exists, `RegionSelector` cancels the two editors itself before calling the input handler.

- [ ] **Step 1: Write the failing tests** in `tests/RegionSelector/tst_StyleSync.cpp`, in the style of `selectedArrowStyleIsUndoable` (same fixture, real `QTest` mouse/key events on the selector):

```cpp
void shapeMoveIsUndoable();          // drag a selected rectangle by (+30,0) → undo → original rect; redo → moved
void arrowEndpointDragIsUndoable();  // drag the end handle → one undo restores end and control point
void polylineVertexDragIsUndoable(); // drag vertex 1 → undo restores it
void emojiRotateIsUndoable();        // rotation handle drag → undo restores rotation 0
void escapeMidDragRestores();        // press+move on a shape, Escape before release → original rect, canUndo unchanged, capture still open
void rightClickMidDragRestores();    // press+move, right press → original rect, annotations not cleared
void undoDisabledWhileDragging();    // [RF 5] after press+move, m_annotationLayer->canUndo() is false; after release true
void textReEditIsUndoable();         // double-click a text, type, Enter → undo restores the old text, visible
```

Each test adds its annotation through `m_annotationLayer->addItem`, selects it, and drives the gesture with `QTest::mousePress` / `mouseMove` / `mouseRelease` at handle positions computed from the item (`boundingRect()`, or the gizmo handle helpers the existing tests use). Assert on the item after `undo()` / `redo()` through the selector's undo path (`Ctrl+Z` key click), not by calling the layer directly.

- [ ] **Step 2: Run them to verify they fail**

Run: `cd build && ninja RegionSelector_StyleSync && ./bin/RegionSelector_StyleSync shapeMoveIsUndoable escapeMidDragRestores undoDisabledWhileDragging`
Expected:
- `shapeMoveIsUndoable` fails: Task 3 hooked the shared shape editor, so the move is recorded, but `escapeMidDragRestores` and the Emoji/Arrow/Polyline tests fail.
- **Ruling:** if `shapeMoveIsUndoable` already passes because of Task 3, keep it as a regression test. The RED proof for this task is then the Emoji/Arrow/Polyline/Escape/right-click tests.

- [ ] **Step 3: Implement** the rules above.

```cpp
// RegionInputHandler.cpp, e.g. handleArrowAnnotationMove after the item check:
    m_annotationLayer->ensureGesture();
// handleArrowAnnotationRelease, after the !m_isArrowDragging guard:
    m_annotationLayer->commitGesture(AnnotationLayer::ModifyGeometry);

bool RegionInputHandler::cancelAnnotationManipulation()
{
    if (!m_annotationLayer || !m_annotationLayer->isGestureActive()) return false;
    if (m_textAnnotationEditor) m_textAnnotationEditor->cancelInteraction();
    if (m_shapeAnnotationEditor) m_shapeAnnotationEditor->cancelInteraction();
    m_isEmojiDragging = m_isEmojiScaling = m_isEmojiRotating = false;
    m_activeEmojiHandle = GizmoHandle::None;
    m_isArrowDragging = false;
    m_activeArrowHandle = GizmoHandle::None;
    m_isPolylineDragging = false;
    m_activePolylineVertexIndex = -1;
    m_annotationLayer->cancelGesture();
    CursorManager::instance().setInputStateForWidget(m_parentWidget, InputState::Idle);
    emit updateRequested();
    return true;
}
```

Text re-edit is excluded from this cancel: `isGestureActive()` is also true during a re-edit, but Escape during inline editing is handled earlier (`RegionSelector.cpp:4456`) by `cancelEditing`, which calls `cancelGesture` (Task 4). Keep that order. `cancelAnnotationManipulation` must sit after the inline-editing Escape branch.

- [ ] **Step 4: Run to verify they pass**

Run: `cd build && ./bin/RegionSelector_StyleSync`
Expected: all pass. If `QCursor::setPos` races under `-j`, run the suite alone three times (known from #224).

- [ ] **Step 5: Commit**

```bash
git add include/region/RegionInputHandler.h src/region/RegionInputHandler.cpp src/RegionSelector.cpp tests/RegionSelector/tst_StyleSync.cpp
git commit -m "feat(region): undoable emoji/arrow/polyline edits; Escape and right-click cancel (#237)"
```

---

### Task 6: Pin Window wiring and crop ordering

**Files:**
- Modify: `include/PinWindow.h`, `src/PinWindow.cpp`
  - `handleEmojiStickerAnnotationMove` / `…Release`, `handleArrowAnnotationMove` / `…Release`, `handlePolylineAnnotationMove` / `…Release`
  - the Pin text move/release block (`:2720-2731`, `:2852-2865`)
  - Escape (`:3120`)
- Test: `tests/PinWindow/tst_StyleSync.cpp`, `tests/PinWindow/tst_CropUndo.cpp`

**Interfaces:**
- Consumes: Task 2 and Task 3 APIs.
- Produces: `bool PinWindow::cancelAnnotationGesture();`, the same contract as Region's `cancelAnnotationManipulation`, using PinWindow's own flags and editors.

Rules:
- Same as Task 5: `ensureGesture()` before the first mutator in each move handler, and `commitGesture(ModifyGeometry)` in each release branch.
- Pin's text drag reuses `TextAnnotationEditor` (Task 3 hooks), so only verify it.
- In the Escape handler, call `cancelAnnotationGesture()` after the inline-editing branch (`:3069`) and before `m_toolManager->handleEscape()`.
- `handleToolbarUndo` already relies on `canUndo()` / `isHistoryLocked()`. Also return early when `m_annotationLayer->isGestureActive()`, so crop undo cannot run mid-gesture.
- Call `updateUndoRedoState()` after commit and cancel (Pin's release path already does at `:2865`). Connect to `AnnotationLayer::changed` only if Pin does not already.

- [ ] **Step 1: Write the failing tests**

```cpp
// tst_StyleSync.cpp (Pin)
void shapeMoveIsUndoable();
void arrowControlPointDragIsUndoable();
void escapeMidDragRestores();
void toolbarUndoDisabledWhileDragging();   // [RF 5] m_toolbar->viewModel()->canUndo() false mid-drag
// tst_CropUndo.cpp
void cropThenMoveUndoesInOrder();          // [RF 3]: crop → move → undo (move) → undo (crop)
void moveThenCropUndoesInOrder();          // move → crop → undo (crop) → undo (move)
```

`cropThenMoveUndoesInOrder`:
- Apply a crop the way the file's existing tests do, then drag a shape by (+20, 0) with mouse events.
- Undo through `handleToolbarUndo()`: the shape is back at its cropped coordinates and the crop is still applied.
- Undo again: the crop is undone, and the shape is at its original pre-crop position.

- [ ] **Step 2: Run them to verify they fail**

Run: `cd build && ninja PinWindow_StyleSync PinWindow_CropUndo && ./bin/PinWindow_CropUndo cropThenMoveUndoesInOrder moveThenCropUndoesInOrder && ./bin/PinWindow_StyleSync arrowControlPointDragIsUndoable escapeMidDragRestores`
Expected: FAIL. Without the hooks, the arrow drag records nothing; `cropThenMove` undoes the crop first.

- [ ] **Step 3: Implement** the rules, mirroring Task 5's code with PinWindow's member names (`m_isEmojiDragging`, `m_isArrowDragging`, `m_isPolylineDragging`, `m_activePolylineVertexIndex`, the emoji/arrow handle members, `m_textAnnotationEditor`, `m_shapeAnnotationEditor`).

- [ ] **Step 4: Run to verify they pass**

Run: `cd build && ./bin/PinWindow_StyleSync && ./bin/PinWindow_CropUndo`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add include/PinWindow.h src/PinWindow.cpp tests/PinWindow
git commit -m "feat(pin): undoable annotation edits in step with crop undo (#237)"
```

---

### Task 7: Screen Canvas wiring

**Files:**
- Modify: `include/ScreenCanvasSession.h`, `src/ScreenCanvasSession.cpp`
  - Emoji/Arrow/Polyline move and release (`:2615-2821`)
  - Escape (`:2013`)
  - right-click (`:1811-1816`) if it reaches annotation drags
- Test: `tests/ScreenCanvas/tst_StyleSync.cpp`

**Interfaces:**
- Consumes: Task 2 and Task 3 APIs.
- Produces: `bool ScreenCanvasSession::cancelAnnotationGesture();`. It cancels the interaction-surface editors (`m_grabbedSurface` or `m_activeSurface`, as in `hasActiveAnnotationInteraction`), clears the session flags, calls `cancelGesture()`, and calls `updateAllSurfaces()`.

Rules:
- Same hooks as Task 5.
- Escape: call `cancelAnnotationGesture()` after the inline-editing branch and before `m_toolManager->handleEscape()`; on true, `updateAllSurfaces(); return;`.
- The Undo/Redo shortcuts at `:2027-2036` already go through `canUndo()` / `canRedo()`, so no change is needed.

- [ ] **Step 1: Write the failing tests**

```cpp
void shapeMoveIsUndoable();
void polylineMoveIsUndoable();
void escapeMidDragRestores();      // the canvas stays open
void textReEditIsUndoable();
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cd build && ninja ScreenCanvas_StyleSync && ./bin/ScreenCanvas_StyleSync polylineMoveIsUndoable escapeMidDragRestores`
Expected: FAIL. The polyline move records nothing; Escape closes the canvas or leaves the shape moved.

- [ ] **Step 3: Implement** the rules.

- [ ] **Step 4: Run to verify they pass**

Run: `cd build && ./bin/ScreenCanvas_StyleSync`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add include/ScreenCanvasSession.h src/ScreenCanvasSession.cpp tests/ScreenCanvas/tst_StyleSync.cpp
git commit -m "feat(canvas): undoable annotation edits; Escape cancels a drag (#237)"
```

---

### Task 8: Docs, operation video, full suite

**Files:**
- Modify:
  - `docs/docs/annotation-tools.md` and `docs/zh-tw/docs/annotation-tools.md`
  - `docs/developer/ui-design-guidelines.md` (Capture toolbar evidence list)
  - `tests/Qml/tst_ToolbarEvidence.cpp`, or a new gated slot in `tests/RegionSelector/tst_StyleSync.cpp` if the toolbar evidence fixture cannot drive annotations
- Create: `docs/assets/images/developer/toolbar/videos/annotation-geometry-undo.{gif,mp4}`

- [ ] **Step 1: Docs.** Add to the selection/editing section of both user docs:
  - English: "Moving, rotating, resizing, dragging an arrow's ends or curve, dragging a line's points, and re-editing a text are each one undo step. Press Esc during a drag to put the annotation back."
  - zh-TW: 「移動、旋轉、縮放、拖曳箭頭端點或曲線、拖曳折線的點，以及重新編輯文字，各自都是一步 undo。拖曳中按 Esc 會把標註放回原位。」

- [ ] **Step 2: Evidence video.** Add a slot that runs only when `SNAPTRAY_TOOLBAR_VIDEO_DIR` is set and writes frames with the pointer and a caption drawn in (reuse the `tst_ToolbarEvidence` frame helper).
  - Script: select a rectangle → drag it → rotate it → change its colour → Undo ×3 (rotation, colour and move come back in reverse order) → Redo → start a drag, press Esc (it snaps back).
  - Encode with the same ffmpeg commands as #224 (see `docs/developer/ui-design-guidelines.md` "Capture toolbar").
  - Check the frames by looking at a contact sheet.

- [ ] **Step 3: Full suite.**

Run: `./scripts/run-tests.sh`
Expected: `100% tests passed`. Record the count.

- [ ] **Step 4: Commit**

```bash
git add docs tests
git commit -m "docs(annotations): geometry undo and Esc-to-cancel; operation video (#237)"
```

The Windows and Ubuntu manual checks are added to #238 as a comment when the PR opens. That is outward-facing, so it is done only with the user's go-ahead given at PR time.
