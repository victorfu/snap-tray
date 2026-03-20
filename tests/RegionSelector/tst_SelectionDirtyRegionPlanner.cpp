#include <QtTest>

#include "region/SelectionDirtyRegionPlanner.h"

namespace {
QRect probeRectInside(const QRect& rect)
{
    return QRect(rect.topLeft() + QPoint(20, 20), QSize(16, 16));
}
}

class tst_SelectionDirtyRegionPlanner : public QObject
{
    Q_OBJECT

private slots:
    void testMagnifierRectFlipsNearEdges();
    void testSelectionDragIncludesExpandedSelectionRects();
    void testSelectionDragIncludesToolbarAndRegionControlHistory();
    void testSelectionDragSuppressionSkipsFloatingUiRects();
    void testHoverRegionIncludesOldAndNewMagnifier();
};

void tst_SelectionDirtyRegionPlanner::testMagnifierRectFlipsNearEdges()
{
    SelectionDirtyRegionPlanner planner;
    const QSize viewportSize(500, 400);

    const QPoint centerCursor(100, 100);
    const QRect centerRect = planner.magnifierRectForCursor(centerCursor, viewportSize);
    QVERIFY(centerRect.left() > centerCursor.x());
    QVERIFY(centerRect.top() > centerCursor.y());

    const QPoint edgeCursor(490, 390);
    const QRect edgeRect = planner.magnifierRectForCursor(edgeCursor, viewportSize);
    QVERIFY(edgeRect.left() < edgeCursor.x());
    QVERIFY(edgeRect.top() < edgeCursor.y());
    QVERIFY(edgeRect.right() < viewportSize.width());
    QVERIFY(edgeRect.bottom() < viewportSize.height());
}

void tst_SelectionDirtyRegionPlanner::testSelectionDragIncludesExpandedSelectionRects()
{
    SelectionDirtyRegionPlanner planner;
    SelectionDirtyRegionPlanner::SelectionDragParams params;

    params.currentSelectionRect = QRect(220, 180, 200, 130);
    params.lastSelectionRect = QRect(120, 90, 180, 110);
    params.currentMagnifierRect = QRect(560, 120, 190, 215);
    params.lastMagnifierRect = QRect(420, 110, 190, 215);
    params.viewportSize = QSize(1200, 800);

    const QRegion dirty = planner.planSelectionDragRegion(params);

    const int margin = SelectionDirtyRegionPlanner::kSelectionHandleMargin;
    QVERIFY(dirty.contains(params.currentSelectionRect.adjusted(-margin, -margin, margin, margin)));
    QVERIFY(dirty.contains(params.lastSelectionRect.adjusted(-margin, -margin, margin, margin)));

    const int dimPadding = SelectionDirtyRegionPlanner::kDimensionInfoPadding;
    const QRect currentDimInfo = planner.dimensionInfoRectForSelection(params.currentSelectionRect);
    const QRect lastDimInfo = planner.dimensionInfoRectForSelection(params.lastSelectionRect);
    QVERIFY(dirty.contains(currentDimInfo.adjusted(
        -dimPadding, -dimPadding, dimPadding, dimPadding)));
    QVERIFY(dirty.contains(lastDimInfo.adjusted(
        -dimPadding, -dimPadding, dimPadding, dimPadding)));
}

