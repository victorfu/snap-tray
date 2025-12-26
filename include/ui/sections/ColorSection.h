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
 * Displays a grid of color swatches (15 colors in 2 rows of 8, with "..." button).
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

    /**
     * @brief Set whether to show the "more colors" button.
     */
    void setShowMoreButton(bool show) { m_showMoreButton = show; }

    /**
     * @brief Check if the "more colors" button is shown.
     */
    bool showMoreButton() const { return m_showMoreButton; }

    // =========================================================================
    // IWidgetSection Implementation
    // =========================================================================

    void updateLayout(int containerTop, int containerHeight, int xOffset) override;
    int preferredWidth() const override;
    QRect boundingRect() const override { return m_sectionRect; }
    void draw(QPainter& painter, const ToolbarStyleConfig& styleConfig) override;
    bool contains(const QPoint& pos) const override;
    bool handleClick(const QPoint& pos) override;
    bool updateHovered(const QPoint& pos) override;
    void resetHoverState() override;

signals:
    /**
     * @brief Emitted when a color is selected from the palette.
     */
    void colorSelected(const QColor& color);

    /**
     * @brief Emitted when "more colors" button is clicked.
     */
    void moreColorsRequested();

private:
    /**
     * @brief Get the swatch index at the given position.
     * @return Swatch index, or -1 if not on a swatch
     */
    int swatchAtPosition(const QPoint& pos) const;

    // State
    QVector<QColor> m_colors;
    QColor m_currentColor;
    bool m_showMoreButton = true;
    int m_hoveredSwatch = -1;

    // Layout
    QRect m_sectionRect;
    QVector<QRect> m_swatchRects;

    // Layout constants
    static constexpr int SWATCH_SIZE = 16;
    static constexpr int SWATCH_SPACING = 2;
    static constexpr int COLORS_PER_ROW = 8;
    static constexpr int SECTION_PADDING = 6;
    static constexpr int ROW_SPACING = 2;
};

#endif // COLORSECTION_H
