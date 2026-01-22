#include "ui/sections/WidthSection.h"
#include "ToolbarStyle.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <QtGlobal>
#include <QDebug>

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

    // Draw container background inside the glass border for clean alignment
    const int borderInset = 1;
    const int leftMargin = 4;   // Extra margin from toolbar left edge
    const int rightMargin = 4;  // Extra margin on right side
    int size = m_sectionRect.height() - 2 * borderInset - rightMargin;  // Square size
    int x = m_sectionRect.left() + borderInset + leftMargin;
    int y = m_sectionRect.top() + (m_sectionRect.height() - size) / 2;  // Vertically centered
    QRect containerRect(x, y, size, size);

    // Draw container with all rounded corners
    const int radius = 6 - borderInset;
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#007AFF"));
    painter.drawRoundedRect(containerRect, radius, radius);

    // Draw preview dot (shows actual stroke width with current color)
    // Scale the visual size: map m_currentWidth to visual dot size
    double ratio = static_cast<double>(m_currentWidth - m_minWidth) / (m_maxWidth - m_minWidth);
    int minDotSize = 4;
    int maxDotSize = MAX_DOT_SIZE;
    int dotSize = minDotSize + static_cast<int>(ratio * (maxDotSize - minDotSize));

    // Use QPointF for precise centering
    QPointF center = QRectF(containerRect).center();
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawEllipse(center, dotSize / 2.0, dotSize / 2.0);
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
