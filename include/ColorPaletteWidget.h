#ifndef COLORPALETTEWIDGET_H
#define COLORPALETTEWIDGET_H

#include <QObject>
#include <QVector>
#include <QColor>
#include <QRect>
#include <QPoint>
#include "ToolbarStyle.h"

class QPainter;

/**
 * @brief Reusable color palette picker component.
 *
 * Provides color swatches for annotation tools with hover highlighting,
 * selection indicator, and "more colors" dialog support.
 */
class ColorPaletteWidget : public QObject
{
    Q_OBJECT

public:
    explicit ColorPaletteWidget(QObject* parent = nullptr);

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
     * @brief Set the palette visibility.
     */
    void setVisible(bool visible) { m_visible = visible; }

    /**
     * @brief Check if the palette is visible.
     */
    bool isVisible() const { return m_visible; }

    /**
     * @brief Update the palette position.
     * @param anchorRect The rect to anchor the palette to (e.g., toolbar)
     * @param above If true, position above the anchor; otherwise below
     */
    void updatePosition(const QRect& anchorRect, bool above = true);

    /**
     * @brief Draw the color palette.
     * @param painter The QPainter to draw with
     */
    void draw(QPainter& painter);

    /**
     * @brief Get the bounding rectangle of the palette.
     */
    QRect boundingRect() const { return m_paletteRect; }

    /**
     * @brief Get the swatch index at the given position.
     * @return Swatch index, or -1 if not on any swatch
     */
    int swatchAtPosition(const QPoint& pos) const;

    /**
     * @brief Update the hovered swatch based on mouse position.
     * @return true if the hover state changed
     */
    bool updateHoveredSwatch(const QPoint& pos);

    /**
     * @brief Handle a click at the given position.
     * @return true if the click was handled (was on palette)
     */
    bool handleClick(const QPoint& pos);

    /**
     * @brief Check if position is within the palette.
     */
    bool contains(const QPoint& pos) const;

    /**
     * @brief Handle a double-click at the given position.
     * @return true if the click was handled
     */
    bool handleDoubleClick(const QPoint& pos);

signals:
    /**
     * @brief Emitted when a color is selected from the palette.
     */
    void colorSelected(const QColor& color);

    /**
     * @brief Emitted when preview swatch is clicked to open color picker.
     */
    void customColorPickerRequested();

private:
    void updateSwatchRects();

    QVector<QColor> m_colors;
    QColor m_currentColor;
    bool m_visible;

    QRect m_paletteRect;
    QRect m_customSwatchRect;      // Custom swatch (first position)
    QVector<QRect> m_swatchRects;  // Standard color swatches
    int m_hoveredSwatch;

    // Layout constants
    static constexpr int SWATCH_SIZE = 20;       // Larger swatches for better visibility
    static constexpr int SWATCH_SPACING = 3;     // Slightly more spacing
    static constexpr int ROW_SPACING = 3;
    static constexpr int PALETTE_PADDING = 4;
    static constexpr int BORDER_RADIUS = 3;      // Rounded corners

    // Toolbar style configuration
    ToolbarStyleConfig m_styleConfig;
};

#endif // COLORPALETTEWIDGET_H
