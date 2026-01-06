#include "ColorPaletteWidget.h"
#include "GlassRenderer.h"

#include <QPainter>
#include <QColorDialog>
#include <QDebug>
#include <QtGlobal>

ColorPaletteWidget::ColorPaletteWidget(QObject* parent)
    : QObject(parent)
    , m_currentColor(Qt::red)
    , m_visible(false)
    , m_showMoreButton(false)
    , m_hoveredSwatch(-1)
    , m_styleConfig(ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle()))
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
    int swatchCount = m_colors.size() + (m_showMoreButton ? 1 : 0);
    int rows = (swatchCount + COLORS_PER_ROW - 1) / COLORS_PER_ROW;
    rows = qMax(MIN_ROWS, rows);

    int gridWidth = COLORS_PER_ROW * SWATCH_SIZE + (COLORS_PER_ROW - 1) * SWATCH_SPACING;
    int gridHeight = rows * SWATCH_SIZE + (rows - 1) * ROW_SPACING;
    int paletteWidth = PALETTE_PADDING + gridWidth + PALETTE_PADDING;
    int paletteHeight = gridHeight + PALETTE_PADDING * 2;

    int paletteX = anchorRect.left();
    int paletteY;

    if (above) {
        paletteY = anchorRect.top() - paletteHeight - 4;
    } else {
        paletteY = anchorRect.bottom() + 4;
    }

    // Keep on screen (basic bounds check)
    if (paletteX < 5) paletteX = 5;
    if (paletteY < 5 && above) {
        // Not enough space above, put below
        paletteY = anchorRect.bottom() + 4;
    }

    m_paletteRect = QRect(paletteX, paletteY, paletteWidth, paletteHeight);
    updateSwatchRects();
}

void ColorPaletteWidget::updateSwatchRects()
{
    int swatchCount = m_colors.size() + (m_showMoreButton ? 1 : 0);
    m_swatchRects.resize(swatchCount);

    int rows = (swatchCount + COLORS_PER_ROW - 1) / COLORS_PER_ROW;
    rows = qMax(MIN_ROWS, rows);
    int gridHeight = rows * SWATCH_SIZE + (rows - 1) * ROW_SPACING;

    int baseX = m_paletteRect.left() + PALETTE_PADDING;
    int topY = m_paletteRect.top() + (m_paletteRect.height() - gridHeight) / 2;

    // Calculate swatch positions in rows of 8
    for (int i = 0; i < m_colors.size(); ++i) {
        int row = i / COLORS_PER_ROW;
        int col = i % COLORS_PER_ROW;
        int x = baseX + col * (SWATCH_SIZE + SWATCH_SPACING);
        int y = topY + row * (SWATCH_SIZE + ROW_SPACING);
        m_swatchRects[i] = QRect(x, y, SWATCH_SIZE, SWATCH_SIZE);
    }

    if (m_showMoreButton) {
        int moreIdx = m_colors.size();
        int row = moreIdx / COLORS_PER_ROW;
        int col = moreIdx % COLORS_PER_ROW;
        int x = baseX + col * (SWATCH_SIZE + SWATCH_SPACING);
        int y = topY + row * (SWATCH_SIZE + ROW_SPACING);
        m_moreButtonRect = QRect(x, y, SWATCH_SIZE, SWATCH_SIZE);
        m_swatchRects[moreIdx] = m_moreButtonRect;
    } else {
        m_moreButtonRect = QRect();
    }
}

void ColorPaletteWidget::draw(QPainter& painter)
{
    if (!m_visible) return;

    // Draw glass panel (6px radius for sub-widgets)
    GlassRenderer::drawGlassPanel(painter, m_paletteRect, m_styleConfig, 6);

    // Draw color swatches
    for (int i = 0; i < m_colors.size(); ++i) {
        QRect swatchRect = m_swatchRects[i];

        // Highlight if hovered
        if (i == m_hoveredSwatch) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_styleConfig.hoverBackgroundColor);
            painter.drawRoundedRect(swatchRect, 4, 4);
        }

        // Draw color rounded square
        const int kSwatchInset = 2;
        QRect colorRect = swatchRect.adjusted(kSwatchInset, kSwatchInset,
                                              -kSwatchInset, -kSwatchInset);

        // Draw border (white for dark colors, dark for light colors)
        QColor borderColor = (m_colors[i].lightness() > 200) ? m_styleConfig.separatorColor : Qt::white;
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
            painter.setBrush(m_styleConfig.hoverBackgroundColor);
            painter.drawRoundedRect(moreRect, 4, 4);
        }

        // Draw "..." text
        painter.setPen(m_styleConfig.textColor);
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
