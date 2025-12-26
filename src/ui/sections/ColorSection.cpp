#include "ui/sections/ColorSection.h"
#include "ToolbarStyle.h"

#include <QPainter>

ColorSection::ColorSection(QObject* parent)
    : QObject(parent)
    , m_currentColor(Qt::red)
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

void ColorSection::setColors(const QVector<QColor>& colors)
{
    m_colors = colors;
}

void ColorSection::setCurrentColor(const QColor& color)
{
    m_currentColor = color;
}

int ColorSection::preferredWidth() const
{
    return COLORS_PER_ROW * SWATCH_SIZE + (COLORS_PER_ROW - 1) * SWATCH_SPACING + SECTION_PADDING * 2;
}

void ColorSection::updateLayout(int containerTop, int containerHeight, int xOffset)
{
    Q_UNUSED(containerHeight);

    int width = preferredWidth();
    m_sectionRect = QRect(xOffset, containerTop, width, containerHeight);

    // Calculate swatch positions in 2 rows (8 per row, "..." naturally at position 15)
    int swatchCount = m_colors.size() + (m_showMoreButton ? 1 : 0);
    m_swatchRects.resize(swatchCount);

    int baseX = m_sectionRect.left() + SECTION_PADDING;
    int topY = m_sectionRect.top() + 2;

    for (int i = 0; i < swatchCount; ++i) {
        int row = i / COLORS_PER_ROW;
        int col = i % COLORS_PER_ROW;
        int x = baseX + col * (SWATCH_SIZE + SWATCH_SPACING);
        int y = topY + row * (SWATCH_SIZE + ROW_SPACING);
        m_swatchRects[i] = QRect(x, y, SWATCH_SIZE, SWATCH_SIZE);
    }
}

void ColorSection::draw(QPainter& painter, const ToolbarStyleConfig& styleConfig)
{
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw color swatches
    for (int i = 0; i < m_colors.size(); ++i) {
        QRect swatchRect = m_swatchRects[i];

        // Highlight if hovered
        if (i == m_hoveredSwatch) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(styleConfig.hoverBackgroundColor);
            painter.drawRoundedRect(swatchRect.adjusted(-2, -2, 2, 2), 4, 4);
        }

        // Draw color rounded square
        int colorSize = swatchRect.width() - 6;
        QRect colorRect(swatchRect.center().x() - colorSize / 2,
                        swatchRect.center().y() - colorSize / 2,
                        colorSize, colorSize);

        // Draw border (white for dark colors, dark for light colors)
        QColor borderColor = (m_colors[i].lightness() > 200) ? styleConfig.separatorColor : Qt::white;
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
            painter.setBrush(styleConfig.hoverBackgroundColor);
            painter.drawRoundedRect(moreRect.adjusted(-2, -2, 2, 2), 4, 4);
        }

        // Draw "..." text
        painter.setPen(styleConfig.textColor);
        QFont font = painter.font();
        font.setPointSize(12);
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(moreRect, Qt::AlignCenter, "...");
    }
}

bool ColorSection::contains(const QPoint& pos) const
{
    return m_sectionRect.contains(pos);
}

int ColorSection::swatchAtPosition(const QPoint& pos) const
{
    if (!m_sectionRect.contains(pos)) return -1;

    for (int i = 0; i < m_swatchRects.size(); ++i) {
        if (m_swatchRects[i].contains(pos)) {
            return i;
        }
    }
    return -1;
}

bool ColorSection::handleClick(const QPoint& pos)
{
    int swatchIdx = swatchAtPosition(pos);
    if (swatchIdx >= 0) {
        if (swatchIdx < m_colors.size()) {
            m_currentColor = m_colors[swatchIdx];
            emit colorSelected(m_currentColor);
        } else if (m_showMoreButton) {
            emit moreColorsRequested();
        }
        return true;
    }
    return false;
}

bool ColorSection::updateHovered(const QPoint& pos)
{
    int newHovered = swatchAtPosition(pos);
    if (newHovered != m_hoveredSwatch) {
        m_hoveredSwatch = newHovered;
        return true;
    }
    return false;
}

void ColorSection::resetHoverState()
{
    m_hoveredSwatch = -1;
}
