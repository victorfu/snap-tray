#include "ui/sections/ArrowStyleSection.h"
#include "GlassRenderer.h"
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

constexpr int kDropdownGap = 4;
constexpr int kDropdownRadius = 6;
constexpr int kOptionRadius = 5;
constexpr int kOptionInsetX = 4;
constexpr int kOptionInsetY = 2;

QColor popupHoverColor(const ToolbarStyleConfig& styleConfig)
{
    QColor color = styleConfig.buttonHoverColor;
    color.setAlpha(56);
    return color;
}

QColor popupSelectedColor(const ToolbarStyleConfig& styleConfig)
{
    QColor color = styleConfig.buttonActiveColor;
    color.setAlpha(86);
    return color;
}

QColor popupSelectedBorderColor(const ToolbarStyleConfig& styleConfig)
{
    QColor color = styleConfig.buttonActiveColor;
    color.setAlpha(132);
    return color;
}
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

int ArrowStyleSection::preferredWidth() const
{
    return BUTTON_WIDTH;
}

void ArrowStyleSection::updateLayout(int containerTop, int containerHeight, int xOffset)
{
    m_sectionRect = QRect(xOffset, containerTop, BUTTON_WIDTH, containerHeight);

    // Center the button vertically
    int buttonY = containerTop + (containerHeight - BUTTON_HEIGHT) / 2;

    m_buttonRect = QRect(
        xOffset,
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
            m_buttonRect.top() - dropdownHeight - kDropdownGap,
            dropdownWidth,
            dropdownHeight
        );
    }
    else {
        m_dropdownRect = QRect(
            m_buttonRect.left(),
            m_buttonRect.bottom() + kDropdownGap,
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
    painter.setPen(QPen(m_dropdownOpen ? styleConfig.buttonActiveColor : styleConfig.dropdownBorder, 1));
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

    // Draw dropdown menu if open
    if (m_dropdownOpen) {
        GlassRenderer::drawGlassPanel(painter, m_dropdownRect, styleConfig, kDropdownRadius);

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
            const QRect optionRect(
                m_dropdownRect.left(),
                m_dropdownRect.top() + i * DROPDOWN_OPTION_HEIGHT,
                m_dropdownRect.width(),
                DROPDOWN_OPTION_HEIGHT
            );

            bool isHovered = (m_hoveredOption == i);
            bool isSelected = (m_arrowStyle == styles[i]);

            // Highlight hovered or selected option
            if (isHovered || isSelected) {
                const QRect highlightRect = optionRect.adjusted(
                    kOptionInsetX,
                    kOptionInsetY,
                    -kOptionInsetX,
                    -kOptionInsetY);
                painter.setPen(isSelected ? QPen(popupSelectedBorderColor(styleConfig), 1) : Qt::NoPen);
                painter.setBrush(isSelected ? popupSelectedColor(styleConfig) : popupHoverColor(styleConfig));
                painter.drawRoundedRect(highlightRect, kOptionRadius, kOptionRadius);
            }

            // Draw the style icon
            const QRect optionIconRect = optionRect.adjusted(8, 4, -8, -4);
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

bool ArrowStyleSection::contains(const QPoint& pos) const
{
    if (m_sectionRect.contains(pos)) return true;
    if (m_dropdownOpen && m_dropdownRect.contains(pos)) return true;
    return false;
}

int ArrowStyleSection::optionAtPosition(const QPoint& pos) const
{
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

    if (option >= 0) {
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
