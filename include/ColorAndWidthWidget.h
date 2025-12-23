#ifndef COLORANDWIDTHWIDGET_H
#define COLORANDWIDTHWIDGET_H

#include <QObject>
#include <QVector>
#include <QColor>
#include <QRect>
#include <QPoint>

class QPainter;

/**
 * @brief Unified color palette and line width picker component.
 *
 * Combines ColorPaletteWidget and LineWidthWidget into a single horizontal
 * component with color swatches on the left and line width slider on the right.
 * Reduces vertical toolbar space from 60px to 32px.
 */
class ColorAndWidthWidget : public QObject
{
    Q_OBJECT

public:
    explicit ColorAndWidthWidget(QObject* parent = nullptr);

    // Color palette methods
    /**
     * @brief Set the available colors in the palette.
     */
    void setColors(const QVector<QColor>& colors);

    /**
     * @brief Set the currently selected color.
     */
    void setCurrentColor(const QColor& color);

    /**
     * @brief Get the currently selected color.
     */
    QColor currentColor() const { return m_currentColor; }

    /**
     * @brief Set whether to show the "more colors" button.
     */
    void setShowMoreButton(bool show) { m_showMoreButton = show; }

    // Line width methods
    /**
     * @brief Set the width range.
     */
    void setWidthRange(int min, int max);

    /**
     * @brief Set the current width value.
     */
    void setCurrentWidth(int width);

    /**
     * @brief Get the current width value.
     */
    int currentWidth() const { return m_currentWidth; }

    // Visibility and positioning
    /**
     * @brief Set the widget visibility.
     */
    void setVisible(bool visible) { m_visible = visible; }

    /**
     * @brief Check if the widget is visible.
     */
    bool isVisible() const { return m_visible; }

    /**
     * @brief Update the widget position.
     * @param anchorRect The rect to anchor the widget to (e.g., toolbar)
     * @param above If true, position above the anchor; otherwise below
     * @param screenWidth Screen width for right boundary check (0 to disable)
     */
    void updatePosition(const QRect& anchorRect, bool above = false, int screenWidth = 0);

    /**
     * @brief Get the bounding rectangle of the widget.
     */
    QRect boundingRect() const { return m_widgetRect; }

    // Rendering
    /**
     * @brief Draw the unified widget.
     * @param painter The QPainter to draw with
     */
    void draw(QPainter& painter);

    // Event handling
    /**
     * @brief Check if position is within the widget.
     */
    bool contains(const QPoint& pos) const;

    /**
     * @brief Handle a click at the given position.
     * @return true if the click was handled (was on widget)
     */
    bool handleClick(const QPoint& pos);

    /**
     * @brief Handle mouse press event.
     * @return true if the event was handled
     */
    bool handleMousePress(const QPoint& pos);

    /**
     * @brief Handle mouse move event.
     * @return true if the event was handled
     */
    bool handleMouseMove(const QPoint& pos, bool pressed);

    /**
     * @brief Handle mouse release event.
     * @return true if the event was handled
     */
    bool handleMouseRelease(const QPoint& pos);

    /**
     * @brief Update hover state.
     * @return true if the state changed
     */
    bool updateHovered(const QPoint& pos);

signals:
    /**
     * @brief Emitted when a color is selected from the palette.
     */
    void colorSelected(const QColor& color);

    /**
     * @brief Emitted when "more colors" button is clicked.
     */
    void moreColorsRequested();

    /**
     * @brief Emitted when the width value changes.
     */
    void widthChanged(int width);

private:
    // Layout calculation
    void updateLayout();

    // Color section helpers
    void drawColorSection(QPainter& painter);
    int swatchAtPosition(const QPoint& pos) const;

    // Width section helpers
    void drawWidthSection(QPainter& painter);
    int positionToWidth(int x) const;
    int widthToPosition(int width) const;
    bool isInWidthSection(const QPoint& pos) const;

    // Color state
    QVector<QColor> m_colors;
    QColor m_currentColor;
    bool m_showMoreButton;
    QVector<QRect> m_swatchRects;
    int m_hoveredSwatch;

    // Width state
    int m_minWidth;
    int m_maxWidth;
    int m_currentWidth;
    bool m_isDragging;
    bool m_widthSectionHovered;

    // Layout state
    bool m_visible;
    QRect m_widgetRect;
    QRect m_colorSectionRect;
    QRect m_widthSectionRect;
    QRect m_sliderTrackRect;
    QRect m_previewRect;
    QRect m_labelRect;

    // Layout constants
    static const int WIDGET_HEIGHT = 36;
    static const int SWATCH_SIZE = 16;
    static const int SWATCH_SPACING = 2;
    static const int COLORS_PER_ROW = 8;
    static const int SECTION_PADDING = 6;
    static const int SECTION_SPACING = 8;
    static const int SLIDER_WIDTH = 160;
    static const int SLIDER_HEIGHT = 6;
    static const int HANDLE_SIZE = 14;
    static const int PREVIEW_SIZE = 18;
};

#endif // COLORANDWIDTHWIDGET_H
