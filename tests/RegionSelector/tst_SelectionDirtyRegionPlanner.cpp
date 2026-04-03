#include <QtTest>

#include <QFont>
#include <QRegion>

#include "region/SelectionDimensionLabel.h"
#include "region/SelectionDirtyRegionPlanner.h"

namespace {
QRect probeRectInside(const QRect& rect)
{
    return QRect(rect.topLeft() + QPoint(20, 20), QSize(16, 16));
}

QRect paddedBoldDimensionRect(const QRect& selectionRect, const QSize& viewportSize)
{
    QFont font;
    font.setPointSize(12);
    font.setBold(true);

    return SelectionDimensionLabel::selectionPanelLayout(
               selectionRect,
               SelectionDimensionLabel::widgetLabel(selectionRect, 1.0),
               font,
               viewportSize,
               QSize())
        .panelRect
        .adjusted(-SelectionDirtyRegionPlanner::kDimensionInfoPadding,
                  -SelectionDirtyRegionPlanner::kDimensionInfoPadding,
                  SelectionDirtyRegionPlanner::kDimensionInfoPadding,
                  SelectionDirtyRegionPlanner::kDimensionInfoPadding);
}
}

class tst_SelectionDirtyRegionPlanner : public QObject
{
    Q_OBJECT

private slots:
    void testMagnifierRectFlipsNearEdges();
    void testSelectionDragIncludesExpandedSelectionRects();
    void testSelectionDragSkipsStableInterior();
    void testSelectionDragIncludesToolbarAndRegionControlHistory();
    void testSelectionDragSuppressionSkipsFloatingUiRects();
    void testSelectionDragTracksCompactDimensionLayoutTransition();
    void testSelectionDragCoversBoldDimensionLabelLayoutAtCompactThreshold();
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
    params.currentCursorPos = QPoint(300, 260);
    params.lastCursorPos = QPoint(240, 220);
    params.viewportSize = QSize(1200, 800);

    const QRegion dirty = planner.planSelectionDragRegion(params);

    const int margin = SelectionDirtyRegionPlanner::kSelectionHandleMargin;
    QVERIFY(dirty.contains(params.currentSelectionRect.adjusted(-margin, -margin, margin, margin)));
    QVERIFY(dirty.contains(params.lastSelectionRect.adjusted(-margin, -margin, margin, margin)));

    const int dimPadding = SelectionDirtyRegionPlanner::kDimensionInfoPadding;
    const QRect currentDimInfo = planner.dimensionInfoRectForSelection(
        params.currentSelectionRect, params.viewportSize, params.devicePixelRatio, params.ratioLocked);
    const QRect lastDimInfo = planner.dimensionInfoRectForSelection(
        params.lastSelectionRect, params.viewportSize, params.devicePixelRatio, params.ratioLocked);
    QVERIFY(dirty.contains(currentDimInfo.adjusted(
        -dimPadding, -dimPadding, dimPadding, dimPadding)));
    QVERIFY(dirty.contains(lastDimInfo.adjusted(
        -dimPadding, -dimPadding, dimPadding, dimPadding)));

    QVERIFY(dirty.contains(QRect(340, 240, 20, 20))); // Current-only selection growth.
    QVERIFY(dirty.contains(QRect(140, 110, 20, 20))); // Last-only selection area.
}

