#include "ColorAndWidthWidget.h"

#include <QPainter>
#include <QDebug>

ColorAndWidthWidget::ColorAndWidthWidget(QObject* parent)
    : QObject(parent)
    , m_currentColor(Qt::red)
    , m_showMoreButton(true)
    , m_hoveredSwatch(-1)
    , m_minWidth(1)
    , m_maxWidth(20)
    , m_currentWidth(3)
    , m_widthSectionHovered(false)
    , m_showWidthSection(true)
    , m_visible(false)
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

void ColorAndWidthWidget::setColors(const QVector<QColor>& colors)
{
    m_colors = colors;
}

void ColorAndWidthWidget::setCurrentColor(const QColor& color)
{
    m_currentColor = color;
}

void ColorAndWidthWidget::setWidthRange(int min, int max)
{
    m_minWidth = min;
    m_maxWidth = max;
    if (m_currentWidth < m_minWidth) m_currentWidth = m_minWidth;
    if (m_currentWidth > m_maxWidth) m_currentWidth = m_maxWidth;
}

void ColorAndWidthWidget::setCurrentWidth(int width)
{
    m_currentWidth = qBound(m_minWidth, width, m_maxWidth);
}

void ColorAndWidthWidget::setShowWidthSection(bool show)
{
    m_showWidthSection = show;
}

void ColorAndWidthWidget::updatePosition(const QRect& anchorRect, bool above, int screenWidth)
{
    // Calculate total width (using 2 rows, 8 columns - "..." is part of row 2)
    int colorSectionWidth = COLORS_PER_ROW * SWATCH_SIZE + (COLORS_PER_ROW - 1) * SWATCH_SPACING + SECTION_PADDING * 2;
    int totalWidth = colorSectionWidth;
    if (m_showWidthSection) {
        totalWidth += SECTION_SPACING + WIDTH_SECTION_SIZE;
    }

    int widgetX = anchorRect.left();
    int widgetY;

    if (above) {
        widgetY = anchorRect.top() - WIDGET_HEIGHT - 4;
    } else {
        widgetY = anchorRect.bottom() + 4;
    }

    // Keep on screen - left boundary
    if (widgetX < 5) widgetX = 5;

    // Keep on screen - right boundary
    if (screenWidth > 0) {
        int maxX = screenWidth - totalWidth - 5;
        if (widgetX > maxX) widgetX = maxX;
    }

    // Keep on screen - top boundary (fallback to below)
    if (widgetY < 5 && above) {
        widgetY = anchorRect.bottom() + 4;
    }

    m_widgetRect = QRect(widgetX, widgetY, totalWidth, WIDGET_HEIGHT);
    updateLayout();
}

void ColorAndWidthWidget::updateLayout()
{
    // === COLOR SECTION (2 rows, 8 columns - "..." is part of row 2) ===
    int swatchCount = m_colors.size() + (m_showMoreButton ? 1 : 0);
    int colorSectionWidth = COLORS_PER_ROW * SWATCH_SIZE + (COLORS_PER_ROW - 1) * SWATCH_SPACING + SECTION_PADDING * 2;

    m_colorSectionRect = QRect(
        m_widgetRect.left(),
        m_widgetRect.top(),
        colorSectionWidth,
        WIDGET_HEIGHT
    );

    // Calculate swatch positions in 2 rows (8 per row, "..." naturally at position 15)
    m_swatchRects.resize(swatchCount);
    int baseX = m_colorSectionRect.left() + SECTION_PADDING;
    int rowSpacing = 2;  // Space between rows
    int topY = m_colorSectionRect.top() + 2;

    for (int i = 0; i < swatchCount; ++i) {
        int row = i / COLORS_PER_ROW;
        int col = i % COLORS_PER_ROW;
        int x = baseX + col * (SWATCH_SIZE + SWATCH_SPACING);
        int y = topY + row * (SWATCH_SIZE + rowSpacing);
        m_swatchRects[i] = QRect(x, y, SWATCH_SIZE, SWATCH_SIZE);
    }

    // === WIDTH SECTION (simple dot preview, scroll to adjust) ===
    m_widthSectionRect = QRect(
        m_colorSectionRect.right() + SECTION_SPACING,
        m_widgetRect.top(),
        WIDTH_SECTION_SIZE,
        WIDGET_HEIGHT
    );
}

void ColorAndWidthWidget::draw(QPainter& painter)
{
    if (!m_visible) return;

    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw unified shadow
    QRect shadowRect = m_widgetRect.adjusted(2, 2, 2, 2);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 50));
    painter.drawRoundedRect(shadowRect, 6, 6);

    // Draw unified background with gradient
    QLinearGradient gradient(m_widgetRect.topLeft(), m_widgetRect.bottomLeft());
    gradient.setColorAt(0, QColor(55, 55, 55, 245));
    gradient.setColorAt(1, QColor(40, 40, 40, 245));
    painter.setBrush(gradient);
    painter.setPen(QPen(QColor(70, 70, 70), 1));
    painter.drawRoundedRect(m_widgetRect, 6, 6);

    // Draw color section
    drawColorSection(painter);

    // Draw width section (if enabled)
    if (m_showWidthSection) {
        drawWidthSection(painter);
    }
}

