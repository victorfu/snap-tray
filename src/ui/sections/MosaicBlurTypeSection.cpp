#include "ui/sections/MosaicBlurTypeSection.h"
#include "ToolbarStyle.h"

#include <QPainter>
#include <QPainterPath>

MosaicBlurTypeSection::MosaicBlurTypeSection(QObject* parent)
    : QObject(parent)
{
}

int MosaicBlurTypeSection::preferredWidth() const
{
    return BUTTON_WIDTH;
}

void MosaicBlurTypeSection::updateLayout(int containerTop, int containerHeight, int xOffset)
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
    int dropdownWidth = BUTTON_WIDTH;  // Match button width
    int dropdownHeight = 2 * DROPDOWN_OPTION_HEIGHT;  // 2 options (Pixelate, Gaussian)

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

void MosaicBlurTypeSection::draw(QPainter& painter, const ToolbarStyleConfig& styleConfig)
{
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw the button showing current style
    bool buttonHovered = (m_hoveredOption == -2);
    QColor bgColor = buttonHovered ? styleConfig.buttonHoverColor : styleConfig.buttonInactiveColor;
    painter.setPen(QPen(styleConfig.dropdownBorder, 1));
    painter.setBrush(bgColor);
    painter.drawRoundedRect(m_buttonRect, 4, 4);

    // Draw current style text in the button
    const QString currentLabel = (m_blurType == BlurType::Pixelate) ? tr("Pixelate") : tr("Gaussian");
    painter.setPen(styleConfig.textColor);
    QFont font = painter.font();
    font.setPointSize(11);
    painter.setFont(font);
    painter.drawText(m_buttonRect.adjusted(6, 0, -14, 0), Qt::AlignVCenter | Qt::AlignLeft, currentLabel);

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

        // Draw each option with text labels
        BlurType types[] = { BlurType::Pixelate, BlurType::Gaussian };
        const QString labels[] = { tr("Pixelate"), tr("Gaussian") };
        for (int i = 0; i < 2; ++i) {
            QRect optionRect(
                m_dropdownRect.left(),
                m_dropdownRect.top() + i * DROPDOWN_OPTION_HEIGHT,
                m_dropdownRect.width(),
                DROPDOWN_OPTION_HEIGHT
            );

            bool isHovered = (m_hoveredOption == i);
            bool isSelected = (m_blurType == types[i]);

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

            // Draw text label
            painter.setPen(styleConfig.textColor);
            QFont font = painter.font();
            font.setPointSize(11);
            painter.setFont(font);
            painter.drawText(optionRect.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, labels[i]);
        }
    }
}

bool MosaicBlurTypeSection::contains(const QPoint& pos) const
{
    if (m_sectionRect.contains(pos)) return true;
    if (m_dropdownOpen && m_dropdownRect.contains(pos)) return true;
    return false;
}

int MosaicBlurTypeSection::optionAtPosition(const QPoint& pos) const
{
    // Check if in dropdown
    if (m_dropdownOpen && m_dropdownRect.contains(pos)) {
        int relativeY = pos.y() - m_dropdownRect.top();
        int option = relativeY / DROPDOWN_OPTION_HEIGHT;
        if (option >= 0 && option < 2) {
            return option;
        }
    }

    // Check if on button
    if (m_buttonRect.contains(pos)) {
        return -2;  // Special value for button
    }

    return -1;
}

bool MosaicBlurTypeSection::handleClick(const QPoint& pos)
{
    int option = optionAtPosition(pos);

    if (option >= 0) {
        // Clicked on a dropdown option
        BlurType types[] = { BlurType::Pixelate, BlurType::Gaussian };
        m_blurType = types[option];
        m_dropdownOpen = false;
        m_hoveredOption = -1;
        emit blurTypeChanged(m_blurType);
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

bool MosaicBlurTypeSection::updateHovered(const QPoint& pos)
{
    int newHovered = optionAtPosition(pos);
    if (newHovered != m_hoveredOption) {
        m_hoveredOption = newHovered;
        return true;
    }
    return false;
}
