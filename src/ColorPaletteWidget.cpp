#include "ColorPaletteWidget.h"

#include <QPainter>
#include <QColorDialog>
#include <QDebug>

ColorPaletteWidget::ColorPaletteWidget(QObject* parent)
    : QObject(parent)
    , m_currentColor(Qt::red)
    , m_visible(false)
    , m_showMoreButton(true)
    , m_hoveredSwatch(-1)
{
    // Default color palette - 15 colors in 2 rows (8 + 7, with "..." as 8th in row 2)
    m_colors = {
        // Row 1: 8 Primary/Bright colors
        Qt::red,                          // Red
        QColor(255, 149, 0),              // Orange
        QColor(255, 204, 0),              // Yellow
        QColor(52, 199, 89),              // Green
        QColor(0, 199, 190),              // Cyan/Teal
        QColor(0, 122, 255),              // Blue
        QColor(175, 82, 222),             // Purple
        QColor(255, 45, 85),              // Pink

        // Row 2: 7 Pastel colors + neutrals (then "..." button)
        QColor(255, 182, 193),            // Pastel Pink
        QColor(255, 218, 185),            // Pastel Peach
        QColor(255, 250, 205),            // Pastel Yellow
        QColor(152, 251, 152),            // Pastel Green
        QColor(173, 216, 230),            // Pastel Blue
        QColor(128, 128, 128),            // Gray
        Qt::black                         // Black
    };
}

void ColorPaletteWidget::setColors(const QVector<QColor>& colors)
{
    m_colors = colors;
    updateSwatchRects();
}

void ColorPaletteWidget::setCurrentColor(const QColor& color)
{
    m_currentColor = color;
}

void ColorPaletteWidget::updatePosition(const QRect& anchorRect, bool above)
{
    // Calculate width based on 2-row layout (8 columns, "..." is part of row 2)
    int paletteWidth = COLORS_PER_ROW * SWATCH_SIZE + (COLORS_PER_ROW - 1) * SWATCH_SPACING + PALETTE_PADDING * 2;

    int paletteX = anchorRect.left();
    int paletteY;

    if (above) {
        paletteY = anchorRect.top() - PALETTE_HEIGHT - 4;
    } else {
        paletteY = anchorRect.bottom() + 4;
    }

    // Keep on screen (basic bounds check)
    if (paletteX < 5) paletteX = 5;
    if (paletteY < 5 && above) {
        // Not enough space above, put below
        paletteY = anchorRect.bottom() + 4;
    }

    m_paletteRect = QRect(paletteX, paletteY, paletteWidth, PALETTE_HEIGHT);
    updateSwatchRects();
}

void ColorPaletteWidget::updateSwatchRects()
{
    int swatchCount = m_colors.size() + (m_showMoreButton ? 1 : 0);
    m_swatchRects.resize(swatchCount);

    int baseX = m_paletteRect.left() + PALETTE_PADDING;
    int rowSpacing = 2;  // Space between rows
    int topY = m_paletteRect.top() + 2;

    // Calculate swatch positions in 2 rows (8 per row, "..." naturally at position 15)
    for (int i = 0; i < swatchCount; ++i) {
        int row = i / COLORS_PER_ROW;
        int col = i % COLORS_PER_ROW;
        int x = baseX + col * (SWATCH_SIZE + SWATCH_SPACING);
        int y = topY + row * (SWATCH_SIZE + rowSpacing);
        m_swatchRects[i] = QRect(x, y, SWATCH_SIZE, SWATCH_SIZE);
    }
}

void ColorPaletteWidget::draw(QPainter& painter)
{
    if (!m_visible) return;

    // Draw shadow
    QRect shadowRect = m_paletteRect.adjusted(2, 2, 2, 2);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 50));
    painter.drawRoundedRect(shadowRect, 6, 6);

    // Draw background with gradient
    QLinearGradient gradient(m_paletteRect.topLeft(), m_paletteRect.bottomLeft());
    gradient.setColorAt(0, QColor(55, 55, 55, 245));
    gradient.setColorAt(1, QColor(40, 40, 40, 245));

    painter.setBrush(gradient);
    painter.setPen(QPen(QColor(70, 70, 70), 1));
    painter.drawRoundedRect(m_paletteRect, 6, 6);

    // Draw color swatches
    for (int i = 0; i < m_colors.size(); ++i) {
        QRect swatchRect = m_swatchRects[i];

        // Highlight if hovered
        if (i == m_hoveredSwatch) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(80, 80, 80));
            painter.drawRoundedRect(swatchRect.adjusted(-2, -2, 2, 2), 4, 4);
        }

        // Draw color rounded square
        int colorSize = swatchRect.width() - 6;
        QRect colorRect(swatchRect.center().x() - colorSize / 2,
                        swatchRect.center().y() - colorSize / 2,
                        colorSize, colorSize);

        // Draw border (white for dark colors, dark for light colors)
        QColor borderColor = (m_colors[i].lightness() > 200) ? QColor(80, 80, 80) : Qt::white;
        painter.setPen(QPen(borderColor, 1));
        painter.setBrush(m_colors[i]);
        painter.drawRoundedRect(colorRect, 3, 3);

        // Draw selection indicator for current color
        if (m_colors[i] == m_currentColor) {
            painter.setPen(QPen(QColor(0, 174, 255), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(colorRect.adjusted(-3, -3, 3, 3), 4, 4);
        }
    }

    // Draw "more colors" button (last swatch position)
    if (m_showMoreButton) {
        int moreIdx = m_colors.size();
        QRect moreRect = m_swatchRects[moreIdx];

        if (moreIdx == m_hoveredSwatch) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(80, 80, 80));
            painter.drawRoundedRect(moreRect.adjusted(-2, -2, 2, 2), 4, 4);
        }

        // Draw "..." text
        painter.setPen(QColor(180, 180, 180));
        QFont font = painter.font();
        font.setPointSize(12);
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(moreRect, Qt::AlignCenter, "...");
    }
}

int ColorPaletteWidget::swatchAtPosition(const QPoint& pos) const
{
    if (!m_paletteRect.contains(pos)) return -1;

    for (int i = 0; i < m_swatchRects.size(); ++i) {
        if (m_swatchRects[i].contains(pos)) {
            return i;
        }
    }
    return -1;
}

bool ColorPaletteWidget::updateHoveredSwatch(const QPoint& pos)
{
    int newHovered = swatchAtPosition(pos);
    if (newHovered != m_hoveredSwatch) {
        m_hoveredSwatch = newHovered;
        return true;
    }
    return false;
}

bool ColorPaletteWidget::handleClick(const QPoint& pos)
{
    int swatchIdx = swatchAtPosition(pos);
    if (swatchIdx < 0) return false;

    if (swatchIdx < m_colors.size()) {
        // Color swatch clicked
        m_currentColor = m_colors[swatchIdx];
        emit colorSelected(m_currentColor);
        qDebug() << "ColorPaletteWidget: Color selected:" << m_currentColor.name();
    } else if (m_showMoreButton) {
        // "More colors" button clicked
        emit moreColorsRequested();
    }

    return true;
}

bool ColorPaletteWidget::contains(const QPoint& pos) const
{
    return m_paletteRect.contains(pos);
}
