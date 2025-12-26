#ifndef IWIDGETSECTION_H
#define IWIDGETSECTION_H

#include <QRect>
#include <QPoint>

class QPainter;
struct ToolbarStyleConfig;

/**
 * @brief Abstract interface for widget sections within ColorAndWidthWidget.
 *
 * Each section (Color, Width, Text, ArrowStyle, Shape) implements this interface
 * to provide consistent layout, rendering, and event handling behavior.
 */
class IWidgetSection
{
public:
    virtual ~IWidgetSection() = default;

    // =========================================================================
    // Layout
    // =========================================================================

    /**
     * @brief Update the section's layout based on container position.
     * @param containerTop The top Y coordinate of the container widget
     * @param containerHeight The height of the container widget
     * @param xOffset The X offset where this section should start
     */
    virtual void updateLayout(int containerTop, int containerHeight, int xOffset) = 0;

    /**
     * @brief Get the preferred width of this section.
     * @return Width in pixels
     */
    virtual int preferredWidth() const = 0;

    /**
     * @brief Get the bounding rectangle of this section.
     * @return The section's bounding rect
     */
    virtual QRect boundingRect() const = 0;

    // =========================================================================
    // Rendering
    // =========================================================================

    /**
     * @brief Draw the section.
     * @param painter The QPainter to draw with
     * @param styleConfig The toolbar style configuration for theming
     */
    virtual void draw(QPainter& painter, const ToolbarStyleConfig& styleConfig) = 0;

    // =========================================================================
    // Hit Testing
    // =========================================================================

    /**
     * @brief Check if a point is within this section.
     * @param pos The point to test
     * @return true if the point is within the section bounds
     */
    virtual bool contains(const QPoint& pos) const = 0;

    // =========================================================================
    // Event Handling
    // =========================================================================

    /**
     * @brief Handle a click event.
     * @param pos The click position
     * @return true if the click was handled
     */
    virtual bool handleClick(const QPoint& pos) = 0;

    /**
     * @brief Update hover state based on mouse position.
     * @param pos The mouse position
     * @return true if the hover state changed (requiring repaint)
     */
    virtual bool updateHovered(const QPoint& pos) = 0;

    /**
     * @brief Reset all hover state.
     */
    virtual void resetHoverState() = 0;
};

#endif // IWIDGETSECTION_H
