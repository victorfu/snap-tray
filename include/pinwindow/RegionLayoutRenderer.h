#ifndef REGIONLAYOUTRENDERER_H
#define REGIONLAYOUTRENDERER_H

#include <QRect>
#include <QColor>
#include <QPoint>
#include <QVector>

class QPainter;
struct LayoutRegion;

/**
 * @brief Static rendering utilities for Region Layout Mode.
 *
 * Provides drawing functions for region borders, resize handles,
 * index badges, and confirm/cancel buttons.
 */
class RegionLayoutRenderer
{
public:
    /**
     * @brief Draw all regions with borders and badges.
     * @param painter The painter to draw with
     * @param regions The regions to draw
     * @param selectedIndex Index of selected region (-1 for none)
     * @param hoveredIndex Index of hovered region (-1 for none)
     * @param dpr Device pixel ratio
     */
    static void drawRegions(QPainter& painter,
                           const QVector<LayoutRegion>& regions,
                           int selectedIndex,
                           int hoveredIndex,
                           qreal dpr);

    /**
     * @brief Draw a region's border.
     * @param painter The painter to draw with
     * @param region The region to draw
     * @param isSelected Whether this region is selected
     * @param isHovered Whether this region is hovered
     */
    static void drawRegionBorder(QPainter& painter,
                                 const LayoutRegion& region,
                                 bool isSelected,
                                 bool isHovered);

    /**
     * @brief Draw resize handles for the selected region.
     * @param painter The painter to draw with
     * @param rect The region's rect
     * @param color The region's color (for handle stroke)
     */
    static void drawResizeHandles(QPainter& painter,
                                  const QRect& rect,
                                  const QColor& color);

    /**
     * @brief Draw index badge for a region.
     * @param painter The painter to draw with
     * @param rect The region's rect
     * @param index The 1-based index to display
     * @param color The badge background color
     */
    static void drawIndexBadge(QPainter& painter,
                               const QRect& rect,
                               int index,
                               const QColor& color);

    /**
     * @brief Draw confirm and cancel buttons.
     * @param painter The painter to draw with
     * @param canvasBounds The canvas bounding rect
     * @param hoverPos Current mouse position for hover effects
     */
    static void drawConfirmCancelButtons(QPainter& painter,
                                         const QRect& canvasBounds,
                                         const QPoint& hoverPos);

    /**
     * @brief Get the confirm button rectangle.
     * @param canvasBounds The canvas bounding rect
     * @return The confirm button rect
     */
    static QRect confirmButtonRect(const QRect& canvasBounds);

    /**
     * @brief Get the cancel button rectangle.
     * @param canvasBounds The canvas bounding rect
     * @return The cancel button rect
     */
    static QRect cancelButtonRect(const QRect& canvasBounds);

private:
    RegionLayoutRenderer() = delete;  // Static class, no instantiation
};

#endif // REGIONLAYOUTRENDERER_H
