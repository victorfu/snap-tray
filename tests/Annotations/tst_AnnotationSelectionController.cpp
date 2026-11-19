#include <QtTest/QtTest>
#include <QImage>
#include <QKeyEvent>
#include <QPainter>

#include "annotation/AnnotationSelectionController.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/ShapeAnnotation.h"

// Pointer and key input of a multi-object selection, shared by both hosts (#370).
class tst_AnnotationSelectionController : public QObject
{
    Q_OBJECT

private:
    // Shapes at x = 0, 100, 200 (each 50 x 50) and an arrow below them.
    static void addScene(AnnotationLayer& layer)
    {
        for (int i = 0; i < 3; ++i)
            layer.addItem(std::make_unique<ShapeAnnotation>(QRect(i * 100, 0, 50, 50),
                                                            ShapeType::Rectangle, Qt::red, 3, true));
        layer.addItem(std::make_unique<ArrowAnnotation>(QPoint(0, 200), QPoint(80, 200), Qt::red, 4));
    }
    static QRectF rectOf(AnnotationLayer& layer, int index)
    {
        return static_cast<ShapeAnnotation*>(layer.itemAt(index))->rectF();
    }
    static bool key(AnnotationSelectionController& controller, int keyCode,
                    Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                    const std::function<QPointF(const QPoint&)>& map = {})
    {
        QKeyEvent event(QEvent::KeyPress, keyCode, modifiers);
        return controller.handleKey(&event, map);
    }

private slots:
    void shiftClickTogglesMembership();
    void plainPressOnGroupMemberDragsGroupAsOneStep();
    void plainClickOnGroupMemberRecordsNothing();
    void pressOutsideGroupClearsItAndFallsThrough();
    void marqueeOnlyWhereAllowed();
    void marqueeAddsToExistingSelection();
    void escapeCancelsDragThenClearsGroup();
    void selectAllDeleteAndNudge();
    void nudgeFollowsHostMapping();
    void drawsOutlinesOnlyForGroups();
    void missedReleaseKeepsTheDrag();
};

void tst_AnnotationSelectionController::shiftClickTogglesMembership()
{
    AnnotationLayer layer;
    addScene(layer);
    AnnotationSelectionController controller(&layer);
    // Without Shift and without a group, the host's own editors take the press.
    QVERIFY(!controller.handlePress(QPoint(25, 25), Qt::NoModifier, true));
    QVERIFY(controller.handlePress(QPoint(25, 25), Qt::ShiftModifier, false));
    QVERIFY(controller.handlePress(QPoint(125, 25), Qt::ShiftModifier, false));
    QVERIFY(controller.handleRelease(QPoint(125, 25)) == false);
    QCOMPARE(layer.selectionCount(), 2);
    QVERIFY(controller.handlePress(QPoint(25, 25), Qt::ShiftModifier, false));
    QCOMPARE(layer.selectedIds(), std::vector<AnnotationLayer::AnnotationId>{layer.idAt(1)});
}

void tst_AnnotationSelectionController::plainPressOnGroupMemberDragsGroupAsOneStep()
{
    AnnotationLayer layer;
    addScene(layer);
    AnnotationSelectionController controller(&layer);
    layer.setSelectedIds({layer.idAt(0), layer.idAt(1)});
    const auto history = layer.historyCommandCount();
    QVERIFY(controller.handlePress(QPoint(25, 25), Qt::NoModifier, true));
    QVERIFY(controller.handleMove(QPoint(30, 30)));
    QVERIFY(controller.isDragging());
    QVERIFY(controller.handleMove(QPoint(45, 35)));
    QVERIFY(controller.handleRelease(QPoint(45, 35)));
    QCOMPARE(layer.historyCommandCount(), history + 1);
    QCOMPARE(rectOf(layer, 0), QRectF(20, 10, 50, 50));
    QCOMPARE(rectOf(layer, 1), QRectF(120, 10, 50, 50));
    QCOMPARE(rectOf(layer, 2), QRectF(200, 0, 50, 50));
    layer.undo();
    QCOMPARE(rectOf(layer, 0), QRectF(0, 0, 50, 50));
    QCOMPARE(rectOf(layer, 1), QRectF(100, 0, 50, 50));
}

