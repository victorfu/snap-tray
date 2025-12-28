#include "ui/sections/MosaicWidthSection.h"
#include "ToolbarStyle.h"

#include <QPainter>
#include <QtMath>

MosaicWidthSection::MosaicWidthSection(QObject* parent)
    : QObject(parent)
{
}

void MosaicWidthSection::setWidthRange(int min, int max)
{
    m_minWidth = min;
    m_maxWidth = max;
    if (m_currentWidth < m_minWidth) m_currentWidth = m_minWidth;
    if (m_currentWidth > m_maxWidth) m_currentWidth = m_maxWidth;
}

void MosaicWidthSection::setCurrentWidth(int width)
{
    int newWidth = qBound(m_minWidth, width, m_maxWidth);
    if (newWidth != m_currentWidth) {
        m_currentWidth = newWidth;
        emit widthChanged(m_currentWidth);
    }
}

int MosaicWidthSection::preferredWidth() const
{
    return SECTION_WIDTH;
}

void MosaicWidthSection::updateLayout(int containerTop, int containerHeight, int xOffset)
{
    m_sectionRect = QRect(xOffset, containerTop, SECTION_WIDTH, containerHeight);

    // Calculate track rect (centered vertically)
    int trackY = containerTop + (containerHeight - TRACK_HEIGHT) / 2;
    int trackLeft = xOffset + SECTION_PADDING;
    int trackWidth = SECTION_WIDTH - 2 * SECTION_PADDING;
    m_trackRect = QRect(trackLeft, trackY, trackWidth, TRACK_HEIGHT);

    // Calculate thumb rect based on current width
    int thumbX = thumbPositionFromWidth();
    int thumbY = containerTop + (containerHeight - THUMB_SIZE) / 2;
    m_thumbRect = QRect(thumbX - THUMB_SIZE / 2, thumbY, THUMB_SIZE, THUMB_SIZE);
}

int MosaicWidthSection::thumbPositionFromWidth() const
{
    double ratio = static_cast<double>(m_currentWidth - m_minWidth) / (m_maxWidth - m_minWidth);
    int trackUsable = m_trackRect.width() - THUMB_SIZE;
    return m_trackRect.left() + THUMB_SIZE / 2 + static_cast<int>(ratio * trackUsable);
}

int MosaicWidthSection::widthFromPosition(int x) const
{
    int trackUsable = m_trackRect.width() - THUMB_SIZE;
    int relativeX = x - m_trackRect.left() - THUMB_SIZE / 2;
    relativeX = qBound(0, relativeX, trackUsable);
    double ratio = static_cast<double>(relativeX) / trackUsable;
    return m_minWidth + static_cast<int>(ratio * (m_maxWidth - m_minWidth));
}

void MosaicWidthSection::draw(QPainter& painter, const ToolbarStyleConfig& styleConfig)
{
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw track background
    painter.setPen(Qt::NoPen);
    painter.setBrush(styleConfig.buttonInactiveColor);
    painter.drawRoundedRect(m_trackRect, TRACK_HEIGHT / 2, TRACK_HEIGHT / 2);

    // Draw filled portion of track (from left to thumb)
    int thumbCenterX = thumbPositionFromWidth();
    QRect filledRect(m_trackRect.left(), m_trackRect.top(),
                     thumbCenterX - m_trackRect.left(), TRACK_HEIGHT);
    painter.setBrush(styleConfig.buttonActiveColor);
    painter.drawRoundedRect(filledRect, TRACK_HEIGHT / 2, TRACK_HEIGHT / 2);

    // Update thumb rect position
    int thumbY = m_sectionRect.top() + (m_sectionRect.height() - THUMB_SIZE) / 2;
    m_thumbRect = QRect(thumbCenterX - THUMB_SIZE / 2, thumbY, THUMB_SIZE, THUMB_SIZE);

    // Draw thumb
    QColor thumbColor = m_hovered || m_dragging ? styleConfig.buttonHoverColor : styleConfig.textColor;
    painter.setBrush(thumbColor);
    painter.setPen(QPen(styleConfig.separatorColor, 1));
    painter.drawEllipse(m_thumbRect);
}

bool MosaicWidthSection::contains(const QPoint& pos) const
{
    return m_sectionRect.contains(pos);
}

bool MosaicWidthSection::handleClick(const QPoint& pos)
{
    if (!contains(pos)) {
        return false;
    }

    // Calculate new width from click position
    int newWidth = widthFromPosition(pos.x());
    if (newWidth != m_currentWidth) {
        m_currentWidth = newWidth;
        emit widthChanged(m_currentWidth);
    }
    return true;
}

bool MosaicWidthSection::handleWheel(int delta)
{
    // delta > 0 = scroll up = increase width
    // delta < 0 = scroll down = decrease width
    int step = (delta > 0) ? 5 : -5;  // Larger steps for mosaic (range 10-100)
    int newWidth = qBound(m_minWidth, m_currentWidth + step, m_maxWidth);

    if (newWidth != m_currentWidth) {
        m_currentWidth = newWidth;
        emit widthChanged(m_currentWidth);
        return true;
    }
    return false;
}

bool MosaicWidthSection::updateHovered(const QPoint& pos)
{
    bool inSection = contains(pos);
    if (inSection != m_hovered) {
        m_hovered = inSection;
        return true;
    }
    return false;
}

void MosaicWidthSection::resetHoverState()
{
    m_hovered = false;
    m_dragging = false;
}
