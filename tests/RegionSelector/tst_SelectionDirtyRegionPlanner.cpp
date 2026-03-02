#include <QtTest>

#include "region/SelectionDirtyRegionPlanner.h"

namespace {
constexpr int kCrosshairMargin = 2;
}

class tst_SelectionDirtyRegionPlanner : public QObject
{
    Q_OBJECT

private slots:
    void testMagnifierRectFlipsNearEdges();
    void testSelectionDragIncludesExpandedSelectionRects();
    void testSelectionDragIncludesToolbarAndRegionControlHistory();
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
    params.currentCursorPos = QPoint(300, 260);
    params.lastCursorPos = QPoint(240, 220);
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

    const QRect currentHStrip(
        0,
        params.currentCursorPos.y() - kCrosshairMargin,
        params.viewportSize.width(),
        kCrosshairMargin * 2 + 1);
    const QRect lastHStrip(
        0,
        params.lastCursorPos.y() - kCrosshairMargin,
        params.viewportSize.width(),
        kCrosshairMargin * 2 + 1);
    QVERIFY(dirty.contains(currentHStrip));
    QVERIFY(dirty.contains(lastHStrip));
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
    params.currentCursorPos = QPoint(410, 260);
    params.lastCursorPos = QPoint(360, 235);
    params.viewportSize = QSize(1280, 720);

    const QRegion dirty = planner.planSelectionDragRegion(params);
    const int padding = SelectionDirtyRegionPlanner::kWidgetPadding;

    QVERIFY(dirty.contains(params.currentToolbarRect.adjusted(-padding, -padding, padding, padding)));
    QVERIFY(dirty.contains(params.lastToolbarRect.adjusted(-padding, -padding, padding, padding)));
    QVERIFY(dirty.contains(params.currentRegionControlRect.adjusted(-padding, -padding, padding, padding)));
    QVERIFY(dirty.contains(params.lastRegionControlRect.adjusted(-padding, -padding, padding, padding)));
}

void tst_SelectionDirtyRegionPlanner::testHoverRegionIncludesOldAndNewMagnifier()
{
    SelectionDirtyRegionPlanner planner;
    SelectionDirtyRegionPlanner::HoverParams params;
    params.currentMagnifierRect = QRect(680, 200, 190, 215);
    params.lastMagnifierRect = QRect(540, 180, 190, 215);
    params.currentCursorPos = QPoint(760, 320);
    params.lastCursorPos = QPoint(620, 260);
    params.viewportSize = QSize(1600, 900);

    const QRegion dirty = planner.planHoverRegion(params);
    QVERIFY(dirty.contains(params.currentMagnifierRect));
    QVERIFY(dirty.contains(params.lastMagnifierRect));

    const QRect currentVerticalStrip(
        params.currentCursorPos.x() - kCrosshairMargin,
        0,
        kCrosshairMargin * 2 + 1,
        params.viewportSize.height());
    const QRect lastVerticalStrip(
        params.lastCursorPos.x() - kCrosshairMargin,
        0,
        kCrosshairMargin * 2 + 1,
        params.viewportSize.height());
    QVERIFY(dirty.contains(currentVerticalStrip));
    QVERIFY(dirty.contains(lastVerticalStrip));
}

QTEST_MAIN(tst_SelectionDirtyRegionPlanner)
#include "tst_SelectionDirtyRegionPlanner.moc"
