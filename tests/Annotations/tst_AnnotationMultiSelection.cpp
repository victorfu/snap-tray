#include <QtTest/QtTest>
#include <QSignalSpy>

#include "annotations/AnnotationLayer.h"
#include "annotations/AnnotationStyle.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/EmojiStickerAnnotation.h"
#include "annotations/PencilStroke.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/StepBadgeAnnotation.h"
#include "annotations/TextBoxAnnotation.h"

// Multi-object selection and its single-step group transactions (#370).
class TestAnnotationMultiSelection : public QObject
{
    Q_OBJECT

private:
    using Id = AnnotationLayer::AnnotationId;

    // Text, shape, arrow and emoji, in that drawing order.
    static std::vector<Id> addMixed(AnnotationLayer& layer)
    {
        QFont font;
        font.setPointSize(16);
        layer.addItem(std::make_unique<TextBoxAnnotation>(QPointF(10, 10), QStringLiteral("Note"), font, Qt::red));
        layer.addItem(std::make_unique<ShapeAnnotation>(QRect(100, 100, 50, 40), ShapeType::Rectangle, Qt::blue, 3));
        layer.addItem(std::make_unique<ArrowAnnotation>(QPoint(200, 200), QPoint(260, 230), Qt::green, 4));
        layer.addItem(std::make_unique<EmojiStickerAnnotation>(QPoint(300, 300), QStringLiteral("A")));
        std::vector<Id> ids;
        for (int i = 0; i < static_cast<int>(layer.itemCount()); ++i) ids.push_back(layer.idAt(i));
        return ids;
    }
    template <typename T> static T* at(AnnotationLayer& layer, Id id)
    {
        return dynamic_cast<T*>(layer.itemAt(layer.indexOfId(id)));
    }

private slots:
    void selection_toggleBuildsSetAndKeepsSingleEditorsOff();
    void selection_skipsUnselectableAndStaleIds();
    void selection_marqueeHitsIntersectingSelectables();
    void groupDrag_isOneUndoStepAndRedoRestoresExactly();
    void groupDrag_usesTotalOffsetWithoutAccumulation();
    void groupDrag_cancelRestoresAllGeometry();
    void groupDrag_zeroOffsetRecordsNothingAndKeepsRedo();
    void groupDrag_refusesUndoWhileActive();
    void groupDrag_keepsInternalArrowBindingAndDetachesExternal();
    void nudge_coalescesRepeatsIntoOneStep();
    void groupDelete_isOneStepAndRestoresOrderAndBadgeNumbers();
    void groupStyle_changesOnlySupportingItemsInOneStep();
    void groupStyle_coalescedEditRestoresItemsChangedLater();
    void groupModify_undoAfterRemoveAndRestoreRestoresAll();
    void marquee_followsPaintedGeometry();
    void groupDrag_disablesUndoRedoAvailability();
};

void TestAnnotationMultiSelection::selection_toggleBuildsSetAndKeepsSingleEditorsOff()
{
    AnnotationLayer layer;
    const auto ids = addMixed(layer);
    layer.setSelectedIndex(1);
    QCOMPARE(layer.selectionCount(), 1);

    QSignalSpy spy(&layer, &AnnotationLayer::selectionChanged);
    layer.toggleSelected(ids[2]);
    QCOMPARE(spy.count(), 1);
    QVERIFY(layer.isMultiSelection());
    QCOMPARE(layer.selectedIds(), (std::vector<Id>{ids[1], ids[2]}));
    // No single-object editor engages on a group.
    QCOMPARE(layer.selectedIndex(), -1);
    QVERIFY(layer.selectedItem() == nullptr);

    layer.toggleSelected(ids[1]);
    QVERIFY(!layer.isMultiSelection());
    QCOMPARE(layer.selectedId(), ids[2]);

    layer.selectAll();
    QCOMPARE(layer.selectedIds(), ids);
    layer.setSelectedIndex(0);
    QCOMPARE(layer.selectedIds(), (std::vector<Id>{ids[0]}));
    layer.selectAll();
    layer.clearSelection();
    QCOMPARE(layer.selectionCount(), 0);
}

