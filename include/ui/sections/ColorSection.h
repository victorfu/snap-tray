#ifndef COLORSECTION_H
#define COLORSECTION_H

#include <QObject>
#include <QVector>
#include <QColor>
#include <QRect>
#include "ui/IWidgetSection.h"

/**
 * @brief Color palette section for ColorAndWidthWidget.
 *
 * Displays a grid of color swatches (16 colors in 2 rows of 8) with an optional
 * "..." button.
 * Emits signals when a color is selected or "more colors" is requested.
 */
class ColorSection : public QObject, public IWidgetSection
{
    Q_OBJECT

public:
    explicit ColorSection(QObject* parent = nullptr);

    // =========================================================================
    // Color Management
    // =========================================================================

    /**
     * @brief Set the available colors in the palette.
     */
    void setColors(const QVector<QColor>& colors);

    /**
     * @brief Get the available colors.
     */
    QVector<QColor> colors() const { return m_colors; }

    /**
     * @brief Set the currently selected color.
     */
    void setCurrentColor(const QColor& color);

    /**
     * @brief Get the currently selected color.
     */
    QColor currentColor() const { return m_currentColor; }


    // =========================================================================
    // IWidgetSection Implementation
    // =========================================================================

    void updateLayout(int containerTop, int containerHeight, int xOffset) override;
    int preferredWidth() const override;
    int preferredHeight() const;
    QRect boundingRect() const override { return m_sectionRect; }
    void draw(QPainter& painter, const ToolbarStyleConfig& styleConfig) override;
    bool contains(const QPoint& pos) const override;
    bool handleClick(const QPoint& pos) override;
    bool updateHovered(const QPoint& pos) override;

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
    /**
     * @brief Get the swatch index at the given position.
     * @return Swatch index, or -1 if not on a swatch
     */
    int swatchAtPosition(const QPoint& pos) const;
    int rowCount() const;

    // State
    QVector<QColor> m_colors;      // Standard preset colors
    QColor m_currentColor;         // Currently selected color
    int m_hoveredSwatch = -1;

    // Layout
    QRect m_sectionRect;
    QRect m_customSwatchRect;      // Custom swatch (first position)
    QVector<QRect> m_swatchRects;  // Standard color swatches

    // Layout constants
    static constexpr int SWATCH_SIZE = 20;       // Larger swatches for better visibility
    static constexpr int SWATCH_SPACING = 3;     // Slightly more spacing
    static constexpr int SECTION_PADDING = 4;
    static constexpr int BORDER_RADIUS = 3;      // Rounded corners
};

#endif // COLORSECTION_H