void tst_AnnotationSelectionController::plainClickOnGroupMemberRecordsNothing()
{
    AnnotationLayer layer;
    addScene(layer);
    AnnotationSelectionController controller(&layer);
    layer.setSelectedIds({layer.idAt(0), layer.idAt(1)});
    const auto history = layer.historyCommandCount();
    QVERIFY(controller.handlePress(QPoint(25, 25), Qt::NoModifier, true));
    QVERIFY(controller.handleRelease(QPoint(25, 25)));
    QCOMPARE(layer.historyCommandCount(), history);
    QCOMPARE(layer.selectionCount(), 2);
}

void tst_AnnotationSelectionController::pressOutsideGroupClearsItAndFallsThrough()
{
    AnnotationLayer layer;
    addScene(layer);
    AnnotationSelectionController controller(&layer);
    layer.setSelectedIds({layer.idAt(0), layer.idAt(1)});
    QVERIFY(!controller.handlePress(QPoint(225, 25), Qt::NoModifier, true));
    QCOMPARE(layer.selectionCount(), 0);
    layer.setSelectedIds({layer.idAt(0), layer.idAt(1)});
    QVERIFY(!controller.handlePress(QPoint(500, 500), Qt::NoModifier, true));
    QCOMPARE(layer.selectionCount(), 0);
}

void tst_AnnotationSelectionController::marqueeOnlyWhereAllowed()
{
    AnnotationLayer layer;
    addScene(layer);
    AnnotationSelectionController controller(&layer);
    // A host without a marquee (PinWindow) keeps Shift+drag for itself, and
    // a group does not stay selected behind what it draws.
    layer.setSelectedIds({layer.idAt(0), layer.idAt(1)});
    QVERIFY(!controller.handlePress(QPoint(-20, -20), Qt::ShiftModifier, false));
    QVERIFY(!controller.isActive());
    QCOMPARE(layer.selectionCount(), 0);

    QVERIFY(controller.handlePress(QPoint(-20, -20), Qt::ShiftModifier, true));
    QVERIFY(controller.isMarqueeActive());
    controller.handleMove(QPoint(120, 60));
    QCOMPARE(controller.marqueeRect(), QRect(QPoint(-20, -20), QPoint(120, 60)));
    QVERIFY(controller.handleRelease(QPoint(120, 60)));
    QCOMPARE(layer.selectedIds(),
             (std::vector<AnnotationLayer::AnnotationId>{layer.idAt(0), layer.idAt(1)}));
    QVERIFY(controller.marqueeRect().isEmpty());

    // A Shift+click on empty space is not a marquee.
    layer.clearSelection();
    QVERIFY(controller.handlePress(QPoint(500, 500), Qt::ShiftModifier, true));
    QVERIFY(controller.handleRelease(QPoint(501, 500)));
    QCOMPARE(layer.selectionCount(), 0);
}

void tst_AnnotationSelectionController::marqueeAddsToExistingSelection()
{
    AnnotationLayer layer;
    addScene(layer);
    AnnotationSelectionController controller(&layer);
    layer.setSelectedIndex(3);   // the arrow
    QVERIFY(controller.handlePress(QPoint(190, -10), Qt::ShiftModifier, true));
    QVERIFY(controller.handleRelease(QPoint(260, 60)));
    QCOMPARE(layer.selectedIds(),
             (std::vector<AnnotationLayer::AnnotationId>{layer.idAt(2), layer.idAt(3)}));
}

void tst_AnnotationSelectionController::escapeCancelsDragThenClearsGroup()
{
    AnnotationLayer layer;
    addScene(layer);
    AnnotationSelectionController controller(&layer);
    layer.setSelectedIds({layer.idAt(0), layer.idAt(1)});
    const auto history = layer.historyCommandCount();
    controller.handlePress(QPoint(25, 25), Qt::NoModifier, true);
    controller.handleMove(QPoint(60, 60));
    QVERIFY(key(controller, Qt::Key_Escape));
    QVERIFY(!controller.isActive());
    QCOMPARE(rectOf(layer, 0), QRectF(0, 0, 50, 50));
    QCOMPARE(layer.historyCommandCount(), history);
    // The release after a cancelled drag is not consumed as a drag.
    QVERIFY(!controller.handleRelease(QPoint(60, 60)));
    QCOMPARE(layer.selectionCount(), 2);
    QVERIFY(key(controller, Qt::Key_Escape));
    QCOMPARE(layer.selectionCount(), 0);
    // With nothing to cancel, Esc is left to the host (close, cancel capture).
    QVERIFY(!key(controller, Qt::Key_Escape));
    layer.setSelectedIndex(0);
    QVERIFY(!key(controller, Qt::Key_Escape));
}

