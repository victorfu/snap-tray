#ifndef SELECTIONDIRTYREGIONPLANNER_H
#define SELECTIONDIRTYREGIONPLANNER_H

#include <QPoint>
#include <QRect>
#include <QRegion>
#include <QSize>
#include <QtGlobal>

#include "settings/RegionCaptureSettingsManager.h"

/**
 * @brief Builds dirty regions for RegionSelector drag/hover repaint optimization.
 *
 * This planner keeps repaint scope localized to only the elements that visually
 * move between frames: selection rectangle, dimension info, magnifier, toolbar,
 * and region control widget.
 */
class SelectionDirtyRegionPlanner
{
public:
    struct SelectionDragParams {
        QRect currentSelectionRect;
        QRect lastSelectionRect;
        QRect currentToolbarRect;
        QRect lastToolbarRect;
        QRect currentRegionControlRect;
        QRect lastRegionControlRect;
        QRect currentMagnifierRect;
        QRect lastMagnifierRect;
        QPoint currentCursorPos;
        QPoint lastCursorPos;
        QSize viewportSize;
        qreal devicePixelRatio = 1.0;
        bool ratioLocked = false;
        bool includeMagnifier = true;
        bool suppressFloatingUi = false;
    };

    struct HoverParams {
        QRect currentMagnifierRect;
        QRect lastMagnifierRect;
        QPoint currentCursorPos;
        QPoint lastCursorPos;
        QSize viewportSize;
    };

    // Data-driven repaint margins and geometry constants.
    static constexpr int kSelectionHandleMargin = 12;
    static constexpr int kDimensionInfoHeight = 40;
    static constexpr int kDimensionInfoWidth = 180;
    static constexpr int kDimensionInfoPadding = 6;
    static constexpr int kDimensionInfoTopOffset = 10;
    static constexpr int kWidgetPadding = 6;
    static constexpr int kDragFrameIntervalMs = 8;  // ~120Hz

    // Margin around selection rect for annotation-scoped repaints.
    // Must cover stroke overshoot, selection/gizmo handles, and antialiasing.
    // Any clipping beyond this margin is corrected on drawing finish (full repaint).
    static constexpr int kAnnotationRepaintMargin = 30;

    QRect cursorCompanionRectForCursor(
        RegionCaptureSettingsManager::CursorCompanionStyle style,
        const QPoint& cursorPos,
        const QSize& viewportSize) const;
    QRect magnifierRectForCursor(const QPoint& cursorPos, const QSize& viewportSize) const;
    QRect beaverRectForCursor(const QPoint& cursorPos, const QSize& viewportSize) const;
    QRect dimensionInfoRectForSelection(const QRect& selectionRect,
                                        const QSize& viewportSize,
                                        qreal devicePixelRatio,
                                        bool ratioLocked) const;

    QRegion planSelectionDragRegion(const SelectionDragParams& params) const;
    QRegion planHoverRegion(const HoverParams& params) const;

private:
    QRegion clippedToViewport(const QRegion& region, const QSize& viewportSize) const;

    // Must stay in sync with MagnifierPanel layout.
    static constexpr int kMagnifierWidth = 180;
    static constexpr int kMagnifierHeight = 120;
    static constexpr int kMagnifierInfoHeight = 85;
    static constexpr int kMagnifierOffset = 20;
    static constexpr int kMagnifierOuterPadding = 5;
    static constexpr int kBeaverSize = 88;
    static constexpr int kBeaverOffset = 4;
    static constexpr int kBeaverOuterPadding = 0;
    static constexpr int kCrosshairMargin = 2;
    static constexpr int kViewportMargin = 10;

    QRegion crosshairStripRegion(const QPoint& cursorPos, const QSize& viewportSize) const;
};

#endif // SELECTIONDIRTYREGIONPLANNER_H
