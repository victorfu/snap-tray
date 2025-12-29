#include "ui/sections/WidthSection.h"
#include "ToolbarStyle.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>

WidthSection::WidthSection(QObject* parent)
    : QObject(parent)
{
}

void WidthSection::setWidthRange(int min, int max)
{
    m_minWidth = min;
    m_maxWidth = max;
    if (m_currentWidth < m_minWidth) m_currentWidth = m_minWidth;
    if (m_currentWidth > m_maxWidth) m_currentWidth = m_maxWidth;
}

void WidthSection::setCurrentWidth(int width)
{
    m_currentWidth = qBound(m_minWidth, width, m_maxWidth);
}

int WidthSection::preferredWidth() const
{
    return SECTION_SIZE;
}

void WidthSection::updateLayout(int containerTop, int containerHeight, int xOffset)
{
    m_sectionRect = QRect(xOffset, containerTop, SECTION_SIZE, containerHeight);
}

void WidthSection::draw(QPainter& painter, const ToolbarStyleConfig& styleConfig)
{
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw container square (background) - perfect square using height as size
    int centerX = m_sectionRect.center().x();
    int centerY = m_sectionRect.center().y();
    int squareSize = m_sectionRect.height();  // Use height to ensure perfect square
    QRect containerRect(centerX - squareSize / 2, m_sectionRect.top(),
                        squareSize, squareSize);

    // Draw container with rounded left corners to match toolbar radius
    const int radius = 6;  // Match parent glass panel radius
    QPainterPath path;
    path.moveTo(containerRect.right(), containerRect.top());
    path.lineTo(containerRect.left() + radius, containerRect.top());
    path.arcTo(containerRect.left(), containerRect.top(), radius * 2, radius * 2, 90, 90);
    path.lineTo(containerRect.left(), containerRect.bottom() - radius);
    path.arcTo(containerRect.left(), containerRect.bottom() - radius * 2, radius * 2, radius * 2, 180, 90);
    path.lineTo(containerRect.right(), containerRect.bottom());
    path.closeSubpath();

    painter.setPen(QPen(styleConfig.separatorColor, 1));
    painter.setBrush(styleConfig.buttonInactiveColor);
    painter.drawPath(path);

    // Draw preview dot (shows actual stroke width with current color)
    // Scale the visual size: map m_currentWidth to visual dot size
    double ratio = static_cast<double>(m_currentWidth - m_minWidth) / (m_maxWidth - m_minWidth);
    int minDotSize = 4;
    int maxDotSize = MAX_DOT_SIZE;
    int dotSize = minDotSize + static_cast<int>(ratio * (maxDotSize - minDotSize));

    QRect dotRect(centerX - dotSize / 2, centerY - dotSize / 2, dotSize, dotSize);

    painter.setPen(Qt::NoPen);
    painter.setBrush(m_previewColor);
    painter.drawEllipse(dotRect);
}

bool WidthSection::contains(const QPoint& pos) const
{
    return m_sectionRect.contains(pos);
}

bool WidthSection::handleClick(const QPoint& pos)
{
    // Clicks in width section are consumed but no action (use scroll wheel)
    return contains(pos);
}

bool WidthSection::handleWheel(int delta)
{
    // delta > 0 = scroll up = increase width
    // delta < 0 = scroll down = decrease width
    int step = (delta > 0) ? 1 : -1;
    int newWidth = qBound(m_minWidth, m_currentWidth + step, m_maxWidth);

    if (newWidth != m_currentWidth) {
        m_currentWidth = newWidth;
        emit widthChanged(m_currentWidth);
        return true;
    }
    return false;
}

bool WidthSection::updateHovered(const QPoint& pos)
{
    bool inSection = contains(pos);
    if (inSection != m_hovered) {
        m_hovered = inSection;
        return true;
    }
    return false;
}

void WidthSection::resetHoverState()
{
    m_hovered = false;
}
