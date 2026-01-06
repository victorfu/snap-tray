#include "ui/sections/ColorSection.h"
#include "ToolbarStyle.h"

#include <QPainter>
#include <QtGlobal>

ColorSection::ColorSection(QObject* parent)
    : QObject(parent)
    , m_currentColor(Qt::red)
{
    // Default color palette - 16 colors in 2 rows (8 per row)
    m_colors = {
        // Row 1: Grayscale
        Qt::white,
        QColor(224, 224, 224),
        QColor(192, 192, 192),
        QColor(160, 160, 160),
        QColor(128, 128, 128),
        QColor(96, 96, 96),
        QColor(64, 64, 64),
        Qt::black,

        // Row 2: Primary/Standard
        Qt::red,
        QColor(255, 128, 0),
        Qt::yellow,
        Qt::green,
        Qt::cyan,
        Qt::blue,
        QColor(128, 0, 255),
        Qt::magenta
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
    int gridWidth = COLORS_PER_ROW * SWATCH_SIZE + (COLORS_PER_ROW - 1) * SWATCH_SPACING;
    return SECTION_PADDING + gridWidth + SECTION_PADDING;
}

int ColorSection::preferredHeight() const
{
    int rows = rowCount();
    int gridHeight = rows * SWATCH_SIZE + (rows - 1) * ROW_SPACING;
    return gridHeight + GRID_VERTICAL_PADDING * 2;
}

int ColorSection::rowCount() const
{
    int swatchCount = m_colors.size() + (m_showMoreButton ? 1 : 0);
    int rows = (swatchCount + COLORS_PER_ROW - 1) / COLORS_PER_ROW;
    return qMax(NUM_ROWS, rows);
}

void ColorSection::updateLayout(int containerTop, int containerHeight, int xOffset)
{
    int width = preferredWidth();
    m_sectionRect = QRect(xOffset, containerTop, width, containerHeight);

    int rows = rowCount();
    int gridHeight = rows * SWATCH_SIZE + (rows - 1) * ROW_SPACING;
    int gridTop = m_sectionRect.top() + (m_sectionRect.height() - gridHeight) / 2;
    int gridLeft = m_sectionRect.left() + SECTION_PADDING;

    // Calculate swatch positions in rows of 8
    int swatchCount = m_colors.size() + (m_showMoreButton ? 1 : 0);
    m_swatchRects.resize(swatchCount);

    for (int i = 0; i < m_colors.size(); ++i) {
        int row = i / COLORS_PER_ROW;
        int col = i % COLORS_PER_ROW;
        int x = gridLeft + col * (SWATCH_SIZE + SWATCH_SPACING);
        int y = gridTop + row * (SWATCH_SIZE + ROW_SPACING);
        m_swatchRects[i] = QRect(x, y, SWATCH_SIZE, SWATCH_SIZE);
    }

    if (m_showMoreButton) {
        int moreIdx = m_colors.size();
        int row = moreIdx / COLORS_PER_ROW;
        int col = moreIdx % COLORS_PER_ROW;
        int x = gridLeft + col * (SWATCH_SIZE + SWATCH_SPACING);
        int y = gridTop + row * (SWATCH_SIZE + ROW_SPACING);
        m_moreButtonRect = QRect(x, y, SWATCH_SIZE, SWATCH_SIZE);
        m_swatchRects[moreIdx] = m_moreButtonRect;
    } else {
        m_moreButtonRect = QRect();
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
            painter.drawRoundedRect(swatchRect, 4, 4);
        }

        // Draw color rounded square
        const int kSwatchInset = 2;
        QRect colorRect = swatchRect.adjusted(kSwatchInset, kSwatchInset,
                                              -kSwatchInset, -kSwatchInset);

        // Draw border (white for dark colors, dark for light colors)
        QColor borderColor = (m_colors[i].lightness() > 200) ? styleConfig.separatorColor : Qt::white;
        painter.setPen(QPen(borderColor, 1));
        painter.setBrush(m_colors[i]);
        painter.drawRoundedRect(colorRect, 4, 4);

        // Draw selection indicator for current color
        if (m_colors[i] == m_currentColor) {
            painter.setPen(QPen(QColor(0, 174, 255), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(colorRect.adjusted(-2, -2, 2, 2), 4, 4);
        }
    }

    // Draw "more colors" button (last swatch position)
    if (m_showMoreButton) {
        int moreIdx = m_colors.size();
        QRect moreRect = m_moreButtonRect;

        if (moreIdx == m_hoveredSwatch) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(styleConfig.hoverBackgroundColor);
            painter.drawRoundedRect(moreRect, 4, 4);
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