void TestAnnotationMultiSelection::selection_skipsUnselectableAndStaleIds()
{
    AnnotationLayer layer;
    const auto ids = addMixed(layer);
    layer.addItem(std::make_unique<StepBadgeAnnotation>(QPoint(5, 5), Qt::red));
    layer.addItem(std::make_unique<PencilStroke>(QVector<QPointF>{{1, 1}, {9, 9}}, Qt::red, 3, LineStyle::Solid));
    layer.selectAll();
    // Badges and strokes stay unselectable; masks likewise.
    QCOMPARE(layer.selectedIds(), ids);

    // Ids that leave the scene drop out of the selection.
    layer.setSelectedIds({ids[0], ids[1], ids[2]});
    layer.setSelectedIds({ids[1], ids[2], 9999});
    QCOMPARE(layer.selectedIds(), (std::vector<Id>{ids[1], ids[2]}));
    // A hidden (re-edited) text is not selected.
    layer.setSelectedIds({ids[0], ids[1], ids[2]});
    layer.itemAt(layer.indexOfId(ids[0]))->setVisible(false);
    QCOMPARE(layer.selectedIds(), (std::vector<Id>{ids[1], ids[2]}));
}

void TestAnnotationMultiSelection::selection_marqueeHitsIntersectingSelectables()
{
    AnnotationLayer layer;
    const auto ids = addMixed(layer);
    QCOMPARE(layer.selectableIdsIntersecting(QRect(QPoint(140, 140), QPoint(210, 210))),
             (std::vector<Id>{ids[1], ids[2]}));
    // Dragging up-left gives the same rectangle.
    QCOMPARE(layer.selectableIdsIntersecting(QRect(QPoint(210, 210), QPoint(140, 140))),
             (std::vector<Id>{ids[1], ids[2]}));
    QVERIFY(layer.selectableIdsIntersecting(QRect(1000, 1000, 10, 10)).empty());
    QCOMPARE(layer.hitTestSelectable(QPoint(125, 100)), 1);
    QCOMPARE(layer.hitTestSelectable(QPoint(-50, -50)), -1);
}

void TestAnnotationMultiSelection::groupDrag_isOneUndoStepAndRedoRestoresExactly()
{
    AnnotationLayer layer;
    const auto ids = addMixed(layer);
    layer.selectAll();
    const auto history = layer.historyCommandCount();
    const QPointF textPos = at<TextBoxAnnotation>(layer, ids[0])->position();
    const QRectF shapeRect = at<ShapeAnnotation>(layer, ids[1])->rectF();
    const QPoint arrowStart = at<ArrowAnnotation>(layer, ids[2])->start();
    const QPointF emojiPos = at<EmojiStickerAnnotation>(layer, ids[3])->precisePosition();

    QVERIFY(layer.beginGroupGesture());
    QVERIFY(layer.updateGroupGesture(QPointF(5, 5)));
    QVERIFY(layer.updateGroupGesture(QPointF(30, -12)));
    QVERIFY(layer.commitGroupGesture());
    QCOMPARE(layer.historyCommandCount(), history + 1);

    const QPointF offset(30, -12);
    QCOMPARE(at<TextBoxAnnotation>(layer, ids[0])->position(), textPos + offset);
    QCOMPARE(at<ShapeAnnotation>(layer, ids[1])->rectF(), shapeRect.translated(offset));
    QCOMPARE(at<ArrowAnnotation>(layer, ids[2])->start(), arrowStart + offset.toPoint());
    QCOMPARE(at<EmojiStickerAnnotation>(layer, ids[3])->precisePosition(), emojiPos + offset);

    layer.undo();
    QCOMPARE(at<TextBoxAnnotation>(layer, ids[0])->position(), textPos);
    QCOMPARE(at<ShapeAnnotation>(layer, ids[1])->rectF(), shapeRect);
    QCOMPARE(at<ArrowAnnotation>(layer, ids[2])->start(), arrowStart);
    QCOMPARE(at<EmojiStickerAnnotation>(layer, ids[3])->precisePosition(), emojiPos);
    // The group stays selected across its own undo.
    QCOMPARE(layer.selectedIds(), ids);

    layer.redo();
    QCOMPARE(at<TextBoxAnnotation>(layer, ids[0])->position(), textPos + offset);
    QCOMPARE(at<ShapeAnnotation>(layer, ids[1])->rectF(), shapeRect.translated(offset));
    QCOMPARE(at<ArrowAnnotation>(layer, ids[2])->start(), arrowStart + offset.toPoint());
    QCOMPARE(at<EmojiStickerAnnotation>(layer, ids[3])->precisePosition(), emojiPos + offset);
}

