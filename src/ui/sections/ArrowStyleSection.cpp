#include "ui/sections/ArrowStyleSection.h"
#include "ToolbarStyle.h"
#include "IconRenderer.h"

#include <QPainter>
#include <QPainterPath>
#include <map>

namespace {
// LineEndStyle to icon key mapping
const std::map<LineEndStyle, QString> kArrowStyleIcons = {
    {LineEndStyle::None, "arrow-none"},
    {LineEndStyle::EndArrow, "arrow-end"},
    {LineEndStyle::EndArrowOutline, "arrow-end-outline"},
    {LineEndStyle::EndArrowLine, "arrow-end-line"},
    {LineEndStyle::BothArrow, "arrow-both"},
    {LineEndStyle::BothArrowOutline, "arrow-both-outline"},
};
} // namespace

ArrowStyleSection::ArrowStyleSection(QObject* parent)
    : QObject(parent)
{
}

void ArrowStyleSection::setArrowStyle(LineEndStyle style)
{
    if (m_arrowStyle != style) {
        m_arrowStyle = style;
        emit arrowStyleChanged(m_arrowStyle);
    }
}

void ArrowStyleSection::setPolylineMode(bool enabled)
{
    if (m_polylineMode != enabled) {
        m_polylineMode = enabled;
        emit polylineModeChanged(m_polylineMode);
    }
}

int ArrowStyleSection::preferredWidth() const
{
    return BUTTON_WIDTH + TOGGLE_SPACING + TOGGLE_WIDTH;
}

void ArrowStyleSection::updateLayout(int containerTop, int containerHeight, int xOffset)
{
    int totalWidth = BUTTON_WIDTH + TOGGLE_SPACING + TOGGLE_WIDTH;
    m_sectionRect = QRect(xOffset, containerTop, totalWidth, containerHeight);

    // Center the buttons vertically
    int buttonY = containerTop + (containerHeight - BUTTON_HEIGHT) / 2;

    // Polyline toggle button (to the left of arrow style dropdown)
    m_polylineToggleRect = QRect(
        xOffset,
        buttonY,
        TOGGLE_WIDTH,
        BUTTON_HEIGHT
    );

    m_buttonRect = QRect(
        xOffset + TOGGLE_WIDTH + TOGGLE_SPACING,
        buttonY,
        BUTTON_WIDTH,
        BUTTON_HEIGHT
    );

    // Dropdown rect - expand upward or downward based on setting
    int dropdownWidth = BUTTON_WIDTH + 10;
    int dropdownHeight = 6 * DROPDOWN_OPTION_HEIGHT;  // 6 options (None + 5 arrow styles)

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

void ArrowStyleSection::draw(QPainter& painter, const ToolbarStyleConfig& styleConfig)
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
    drawArrowStyleIcon(painter, m_arrowStyle, iconRect, styleConfig);

    // Draw dropdown arrow
    int arrowX = m_buttonRect.right() - 8;
    int arrowY = m_buttonRect.center().y();
    QPainterPath arrow;
    arrow.moveTo(arrowX - 3, arrowY - 2);
    arrow.lineTo(arrowX + 3, arrowY - 2);
    arrow.lineTo(arrowX, arrowY + 2);
    arrow.closeSubpath();
    painter.fillPath(arrow, styleConfig.textColor);

    // Draw polyline toggle button
    bool toggleHovered = (m_hoveredOption == -3);
    QColor toggleBgColor;
    if (m_polylineMode) {
        toggleBgColor = styleConfig.buttonActiveColor;
    }
    else if (toggleHovered) {
        toggleBgColor = styleConfig.buttonHoverColor;
    }
    else {
        toggleBgColor = styleConfig.buttonInactiveColor;
    }
    painter.setPen(QPen(styleConfig.dropdownBorder, 1));
    painter.setBrush(toggleBgColor);
    painter.drawRoundedRect(m_polylineToggleRect, 4, 4);

    // Draw polyline icon
    drawPolylineIcon(painter, m_polylineToggleRect, m_polylineMode, styleConfig);

    // Draw dropdown menu if open
    if (m_dropdownOpen) {
        // Draw dropdown background
        painter.setPen(QPen(styleConfig.dropdownBorder, 1));
        painter.setBrush(styleConfig.dropdownBackground);
        painter.drawRoundedRect(m_dropdownRect, 4, 4);

        // Draw each option (6 options: None + 5 arrow styles)
        LineEndStyle styles[] = {
            LineEndStyle::None,
            LineEndStyle::EndArrow,
            LineEndStyle::EndArrowOutline,
            LineEndStyle::EndArrowLine,
            LineEndStyle::BothArrow,
            LineEndStyle::BothArrowOutline
        };
        for (int i = 0; i < 6; ++i) {
            QRect optionRect(
                m_dropdownRect.left(),
                m_dropdownRect.top() + i * DROPDOWN_OPTION_HEIGHT,
                m_dropdownRect.width(),
                DROPDOWN_OPTION_HEIGHT
            );

            bool isHovered = (m_hoveredOption == i);
            bool isSelected = (m_arrowStyle == styles[i]);

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
            drawArrowStyleIcon(painter, styles[i], optionIconRect, styleConfig);
        }
    }
}

