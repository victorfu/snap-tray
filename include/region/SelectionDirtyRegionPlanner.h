#ifndef SELECTIONDIRTYREGIONPLANNER_H
#define SELECTIONDIRTYREGIONPLANNER_H

#include <QPoint>
#include <QRect>
#include <QRegion>
#include <QSize>

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
        QSize viewportSize;
        bool includeMagnifier = true;
    };

    struct HoverParams {
        QRect currentMagnifierRect;
        QRect lastMagnifierRect;
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

    QRect magnifierRectForCursor(const QPoint& cursorPos, const QSize& viewportSize) const;
    QRect dimensionInfoRectForSelection(const QRect& selectionRect) const;

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
    static constexpr int kViewportMargin = 10;
};

#endif // SELECTIONDIRTYREGIONPLANNER_H
