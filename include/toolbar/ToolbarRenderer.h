#ifndef TOOLBARRENDERER_H
#define TOOLBARRENDERER_H

#include "toolbar/ToolbarButtonConfig.h"
#include <QRect>
#include <QVector>
#include <QColor>
#include <QPoint>

class QPainter;
struct ToolbarStyleConfig;

namespace Toolbar {

/**
 * @brief Static utility class for common toolbar rendering operations.
 *
 * Provides shared rendering logic to reduce duplication across different
 * toolbar implementations.
 */
class ToolbarRenderer {
public:
    // Layout constants (matching ToolbarWidget values)
    static constexpr int TOOLBAR_HEIGHT = 32;
    static constexpr int BUTTON_WIDTH = 28;
    static constexpr int BUTTON_SPACING = 2;
    static constexpr int SEPARATOR_WIDTH = 4;
    static constexpr int HORIZONTAL_PADDING = 10;

    /**
     * @brief Calculate toolbar width based on buttons.
     */
    static int calculateWidth(const QVector<ButtonConfig>& buttons);

    /**
     * @brief Calculate button rectangles within a toolbar rect.
     */
    static QVector<QRect> calculateButtonRects(const QRect& toolbarRect,
                                               const QVector<ButtonConfig>& buttons);

    /**
     * @brief Draw separator line.
     */
    static void drawSeparator(QPainter& painter, int x, int y, int height,
                              const ToolbarStyleConfig& style);

    /**
     * @brief Draw tooltip for a button.
     * @param painter The QPainter
     * @param buttonRect The button's rectangle
     * @param tooltip The tooltip text
     * @param style The style configuration
     * @param above If true, draw above the button; otherwise below
     * @param viewportWidth Width of viewport for boundary checking (0 = no check)
     */
    static void drawTooltip(QPainter& painter, const QRect& buttonRect,
                           const QString& tooltip, const ToolbarStyleConfig& style,
                           bool above = true, int viewportWidth = 0);

    /**
     * @brief Hit test: find button index at position.
     * @return Button index, or -1 if none
     */
    static int buttonAtPosition(const QPoint& pos, const QVector<QRect>& buttonRects);

    /**
     * @brief Position toolbar below a reference rect with boundary checks.
     * @param referenceRect The rect to position below (e.g., selection area)
     * @param toolbarWidth Width of the toolbar
     * @param viewportWidth Width of the viewport
     * @param viewportHeight Height of the viewport
     * @param margin Margin from edges
     * @return The calculated toolbar rectangle
     */
    static QRect positionBelow(const QRect& referenceRect, int toolbarWidth,
                               int viewportWidth, int viewportHeight,
                               int margin = 8);

    /**
     * @brief Position toolbar centered at bottom of screen.
     * @param centerX X coordinate of center
     * @param bottomY Y coordinate of bottom
     * @param toolbarWidth Width of the toolbar
     * @return The calculated toolbar rectangle
     */
    static QRect positionAtBottom(int centerX, int bottomY, int toolbarWidth);

    /**
     * @brief Get icon color based on button config and state.
     * @param config The button configuration
     * @param isActive Whether the button is active
     * @param isHovered Whether the button is hovered
     * @param style The style configuration
     * @return The calculated icon color
     */
    static QColor getIconColor(const ButtonConfig& config, bool isActive,
                               bool isHovered, const ToolbarStyleConfig& style);
};

} // namespace Toolbar

#endif // TOOLBARRENDERER_H
