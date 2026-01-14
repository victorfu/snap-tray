#include "ColorPaletteWidget.h"
#include "GlassRenderer.h"

#include <QPainter>
#include <QPainterPath>
#include <QColorDialog>
#include <QDebug>
#include <QtGlobal>

ColorPaletteWidget::ColorPaletteWidget(QObject* parent)
    : QObject(parent)
    , m_currentColor(Qt::red)
    , m_visible(false)
    , m_hoveredSwatch(-2)  // -2 = nothing hovered
    , m_styleConfig(ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle()))
{
    // Standard preset colors (6 colors)
    m_colors = {
        QColor(220, 53, 69),    // Red
        QColor(255, 240, 120),  // Yellow (brighter)
        QColor(80, 200, 120),   // Green (lighter)
        QColor(0, 123, 255),    // Blue
        QColor(33, 37, 41),     // Black (soft)
        Qt::white               // White
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
    // Width = custom swatch + standard colors
    int itemCount = 1 + m_colors.size();  // 1 custom + N standard
    int gridWidth = itemCount * SWATCH_SIZE + (itemCount - 1) * SWATCH_SPACING;
    int paletteWidth = PALETTE_PADDING + gridWidth + PALETTE_PADDING;
    int paletteHeight = SWATCH_SIZE + PALETTE_PADDING * 2;

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
    int gridTop = m_paletteRect.top() + (m_paletteRect.height() - SWATCH_SIZE) / 2;
    int gridLeft = m_paletteRect.left() + PALETTE_PADDING;

    // Custom swatch is first (index -1 conceptually, but stored separately)
    m_customSwatchRect = QRect(gridLeft, gridTop, SWATCH_SIZE, SWATCH_SIZE);

    // Standard color swatches start after custom swatch
    int standardLeft = gridLeft + SWATCH_SIZE + SWATCH_SPACING;
    m_swatchRects.resize(m_colors.size());

    for (int i = 0; i < m_colors.size(); ++i) {
        int x = standardLeft + i * (SWATCH_SIZE + SWATCH_SPACING);
        m_swatchRects[i] = QRect(x, gridTop, SWATCH_SIZE, SWATCH_SIZE);
    }
}

// Helper to draw a mini color wheel icon
static void drawColorWheelIcon(QPainter& painter, const QRect& rect)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    int cx = rect.center().x();
    int cy = rect.center().y();
    int radius = qMin(rect.width(), rect.height()) / 2 - 1;

    // Draw 6 colored pie segments
    const QColor wheelColors[] = {
        QColor(255, 0, 0),      // Red
        QColor(255, 255, 0),    // Yellow
        QColor(0, 255, 0),      // Green
        QColor(0, 255, 255),    // Cyan
        QColor(0, 0, 255),      // Blue
        QColor(255, 0, 255)     // Magenta
    };

    for (int i = 0; i < 6; ++i) {
        QPainterPath path;
        path.moveTo(cx, cy);
        int startAngle = i * 60 * 16;  // Qt uses 1/16th degree
        int spanAngle = 60 * 16;
        path.arcTo(rect, startAngle / 16.0, spanAngle / 16.0);
        path.closeSubpath();

        painter.setPen(Qt::NoPen);
        painter.setBrush(wheelColors[i]);
        painter.drawPath(path);
    }

    // Draw small white center circle
    painter.setBrush(Qt::white);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPoint(cx, cy), radius / 3, radius / 3);

    painter.restore();
}

void ColorPaletteWidget::draw(QPainter& painter)
{
    if (!m_visible) return;

    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw glass panel (6px radius for sub-widgets)
    GlassRenderer::drawGlassPanel(painter, m_paletteRect, m_styleConfig, 6);

    // Border colors for light theme visibility
    const QColor darkBorder(96, 96, 96);     // For light colors (white, yellow)
    const QColor lightBorder(192, 192, 192); // For dark colors

    auto getBorderColor = [&](const QColor& color) {
        return (color.lightness() > 180) ? darkBorder : lightBorder;
    };

    // =========================================================================
    // Draw Color Preview Swatch (first position) - shows current selected color
    // =========================================================================
    {
        QRect swatchRect = m_customSwatchRect;
        bool isHovered = (m_hoveredSwatch == -1);

        // Draw swatch fill with current color
        QColor borderColor = getBorderColor(m_currentColor);

        // Hover effect: darken border
        if (isHovered) {
            borderColor = borderColor.darker(120);
        }

        // Swatch border and fill - shows currently selected color
        painter.setPen(QPen(borderColor, 1));
        painter.setBrush(m_currentColor);
        painter.drawRoundedRect(swatchRect, BORDER_RADIUS, BORDER_RADIUS);

        // Draw color wheel icon in bottom-right corner (indicates color picker)
        int iconSize = 10;
        QRect iconRect(swatchRect.right() - iconSize + 1,
                       swatchRect.bottom() - iconSize + 1,
                       iconSize, iconSize);
        drawColorWheelIcon(painter, iconRect);
    }

    // =========================================================================
    // Draw Standard Color Swatches
    // =========================================================================
    for (int i = 0; i < m_colors.size(); ++i) {
        QRect swatchRect = m_swatchRects[i];
        bool isHovered = (i == m_hoveredSwatch);
        bool isSelected = (m_colors[i] == m_currentColor);

        QColor borderColor = getBorderColor(m_colors[i]);

        // Hover effect: darken border
        if (isHovered) {
            borderColor = borderColor.darker(120);
        }

        // Selection ring (outside the swatch)
        if (isSelected) {
            painter.setPen(QPen(QColor(0, 174, 255), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(swatchRect.adjusted(-2, -2, 2, 2), BORDER_RADIUS + 1, BORDER_RADIUS + 1);
        }

        // Swatch border and fill
        painter.setPen(QPen(borderColor, 1));
        painter.setBrush(m_colors[i]);
        painter.drawRoundedRect(swatchRect, BORDER_RADIUS, BORDER_RADIUS);
    }
}

int ColorPaletteWidget::swatchAtPosition(const QPoint& pos) const
{
    if (!m_paletteRect.contains(pos)) return -2;  // -2 = outside

    // Check custom swatch first
    if (m_customSwatchRect.contains(pos)) {
        return -1;  // -1 = custom swatch
    }

    // Check standard swatches
    for (int i = 0; i < m_swatchRects.size(); ++i) {
        if (m_swatchRects[i].contains(pos)) {
            return i;
        }
    }
    return -2;  // Not on any swatch
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

    if (swatchIdx == -1) {
        // Preview swatch clicked - open color picker
        emit customColorPickerRequested();
        return true;
    } else if (swatchIdx >= 0 && swatchIdx < m_colors.size()) {
        // Standard color clicked
        m_currentColor = m_colors[swatchIdx];
        emit colorSelected(m_currentColor);
        qDebug() << "ColorPaletteWidget: Color selected:" << m_currentColor.name();
        return true;
    }
    return false;
}

bool ColorPaletteWidget::handleDoubleClick(const QPoint& pos)
{
    Q_UNUSED(pos);
    // No special double-click behavior needed
    return false;
}

bool ColorPaletteWidget::contains(const QPoint& pos) const
{
    return m_paletteRect.contains(pos);
}