void TestAnnotationMultiSelection::groupDrag_usesTotalOffsetWithoutAccumulation()
{
    AnnotationLayer layer;
    const auto ids = addMixed(layer);
    layer.setSelectedIds({ids[1], ids[2]});
    const QRectF shapeRect = at<ShapeAnnotation>(layer, ids[1])->rectF();
    QVERIFY(layer.beginGroupGesture());
    for (int i = 1; i <= 20; ++i) layer.updateGroupGesture(QPointF(i * 0.4, 0));
    // 20 frames of 0.4 px is an 8 px drag, not 20 rounded 0 px steps.
    QCOMPARE(at<ShapeAnnotation>(layer, ids[1])->rectF(), shapeRect.translated(8, 0));
    layer.commitGroupGesture();
}

void TestAnnotationMultiSelection::groupDrag_cancelRestoresAllGeometry()
{
    AnnotationLayer layer;
    const auto ids = addMixed(layer);
    layer.selectAll();
    const auto history = layer.historyCommandCount();
    const QRectF shapeRect = at<ShapeAnnotation>(layer, ids[1])->rectF();
    const QPoint arrowEnd = at<ArrowAnnotation>(layer, ids[2])->end();
    QVERIFY(layer.beginGroupGesture());
    layer.updateGroupGesture(QPointF(40, 40));
    layer.cancelGroupGesture();
    QVERIFY(!layer.isGroupGestureActive());
    QCOMPARE(layer.historyCommandCount(), history);
    QCOMPARE(at<ShapeAnnotation>(layer, ids[1])->rectF(), shapeRect);
    QCOMPARE(at<ArrowAnnotation>(layer, ids[2])->end(), arrowEnd);
}

void TestAnnotationMultiSelection::groupDrag_zeroOffsetRecordsNothingAndKeepsRedo()
{
    AnnotationLayer layer;
    const auto ids = addMixed(layer);
    layer.undo();   // the emoji goes to redo
    QVERIFY(layer.canRedo());
    layer.setSelectedIds({ids[0], ids[1]});
    const auto history = layer.historyCommandCount();
    QVERIFY(layer.beginGroupGesture());
    layer.updateGroupGesture(QPointF(10, 0));
    layer.updateGroupGesture(QPointF(0, 0));
    QVERIFY(!layer.commitGroupGesture());
    QCOMPARE(layer.historyCommandCount(), history);
    QVERIFY(layer.canRedo());
    QVERIFY(!layer.translateSelectedItems(QPointF(0.2, 0.2), QStringLiteral("nudge")));
    QVERIFY(layer.canRedo());
}

void TestAnnotationMultiSelection::groupDrag_refusesUndoWhileActive()
{
    AnnotationLayer layer;
    const auto ids = addMixed(layer);
    layer.setSelectedIds({ids[1], ids[2]});
    QVERIFY(layer.beginGroupGesture());
    layer.updateGroupGesture(QPointF(3, 3));
    const auto count = layer.itemCount();
    layer.undo();
    QCOMPARE(layer.itemCount(), count);
    QVERIFY(layer.isGroupGestureActive());
    QVERIFY(!layer.removeSelectedItems());
    // Drawing a new annotation ends the drag as one step first.
    const auto history = layer.historyCommandCount();
    layer.addItem(std::make_unique<ShapeAnnotation>(QRect(0, 0, 5, 5), ShapeType::Ellipse, Qt::red, 2));
    QVERIFY(!layer.isGroupGestureActive());
    QCOMPARE(layer.historyCommandCount(), history + 2);
}

