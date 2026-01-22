#ifndef GLASSRENDERER_H
#define GLASSRENDERER_H

#include <QPainter>
#include <QRect>
#include "ToolbarStyle.h"

/**
 * @brief Utility class for rendering macOS-style glass effects.
 *
 * Provides static methods to draw glass panels with:
 * - Multi-layer soft shadows
 * - Semi-transparent backgrounds with gradient simulation
 * - Hairline borders
 * - Inner highlight for depth effect
 */
class GlassRenderer {
public:
    /**
     * @brief Draw a complete glass panel with all effects.
     * @param painter The QPainter to draw with
     * @param rect The rectangle to draw the glass panel in
     * @param config The toolbar style configuration
     * @param radiusOverride Optional radius override (-1 to use config.cornerRadius)
     */
    static void drawGlassPanel(QPainter& painter,
                               const QRect& rect,
                               const ToolbarStyleConfig& config,
                               int radiusOverride = -1);

    /**
     * @brief Draw only the glass background (no shadow or border).
     * Useful for tooltips or overlays that need custom handling.
     */
    static void drawGlassBackground(QPainter& painter,
                                    const QRect& rect,
                                    const ToolbarStyleConfig& config,
                                    int radius);

private:
    /**
     * @brief Draw hairline border with low opacity.
     */
    static void drawHairlineBorder(QPainter& painter,
                                   const QRect& rect,
                                   const ToolbarStyleConfig& config,
                                   int radius);

    /**
     * @brief Draw inner highlight at top edge for depth effect.
     */
    static void drawInnerHighlight(QPainter& painter,
                                   const QRect& rect,
                                   const ToolbarStyleConfig& config,
                                   int radius);
};

#endif // GLASSRENDERER_H
