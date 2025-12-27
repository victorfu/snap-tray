#include "ui/sections/LineStyleSection.h"
#include "ToolbarStyle.h"

#include <QPainter>
#include <QPainterPath>

LineStyleSection::LineStyleSection(QObject* parent)
    : QObject(parent)
{
}

void LineStyleSection::setLineStyle(LineStyle style)
{
    if (m_lineStyle != style) {
        m_lineStyle = style;
        emit lineStyleChanged(m_lineStyle);
    }
}

int LineStyleSection::preferredWidth() const
{
    return BUTTON_WIDTH + 6;  // Add right margin
}

void LineStyleSection::updateLayout(int containerTop, int containerHeight, int xOffset)
{
    m_sectionRect = QRect(xOffset, containerTop, BUTTON_WIDTH, containerHeight);

    // Center the button vertically
    int buttonY = containerTop + (containerHeight - BUTTON_HEIGHT) / 2;
    m_buttonRect = QRect(xOffset, buttonY, BUTTON_WIDTH, BUTTON_HEIGHT);

    // Dropdown rect - expand upward or downward based on setting
    int dropdownWidth = BUTTON_WIDTH + 10;
    int dropdownHeight = NUM_OPTIONS * DROPDOWN_OPTION_HEIGHT;

    if (m_dropdownExpandsUpward) {
        m_dropdownRect = QRect(
            m_buttonRect.left(),
            m_buttonRect.top() - dropdownHeight - 2,
            dropdownWidth,
            dropdownHeight
        );
    }
    else {
        m_dropdownRect = QRect(
            m_buttonRect.left(),
            m_buttonRect.bottom() + 2,
            dropdownWidth,
            dropdownHeight
        );
    }
}

void LineStyleSection::draw(QPainter& painter, const ToolbarStyleConfig& styleConfig)
{
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw the button showing current style
    bool buttonHovered = (m_hoveredOption == -2);
    QColor bgColor = buttonHovered ? styleConfig.buttonHoverColor : styleConfig.buttonInactiveColor;
    painter.setPen(QPen(styleConfig.dropdownBorder, 1));
    painter.setBrush(bgColor);
    painter.drawRoundedRect(m_buttonRect, 4, 4);

    // Draw current style icon in the button
    QRect iconRect = m_buttonRect.adjusted(4, 4, -12, -4);
    drawLineStyleIcon(painter, m_lineStyle, iconRect, styleConfig);

    // Draw dropdown arrow
    int arrowX = m_buttonRect.right() - 8;
    int arrowY = m_buttonRect.center().y();
    QPainterPath arrow;
    arrow.moveTo(arrowX - 3, arrowY - 2);
    arrow.lineTo(arrowX + 3, arrowY - 2);
    arrow.lineTo(arrowX, arrowY + 2);
    arrow.closeSubpath();
    painter.fillPath(arrow, styleConfig.textColor);

    // Draw dropdown menu if open
    if (m_dropdownOpen) {
        // Draw dropdown background
        painter.setPen(QPen(styleConfig.dropdownBorder, 1));
        painter.setBrush(styleConfig.dropdownBackground);
        painter.drawRoundedRect(m_dropdownRect, 4, 4);

        // Draw each option
        LineStyle styles[] = { LineStyle::Solid, LineStyle::Dashed, LineStyle::Dotted };
        for (int i = 0; i < NUM_OPTIONS; ++i) {
            QRect optionRect(
                m_dropdownRect.left(),
                m_dropdownRect.top() + i * DROPDOWN_OPTION_HEIGHT,
                m_dropdownRect.width(),
                DROPDOWN_OPTION_HEIGHT
            );

            bool isHovered = (m_hoveredOption == i);
            bool isSelected = (m_lineStyle == styles[i]);

            // Highlight hovered or selected option
            if (isHovered || isSelected) {
                QColor highlightColor = isSelected
                    ? QColor(styleConfig.buttonActiveColor.red(),
                        styleConfig.buttonActiveColor.green(),
                        styleConfig.buttonActiveColor.blue(), 100)
                    : styleConfig.buttonHoverColor;
                painter.setPen(Qt::NoPen);
                painter.setBrush(highlightColor);
                painter.drawRoundedRect(optionRect.adjusted(2, 2, -2, -2), 3, 3);
            }

            // Draw the style icon
            QRect optionIconRect = optionRect.adjusted(8, 4, -8, -4);
            drawLineStyleIcon(painter, styles[i], optionIconRect, styleConfig);
        }
    }
}

void LineStyleSection::drawLineStyleIcon(QPainter& painter, LineStyle style, const QRect& rect,
    const ToolbarStyleConfig& styleConfig) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Line parameters
    int lineY = rect.center().y();
    int lineLeft = rect.left() + 2;
    int lineRight = rect.right() - 2;
    int lineWidth = 2;

    // Map LineStyle to Qt::PenStyle
    Qt::PenStyle qtStyle = Qt::SolidLine;
    switch (style) {
    case LineStyle::Solid:
        qtStyle = Qt::SolidLine;
        break;
    case LineStyle::Dashed:
        qtStyle = Qt::DashLine;
        break;
    case LineStyle::Dotted:
        qtStyle = Qt::DotLine;
        break;
    }

    QPen linePen(styleConfig.textColor, lineWidth, qtStyle, Qt::RoundCap);
    painter.setPen(linePen);

    // Draw the line
    painter.drawLine(lineLeft, lineY, lineRight, lineY);

    painter.restore();
}

bool LineStyleSection::contains(const QPoint& pos) const
{
    if (m_sectionRect.contains(pos)) return true;
    if (m_dropdownOpen && m_dropdownRect.contains(pos)) return true;
    return false;
}

int LineStyleSection::optionAtPosition(const QPoint& pos) const
{
    // Check if in dropdown
    if (m_dropdownOpen && m_dropdownRect.contains(pos)) {
        int relativeY = pos.y() - m_dropdownRect.top();
        int option = relativeY / DROPDOWN_OPTION_HEIGHT;
        if (option >= 0 && option < NUM_OPTIONS) {
            return option;
        }
    }

    // Check if on button
    if (m_buttonRect.contains(pos)) {
        return -2;  // Special value for button
    }

    return -1;
}

bool LineStyleSection::handleClick(const QPoint& pos)
{
    int option = optionAtPosition(pos);

    if (option >= 0) {
        // Clicked on a dropdown option
        LineStyle styles[] = { LineStyle::Solid, LineStyle::Dashed, LineStyle::Dotted };
        m_lineStyle = styles[option];
        m_dropdownOpen = false;
        m_hoveredOption = -1;
        emit lineStyleChanged(m_lineStyle);
        return true;
    }
    else if (option == -2) {
        // Clicked on the button - toggle dropdown
        m_dropdownOpen = !m_dropdownOpen;
        return true;
    }
    else if (m_dropdownOpen) {
        // Clicked outside dropdown while open - close it
        m_dropdownOpen = false;
        m_hoveredOption = -1;
        // Return false to let other handlers process the click
        return false;
    }

    return false;
}

bool LineStyleSection::updateHovered(const QPoint& pos)
{
    int newHovered = optionAtPosition(pos);
    if (newHovered != m_hoveredOption) {
        m_hoveredOption = newHovered;
        return true;
    }
    return false;
}

void LineStyleSection::resetHoverState()
{
    m_hoveredOption = -1;
}