void ColorAndWidthWidget::drawColorSection(QPainter& painter)
{
    // Draw color swatches (adapted from ColorPaletteWidget)
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

void ColorAndWidthWidget::drawWidthSection(QPainter& painter)
{
    // Draw container circle (background)
    int containerSize = WIDTH_SECTION_SIZE - 8;
    int centerX = m_widthSectionRect.center().x();
    int centerY = m_widthSectionRect.center().y();
    QRect containerRect(centerX - containerSize / 2, centerY - containerSize / 2,
                        containerSize, containerSize);

    painter.setPen(QPen(QColor(60, 60, 60), 1));
    painter.setBrush(QColor(35, 35, 35));
    painter.drawEllipse(containerRect);

    // Draw preview dot (shows actual stroke width with current color)
    // Scale the visual size: map m_currentWidth to visual dot size
    double ratio = static_cast<double>(m_currentWidth - m_minWidth) / (m_maxWidth - m_minWidth);
    int minDotSize = 4;
    int maxDotSize = MAX_DOT_SIZE;
    int dotSize = minDotSize + static_cast<int>(ratio * (maxDotSize - minDotSize));

    QRect dotRect(centerX - dotSize / 2, centerY - dotSize / 2, dotSize, dotSize);

    painter.setPen(Qt::NoPen);
    painter.setBrush(m_currentColor);
    painter.drawEllipse(dotRect);
}

bool ColorAndWidthWidget::contains(const QPoint& pos) const
{
    return m_widgetRect.contains(pos);
}

int ColorAndWidthWidget::swatchAtPosition(const QPoint& pos) const
{
    if (!m_colorSectionRect.contains(pos)) return -1;

    for (int i = 0; i < m_swatchRects.size(); ++i) {
        if (m_swatchRects[i].contains(pos)) {
            return i;
        }
    }
    return -1;
}

bool ColorAndWidthWidget::isInWidthSection(const QPoint& pos) const
{
    return m_widthSectionRect.contains(pos);
}

bool ColorAndWidthWidget::handleClick(const QPoint& pos)
{
    if (!m_widgetRect.contains(pos)) return false;

    // Width section: clicks are consumed but no action (use scroll wheel)
    if (isInWidthSection(pos)) {
        return true;
    }

    // Color section
    int swatchIdx = swatchAtPosition(pos);
    if (swatchIdx >= 0) {
        if (swatchIdx < m_colors.size()) {
            m_currentColor = m_colors[swatchIdx];
            emit colorSelected(m_currentColor);
            qDebug() << "ColorAndWidthWidget: Color selected:" << m_currentColor.name();
        } else if (m_showMoreButton) {
            emit moreColorsRequested();
        }
        return true;
    }

    return false;
}

bool ColorAndWidthWidget::handleMousePress(const QPoint& pos)
{
    // Delegate to handleClick for unified handling
    return handleClick(pos);
}

bool ColorAndWidthWidget::handleMouseMove(const QPoint& pos, bool pressed)
{
    Q_UNUSED(pressed);
    if (!m_visible) return false;

    // Update hover states
    return updateHovered(pos);
}

bool ColorAndWidthWidget::handleMouseRelease(const QPoint& pos)
{
    Q_UNUSED(pos);
    return false;
}

bool ColorAndWidthWidget::updateHovered(const QPoint& pos)
{
    bool changed = false;

    // Update color swatch hover
    int newHoveredSwatch = swatchAtPosition(pos);
    if (newHoveredSwatch != m_hoveredSwatch) {
        m_hoveredSwatch = newHoveredSwatch;
        changed = true;
    }

    // Update width section hover
    bool inWidthSection = isInWidthSection(pos);
    if (inWidthSection != m_widthSectionHovered) {
        m_widthSectionHovered = inWidthSection;
        changed = true;
    }

    return changed;
}

bool ColorAndWidthWidget::handleWheel(int delta)
{
    // Don't handle wheel events if width section is hidden
    if (!m_showWidthSection) {
        return false;
    }

    // delta > 0 = scroll up = increase width
    // delta < 0 = scroll down = decrease width
    int step = (delta > 0) ? 1 : -1;
    int newWidth = qBound(m_minWidth, m_currentWidth + step, m_maxWidth);

    if (newWidth != m_currentWidth) {
        m_currentWidth = newWidth;
        emit widthChanged(m_currentWidth);
        qDebug() << "ColorAndWidthWidget: Width changed to" << m_currentWidth << "(scroll)";
        return true;
    }
    return false;
}