void ArrowStyleSection::drawArrowStyleIcon(QPainter& painter, LineEndStyle style, const QRect& rect,
    const ToolbarStyleConfig& styleConfig) const
{
    auto it = kArrowStyleIcons.find(style);
    if (it != kArrowStyleIcons.end()) {
        IconRenderer::instance().renderIconFit(painter, rect, it->second, styleConfig.textColor);
    }
}

void ArrowStyleSection::drawPolylineIcon(QPainter& painter, const QRect& rect, bool active,
    const ToolbarStyleConfig& styleConfig) const
{
    QColor iconColor = active ? styleConfig.textActiveColor : styleConfig.textColor;
    IconRenderer::instance().renderIcon(painter, rect, "polyline", iconColor, 6);
}

bool ArrowStyleSection::contains(const QPoint& pos) const
{
    if (m_sectionRect.contains(pos)) return true;
    if (m_polylineToggleRect.contains(pos)) return true;
    if (m_dropdownOpen && m_dropdownRect.contains(pos)) return true;
    return false;
}

int ArrowStyleSection::optionAtPosition(const QPoint& pos) const
{
    // Check if on polyline toggle
    if (m_polylineToggleRect.contains(pos)) {
        return -3;  // Special value for polyline toggle
    }

    // Check if in dropdown
    if (m_dropdownOpen && m_dropdownRect.contains(pos)) {
        int relativeY = pos.y() - m_dropdownRect.top();
        int option = relativeY / DROPDOWN_OPTION_HEIGHT;
        if (option >= 0 && option < 6) {
            return option;
        }
    }

    // Check if on button
    if (m_buttonRect.contains(pos)) {
        return -2;  // Special value for button
    }

    return -1;
}

bool ArrowStyleSection::handleClick(const QPoint& pos)
{
    int option = optionAtPosition(pos);

    if (option == -3) {
        // Clicked on polyline toggle
        m_polylineMode = !m_polylineMode;
        emit polylineModeChanged(m_polylineMode);
        return true;
    }
    else if (option >= 0) {
        // Clicked on a dropdown option (6 options: None + 5 arrow styles)
        LineEndStyle styles[] = {
            LineEndStyle::None,
            LineEndStyle::EndArrow,
            LineEndStyle::EndArrowOutline,
            LineEndStyle::EndArrowLine,
            LineEndStyle::BothArrow,
            LineEndStyle::BothArrowOutline
        };
        m_arrowStyle = styles[option];
        m_dropdownOpen = false;
        m_hoveredOption = -1;
        emit arrowStyleChanged(m_arrowStyle);
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

bool ArrowStyleSection::updateHovered(const QPoint& pos)
{
    int newHovered = optionAtPosition(pos);
    if (newHovered != m_hoveredOption) {
        m_hoveredOption = newHovered;
        return true;
    }
    return false;
}

void ArrowStyleSection::resetHoverState()
{
    m_hoveredOption = -1;
}