void tst_SelectionDirtyRegionPlanner::testSelectionDragIncludesToolbarAndRegionControlHistory()
{
    SelectionDirtyRegionPlanner planner;
    SelectionDirtyRegionPlanner::SelectionDragParams params;

    params.currentSelectionRect = QRect(320, 220, 180, 120);
    params.lastSelectionRect = QRect(320, 220, 180, 120);
    params.currentToolbarRect = QRect(360, 360, 240, 44);
    params.lastToolbarRect = QRect(290, 320, 240, 44);
    params.currentRegionControlRect = QRect(320, 170, 110, 32);
    params.lastRegionControlRect = QRect(250, 140, 110, 32);
    params.currentMagnifierRect = QRect(600, 140, 190, 215);
    params.lastMagnifierRect = QRect(600, 140, 190, 215);
    params.viewportSize = QSize(1280, 720);

    const QRegion dirty = planner.planSelectionDragRegion(params);
    const int padding = SelectionDirtyRegionPlanner::kWidgetPadding;

    QVERIFY(dirty.contains(params.currentToolbarRect.adjusted(-padding, -padding, padding, padding)));
    QVERIFY(dirty.contains(params.lastToolbarRect.adjusted(-padding, -padding, padding, padding)));
    QVERIFY(dirty.contains(params.currentRegionControlRect.adjusted(-padding, -padding, padding, padding)));
    QVERIFY(dirty.contains(params.lastRegionControlRect.adjusted(-padding, -padding, padding, padding)));
}

void tst_SelectionDirtyRegionPlanner::testSelectionDragSuppressionSkipsFloatingUiRects()
{
    SelectionDirtyRegionPlanner planner;
    SelectionDirtyRegionPlanner::SelectionDragParams params;

    params.currentSelectionRect = QRect(240, 180, 160, 100);
    params.lastSelectionRect = QRect(220, 180, 160, 100);
    params.currentToolbarRect = QRect(1020, 520, 240, 44);
    params.lastToolbarRect = QRect(980, 500, 240, 44);
    params.currentRegionControlRect = QRect(1090, 120, 110, 32);
    params.lastRegionControlRect = QRect(1040, 120, 110, 32);
    params.currentMagnifierRect = QRect(640, 150, 190, 215);
    params.lastMagnifierRect = QRect(520, 150, 190, 215);
    params.viewportSize = QSize(1440, 900);
    params.suppressFloatingUi = true;
    params.includeMagnifier = false;

    const QRegion dirty = planner.planSelectionDragRegion(params);
    const int margin = SelectionDirtyRegionPlanner::kSelectionHandleMargin;
    const int dimPadding = SelectionDirtyRegionPlanner::kDimensionInfoPadding;

    QVERIFY(dirty.contains(params.currentSelectionRect.adjusted(-margin, -margin, margin, margin)));
    QVERIFY(dirty.contains(params.lastSelectionRect.adjusted(-margin, -margin, margin, margin)));

    const QRect currentDimInfo = planner.dimensionInfoRectForSelection(params.currentSelectionRect);
    QVERIFY(dirty.contains(currentDimInfo.adjusted(
        -dimPadding, -dimPadding, dimPadding, dimPadding)));

    QVERIFY(!dirty.contains(probeRectInside(params.currentToolbarRect)));
    QVERIFY(!dirty.contains(probeRectInside(params.lastToolbarRect)));
    QVERIFY(!dirty.contains(probeRectInside(params.currentRegionControlRect)));
    QVERIFY(!dirty.contains(probeRectInside(params.lastRegionControlRect)));
    QVERIFY(!dirty.contains(probeRectInside(params.currentMagnifierRect)));
    QVERIFY(!dirty.contains(probeRectInside(params.lastMagnifierRect)));
}

void tst_SelectionDirtyRegionPlanner::testHoverRegionIncludesOldAndNewMagnifier()
{
    SelectionDirtyRegionPlanner planner;
    SelectionDirtyRegionPlanner::HoverParams params;
    params.currentMagnifierRect = QRect(680, 200, 190, 215);
    params.lastMagnifierRect = QRect(540, 180, 190, 215);
    params.viewportSize = QSize(1600, 900);

    const QRegion dirty = planner.planHoverRegion(params);
    QVERIFY(dirty.contains(params.currentMagnifierRect));
    QVERIFY(dirty.contains(params.lastMagnifierRect));
}

QTEST_MAIN(tst_SelectionDirtyRegionPlanner)
#include "tst_SelectionDirtyRegionPlanner.moc"