void TestAnnotationMultiSelection::groupDrag_keepsInternalArrowBindingAndDetachesExternal()
{
    AnnotationLayer layer;
    layer.addItem(std::make_unique<ShapeAnnotation>(QRect(0, 0, 100, 100), ShapeType::Rectangle, Qt::red, 2));
    layer.addItem(std::make_unique<ShapeAnnotation>(QRect(400, 0, 100, 100), ShapeType::Rectangle, Qt::red, 2));
    layer.addItem(std::make_unique<ArrowAnnotation>(QPoint(100, 50), QPoint(400, 50), Qt::red, 3));
    const Id inside = layer.idAt(0), outside = layer.idAt(1), arrowId = layer.idAt(2);
    auto* arrow = at<ArrowAnnotation>(layer, arrowId);
    arrow->setBinding(false, {at<ShapeAnnotation>(layer, inside)->persistentId(), QPointF(1.0, 0.5)});
    arrow->setBinding(true, {at<ShapeAnnotation>(layer, outside)->persistentId(), QPointF(0.0, 0.5)});

    layer.setSelectedIds({inside, arrowId});
    QVERIFY(layer.beginGroupGesture());
    layer.updateGroupGesture(QPointF(10, 20));
    QVERIFY(layer.commitGroupGesture());
    arrow = at<ArrowAnnotation>(layer, arrowId);
    QCOMPARE(arrow->start(), QPoint(110, 70));
    QCOMPARE(arrow->end(), QPoint(410, 70));
    QVERIFY(!arrow->binding(false).targetId.isNull());
    QVERIFY(arrow->binding(true).targetId.isNull());

    layer.undo();
    arrow = at<ArrowAnnotation>(layer, arrowId);
    QCOMPARE(arrow->start(), QPoint(100, 50));
    QCOMPARE(arrow->binding(true).targetId, at<ShapeAnnotation>(layer, outside)->persistentId());
}

void TestAnnotationMultiSelection::nudge_coalescesRepeatsIntoOneStep()
{
    AnnotationLayer layer;
    const auto ids = addMixed(layer);
    layer.setSelectedIds({ids[1], ids[3]});
    const auto history = layer.historyCommandCount();
    const QRectF shapeRect = at<ShapeAnnotation>(layer, ids[1])->rectF();
    for (int i = 0; i < 3; ++i) QVERIFY(layer.translateSelectedItems(QPointF(1, 0), QStringLiteral("nudge")));
    QCOMPARE(layer.historyCommandCount(), history + 1);
    QCOMPARE(at<ShapeAnnotation>(layer, ids[1])->rectF(), shapeRect.translated(3, 0));
    layer.endModifyCoalescing();
    QVERIFY(layer.translateSelectedItems(QPointF(0, 10), QStringLiteral("nudge")));
    QCOMPARE(layer.historyCommandCount(), history + 2);
    layer.undo();
    layer.undo();
    QCOMPARE(at<ShapeAnnotation>(layer, ids[1])->rectF(), shapeRect);
}