void tst_AnnotationSelectionController::selectAllDeleteAndNudge()
{
    AnnotationLayer layer;
    AnnotationSelectionController controller(&layer);
    // Nothing selectable: Select All is left to the host.
    QVERIFY(!key(controller, Qt::Key_A, Qt::ControlModifier));
    addScene(layer);
    QVERIFY(key(controller, Qt::Key_A, Qt::ControlModifier));
    QCOMPARE(layer.selectionCount(), 4);

    const auto history = layer.historyCommandCount();
    QVERIFY(key(controller, Qt::Key_Right));
    QVERIFY(key(controller, Qt::Key_Right));
    QVERIFY(key(controller, Qt::Key_Down, Qt::ShiftModifier));
    QCOMPARE(layer.historyCommandCount(), history + 1);
    QCOMPARE(rectOf(layer, 0), QRectF(2, 10, 50, 50));
    // Other modifiers are not nudges.
    QVERIFY(!key(controller, Qt::Key_Right, Qt::ControlModifier));

    QVERIFY(key(controller, Qt::Key_Delete));
    QCOMPARE(layer.itemCount(), size_t(0));
    QCOMPARE(layer.historyCommandCount(), history + 2);
    layer.undo();
    QCOMPARE(layer.itemCount(), size_t(4));
    QVERIFY(!key(controller, Qt::Key_Delete));   // nothing selected
    layer.setSelectedIndex(0);
    QVERIFY(!key(controller, Qt::Key_Delete));   // a single selection is the host's
    QCOMPARE(layer.itemCount(), size_t(4));
}

void tst_AnnotationSelectionController::nudgeFollowsHostMapping()
{
    AnnotationLayer layer;
    addScene(layer);
    AnnotationSelectionController controller(&layer);
    layer.setSelectedIds({layer.idAt(0), layer.idAt(1)});
    // A pin rotated 90 degrees: right on screen is up in the image.
    const auto rotated = [](const QPoint& step) { return QPointF(step.y(), -step.x()); };
    QVERIFY(key(controller, Qt::Key_Right, Qt::NoModifier, rotated));
    QCOMPARE(rectOf(layer, 0), QRectF(0, -1, 50, 50));
    // Without a group, arrows stay with the host (moving the capture area).
    layer.setSelectedIndex(0);
    QVERIFY(!key(controller, Qt::Key_Right));
}

void tst_AnnotationSelectionController::drawsOutlinesOnlyForGroups()
{
    AnnotationLayer layer;
    addScene(layer);
    AnnotationSelectionController controller(&layer);
    layer.setSelectedIndex(0);
    QVERIFY(controller.visualRect().isEmpty());
    layer.setSelectedIds({layer.idAt(0), layer.idAt(2)});
    QVERIFY(controller.visualRect().contains(layer.selectionBoundingRect()));

    auto paintedPixels = [&controller] {
        QImage image(300, 300, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        controller.draw(painter);
        painter.end();
        int count = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                count += image.pixelColor(x, y) != QColor(Qt::white);
        return count;
    };
    QVERIFY(paintedPixels() > 0);
    layer.setSelectedIndex(0);   // a single selection keeps its own gizmo
    QCOMPARE(paintedPixels(), 0);
}

void tst_AnnotationSelectionController::missedReleaseKeepsTheDrag()
{
    AnnotationLayer layer;
    addScene(layer);
    AnnotationSelectionController controller(&layer);
    layer.setSelectedIds({layer.idAt(0), layer.idAt(1)});
    const auto history = layer.historyCommandCount();
    controller.handlePress(QPoint(25, 25), Qt::NoModifier, true);
    controller.handleMove(QPoint(35, 25));
    // The release was lost; the next press records the drag, as a single
    // object's gesture would.
    controller.handlePress(QPoint(500, 500), Qt::NoModifier, true);
    QCOMPARE(rectOf(layer, 0), QRectF(10, 0, 50, 50));
    QCOMPARE(layer.historyCommandCount(), history + 1);
}

QTEST_MAIN(tst_AnnotationSelectionController)
#include "tst_AnnotationSelectionController.moc"
