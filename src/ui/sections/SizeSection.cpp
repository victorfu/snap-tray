#include "ui/sections/SizeSection.h"
#include "ToolbarStyle.h"

#include <QPainter>
#include <QPen>

SizeSection::SizeSection(QObject* parent)
    : QObject(parent)
{
    m_buttonRects.resize(3);
}

void SizeSection::setSize(StepBadgeSize size)
{
    if (m_size != size) {
        m_size = size;
        emit sizeChanged(m_size);
    }
}

int SizeSection::preferredWidth() const
{
    return BUTTON_SIZE * 3 + BUTTON_SPACING * 2 + SECTION_PADDING;
}

void SizeSection::updateLayout(int containerTop, int containerHeight, int xOffset)
{
    m_sectionRect = QRect(xOffset, containerTop, preferredWidth(), containerHeight);

    // Center buttons vertically
    int buttonY = containerTop + (containerHeight - BUTTON_SIZE) / 2;

    for (int i = 0; i < 3; ++i) {
        m_buttonRects[i] = QRect(
            xOffset + SECTION_PADDING / 2 + i * (BUTTON_SIZE + BUTTON_SPACING),
            buttonY,
            BUTTON_SIZE,
            BUTTON_SIZE
        );
    }
}

void SizeSection::draw(QPainter& painter, const ToolbarStyleConfig& styleConfig)
{
    painter.setRenderHint(QPainter::Antialiasing, true);

    for (int i = 0; i < 3; ++i) {
        if (i >= m_buttonRects.size()) break;

        QRect rect = m_buttonRects[i];

        // Determine selection state - convert index to StepBadgeSize
        StepBadgeSize buttonSize = static_cast<StepBadgeSize>(i);
        bool isSelected = (m_size == buttonSize);
        bool isHovered = (m_hoveredButton == i);

        // Background
        QColor bgColor;
        if (isSelected) {
            bgColor = styleConfig.buttonActiveColor;
        } else if (isHovered) {
            bgColor = styleConfig.buttonHoverColor;
        } else {
            bgColor = styleConfig.buttonInactiveColor;
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(bgColor);
        painter.drawRoundedRect(rect, 4, 4);

        // Draw circle icon - scaled to represent size
        // Small=3, Medium=5, Large=7 radius for icon
        QColor iconColor = isSelected ? styleConfig.textActiveColor : styleConfig.textColor;
        int iconRadius;
        switch (i) {
        case 0: iconRadius = 3; break;  // Small
        case 1: iconRadius = 5; break;  // Medium
        case 2: iconRadius = 7; break;  // Large
        default: iconRadius = 5; break;
        }

        QPoint center = rect.center();
        painter.setPen(Qt::NoPen);
        painter.setBrush(iconColor);
        painter.drawEllipse(center, iconRadius, iconRadius);
    }
}

bool SizeSection::contains(const QPoint& pos) const
{
    return m_sectionRect.contains(pos);
}

int SizeSection::buttonAtPosition(const QPoint& pos) const
{
    for (int i = 0; i < m_buttonRects.size(); ++i) {
        if (m_buttonRects[i].contains(pos)) {
            return i;
        }
    }
    return -1;
}

bool SizeSection::handleClick(const QPoint& pos)
{
    int btn = buttonAtPosition(pos);
    if (btn >= 0 && btn <= 2) {
        StepBadgeSize newSize = static_cast<StepBadgeSize>(btn);
        if (m_size != newSize) {
            m_size = newSize;
            emit sizeChanged(m_size);
        }
        return true;
    }
    return false;
}

bool SizeSection::updateHovered(const QPoint& pos)
{
    int newHovered = buttonAtPosition(pos);
    if (newHovered != m_hoveredButton) {
        m_hoveredButton = newHovered;
        return true;
    }
    return false;
}