void TestAnnotationMultiSelection::groupDelete_isOneStepAndRestoresOrderAndBadgeNumbers()
{
    AnnotationLayer layer;
    layer.addItem(std::make_unique<StepBadgeAnnotation>(QPoint(5, 5), Qt::red));
    const auto all = addMixed(layer);
    const std::vector<Id> ids(all.begin() + 1, all.end());   // past the badge
    layer.addItem(std::make_unique<StepBadgeAnnotation>(QPoint(50, 5), Qt::red, 2));
    std::vector<Id> before;
    for (int i = 0; i < static_cast<int>(layer.itemCount()); ++i) before.push_back(layer.idAt(i));
    QCOMPARE(at<StepBadgeAnnotation>(layer, before.back())->number(), 2);

    layer.setSelectedIds({ids[0], ids[2], ids[3]});
    const auto history = layer.historyCommandCount();
    QVERIFY(layer.removeSelectedItems());
    QCOMPARE(layer.historyCommandCount(), history + 1);
    QCOMPARE(layer.itemCount(), before.size() - 3);
    QCOMPARE(layer.selectionCount(), 0);

    layer.undo();
    std::vector<Id> restored;
    for (int i = 0; i < static_cast<int>(layer.itemCount()); ++i) restored.push_back(layer.idAt(i));
    QCOMPARE(restored, before);
    QCOMPARE(at<StepBadgeAnnotation>(layer, before.back())->number(), 2);
    layer.redo();
    QCOMPARE(layer.itemCount(), before.size() - 3);
    QCOMPARE(layer.indexOfId(ids[1]), 1);
}

void TestAnnotationMultiSelection::groupStyle_changesOnlySupportingItemsInOneStep()
{
    AnnotationLayer layer;
    const auto ids = addMixed(layer);
    layer.selectAll();
    const auto history = layer.historyCommandCount();
    const int shapeWidth = AnnotationStyle::read(*at<ShapeAnnotation>(layer, ids[1])).width;
    QVERIFY(layer.modifySelectedItems(QStringLiteral("color"), [](AnnotationItem& item) {
        return AnnotationStyle::apply(item, StyleProperty::Color, QColor(Qt::magenta));
    }));
    QCOMPARE(layer.historyCommandCount(), history + 1);
    for (int i = 0; i < 3; ++i)
        QCOMPARE(AnnotationStyle::read(*layer.itemAt(layer.indexOfId(ids[i]))).color, QColor(Qt::magenta));
    // Width is unsupported by text: it is left alone, not converted.
    QVERIFY(layer.modifySelectedItems(QStringLiteral("width"), [](AnnotationItem& item) {
        return AnnotationStyle::apply(item, StyleProperty::Width, 9);
    }));
    QCOMPARE(AnnotationStyle::read(*at<ShapeAnnotation>(layer, ids[1])).width, 9);
    layer.undo();
    QCOMPARE(AnnotationStyle::read(*at<ShapeAnnotation>(layer, ids[1])).width, shapeWidth);
    layer.undo();
    QCOMPARE(AnnotationStyle::read(*at<TextBoxAnnotation>(layer, ids[0])).color, QColor(Qt::red));
    QCOMPARE(AnnotationStyle::read(*at<ShapeAnnotation>(layer, ids[1])).color, QColor(Qt::blue));
    QCOMPARE(AnnotationStyle::read(*at<ArrowAnnotation>(layer, ids[2])).color, QColor(Qt::green));
    // Setting a value every target already has records nothing.
    QVERIFY(!layer.modifySelectedItems(QStringLiteral("width"), [](AnnotationItem&) { return false; }));
    QCOMPARE(layer.historyCommandCount(), history + 2);
}

void TestAnnotationMultiSelection::groupStyle_coalescedEditRestoresItemsChangedLater()
{
    AnnotationLayer layer;
    layer.addItem(std::make_unique<ShapeAnnotation>(QRect(0, 0, 10, 10), ShapeType::Rectangle, Qt::red, 2));
    layer.addItem(std::make_unique<ShapeAnnotation>(QRect(20, 0, 10, 10), ShapeType::Rectangle, Qt::blue, 2));
    const Id red = layer.idAt(0), blue = layer.idAt(1);
    layer.selectAll();
    const auto history = layer.historyCommandCount();
    auto setColor = [&layer](const QColor& color) {
        return layer.modifySelectedItems(QStringLiteral("color"), [color](AnnotationItem& item) {
            return AnnotationStyle::apply(item, StyleProperty::Color, color);
        });
    };
    QVERIFY(setColor(Qt::blue));    // only the red one changes
    QVERIFY(setColor(Qt::green));   // now both do
    QCOMPARE(layer.historyCommandCount(), history + 1);
    layer.undo();
    QCOMPARE(AnnotationStyle::read(*at<ShapeAnnotation>(layer, red)).color, QColor(Qt::red));
    QCOMPARE(AnnotationStyle::read(*at<ShapeAnnotation>(layer, blue)).color, QColor(Qt::blue));
}