void tst_SelectionDirtyRegionPlanner::testSelectionDragSkipsStableInterior()
{
    SelectionDirtyRegionPlanner planner;
    SelectionDirtyRegionPlanner::SelectionDragParams params;

    params.currentSelectionRect = QRect(100, 100, 320, 220);
    params.lastSelectionRect = QRect(110, 110, 320, 220);
    params.currentCursorPos = QPoint(420, 320);
    params.lastCursorPos = QPoint(430, 330);
    params.viewportSize = QSize(1600, 900);

    const QRegion dirty = planner.planSelectionDragRegion(params);

    const QRect stableInterior(180, 180, 80, 60);
    QVERIFY(!dirty.contains(stableInterior));
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
    params.currentCursorPos = QPoint(320, 230);
    params.lastCursorPos = QPoint(300, 230);
    params.viewportSize = QSize(1440, 900);
    params.suppressFloatingUi = true;
    params.includeMagnifier = false;

    const QRegion dirty = planner.planSelectionDragRegion(params);
    const int margin = SelectionDirtyRegionPlanner::kSelectionHandleMargin;
    const int dimPadding = SelectionDirtyRegionPlanner::kDimensionInfoPadding;

    QVERIFY(dirty.contains(params.currentSelectionRect.adjusted(-margin, -margin, margin, margin)));
    QVERIFY(dirty.contains(params.lastSelectionRect.adjusted(-margin, -margin, margin, margin)));

    const QRect currentDimInfo = planner.dimensionInfoRectForSelection(
        params.currentSelectionRect, params.viewportSize, params.devicePixelRatio, params.ratioLocked);
    QVERIFY(dirty.contains(currentDimInfo.adjusted(
        -dimPadding, -dimPadding, dimPadding, dimPadding)));

    QVERIFY(!dirty.contains(probeRectInside(params.currentToolbarRect)));
    QVERIFY(!dirty.contains(probeRectInside(params.lastToolbarRect)));
    QVERIFY(!dirty.contains(probeRectInside(params.currentRegionControlRect)));
    QVERIFY(!dirty.contains(probeRectInside(params.lastRegionControlRect)));
    QVERIFY(!dirty.contains(probeRectInside(params.currentMagnifierRect)));
    QVERIFY(!dirty.contains(probeRectInside(params.lastMagnifierRect)));
}

void tst_SelectionDirtyRegionPlanner::testSelectionDragTracksCompactDimensionLayoutTransition()
{
    SelectionDirtyRegionPlanner planner;
    SelectionDirtyRegionPlanner::SelectionDragParams params;

    params.currentSelectionRect = QRect(240, 6, 120, 30);
    params.lastSelectionRect = QRect(220, 6, 260, 140);
    params.currentCursorPos = QPoint(300, 30);
    params.lastCursorPos = QPoint(320, 60);
    params.viewportSize = QSize(1440, 900);

    const QRegion dirty = planner.planSelectionDragRegion(params);
    const int dimPadding = SelectionDirtyRegionPlanner::kDimensionInfoPadding;
    const QRect currentDimInfo = planner.dimensionInfoRectForSelection(
        params.currentSelectionRect, params.viewportSize, params.devicePixelRatio, params.ratioLocked);
    const QRect lastDimInfo = planner.dimensionInfoRectForSelection(
        params.lastSelectionRect, params.viewportSize, params.devicePixelRatio, params.ratioLocked);

    QCOMPARE(currentDimInfo.top(), params.currentSelectionRect.top());
    QVERIFY(currentDimInfo.right() < params.currentSelectionRect.left());
    QVERIFY(params.currentSelectionRect.left() - currentDimInfo.right() >= 8);
    QCOMPARE(lastDimInfo.top(), params.lastSelectionRect.top() + 5);
    QCOMPARE(lastDimInfo.left(), params.lastSelectionRect.left() + 5);
    QVERIFY(dirty.contains(currentDimInfo.adjusted(
        -dimPadding, -dimPadding, dimPadding, dimPadding)));
    QVERIFY(dirty.contains(lastDimInfo.adjusted(
        -dimPadding, -dimPadding, dimPadding, dimPadding)));
}

void tst_SelectionDirtyRegionPlanner::testSelectionDragCoversBoldDimensionLabelLayoutAtCompactThreshold()
{
    SelectionDirtyRegionPlanner planner;
    SelectionDirtyRegionPlanner::SelectionDragParams params;

    params.currentSelectionRect = QRect(200, 100, 136, 60);
    params.lastSelectionRect = QRect(200, 100, 137, 60);
    params.currentCursorPos = QPoint(260, 140);
    params.lastCursorPos = QPoint(261, 140);
    params.viewportSize = QSize(800, 600);

    const QRegion dirty = planner.planSelectionDragRegion(params);
    const QRegion missingCurrent =
        QRegion(paddedBoldDimensionRect(params.currentSelectionRect, params.viewportSize)).subtracted(dirty);
    const QRegion missingLast =
        QRegion(paddedBoldDimensionRect(params.lastSelectionRect, params.viewportSize)).subtracted(dirty);

    QVERIFY2(missingCurrent.isEmpty(), "Dirty region missed the current bold dimension label bounds.");
    QVERIFY2(missingLast.isEmpty(), "Dirty region missed the previous bold dimension label bounds.");
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
    QVERIFY(dirty.isEmpty());
}

QTEST_MAIN(tst_SelectionDirtyRegionPlanner)
#include "tst_SelectionDirtyRegionPlanner.moc"