void TestAnnotationMultiSelection::groupModify_undoAfterRemoveAndRestoreRestoresAll()
{
    AnnotationLayer layer;
    const auto ids = addMixed(layer);
    layer.setSelectedIds({ids[1], ids[2]});
    QVERIFY(layer.translateSelectedItems(QPointF(5, 0), QString()));
    const QRectF movedShape = at<ShapeAnnotation>(layer, ids[1])->rectF();
    const QPoint movedArrow = at<ArrowAnnotation>(layer, ids[2])->start();
    // The group Modify still finds its arrow after a removal is undone.
    layer.setSelectedIds({ids[2]});
    QVERIFY(layer.removeSelectedItem());
    layer.undo();   // the arrow comes back: the group Modify is next
    layer.undo();
    QCOMPARE(at<ShapeAnnotation>(layer, ids[1])->rectF(), movedShape.translated(-5, 0));
    QCOMPARE(at<ArrowAnnotation>(layer, ids[2])->start(), movedArrow - QPoint(5, 0));
}

void TestAnnotationMultiSelection::marquee_followsPaintedGeometry()
{
    AnnotationLayer layer;
    layer.addItem(std::make_unique<ArrowAnnotation>(QPoint(0, 0), QPoint(200, 200), Qt::red, 4));
    layer.addItem(std::make_unique<ShapeAnnotation>(QRect(300, 0, 200, 200), ShapeType::Rectangle, Qt::red, 4));
    auto shape = std::make_unique<ShapeAnnotation>(QRect(600, 0, 200, 40), ShapeType::Rectangle, Qt::red, 4, true);
    shape->setRotation(90);
    layer.addItem(std::move(shape));
    const Id arrow = layer.idAt(0), hollow = layer.idAt(1), rotated = layer.idAt(2);
    // Inside the diagonal arrow's box but far from the line.
    QVERIFY(layer.selectableIdsIntersecting(QRect(QPoint(150, 10), QPoint(190, 40))).empty());
    QCOMPARE(layer.selectableIdsIntersecting(QRect(95, 95, 10, 10)), std::vector<Id>{arrow});
    // Inside a hollow rectangle, away from its outline.
    QVERIFY(layer.selectableIdsIntersecting(QRect(350, 50, 50, 50)).empty());
    QCOMPARE(layer.selectableIdsIntersecting(QRect(290, 90, 20, 20)), std::vector<Id>{hollow});
    // A rotated bar now stands upright around its center (700, 20).
    QVERIFY(layer.selectableIdsIntersecting(QRect(600, 0, 40, 30)).empty());
    QCOMPARE(layer.selectableIdsIntersecting(QRect(690, -60, 20, 20)), std::vector<Id>{rotated});
}

void TestAnnotationMultiSelection::groupDrag_disablesUndoRedoAvailability()
{
    AnnotationLayer layer;
    const auto ids = addMixed(layer);
    layer.undo();
    layer.setSelectedIds({ids[0], ids[1]});
    QVERIFY(layer.canUndo());
    QVERIFY(layer.canRedo());
    QVERIFY(layer.beginGroupGesture());
    QVERIFY(layer.isAnyGestureActive());
    QVERIFY(!layer.canUndo());
    QVERIFY(!layer.canRedo());
    layer.cancelGroupGesture();
    QVERIFY(layer.canUndo());
}

QTEST_MAIN(TestAnnotationMultiSelection)
#include "tst_AnnotationMultiSelection.moc"
