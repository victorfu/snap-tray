#include "ui/sections/TextSection.h"
#include "ToolbarStyle.h"

#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>

TextSection::TextSection(QObject* parent)
    : QObject(parent)
{
}

void TextSection::setBold(bool enabled)
{
    if (m_boldEnabled != enabled) {
        m_boldEnabled = enabled;
        emit boldToggled(m_boldEnabled);
    }
}

void TextSection::setItalic(bool enabled)
{
    if (m_italicEnabled != enabled) {
        m_italicEnabled = enabled;
        emit italicToggled(m_italicEnabled);
    }
}

void TextSection::setUnderline(bool enabled)
{
    if (m_underlineEnabled != enabled) {
        m_underlineEnabled = enabled;
        emit underlineToggled(m_underlineEnabled);
    }
}

void TextSection::setFontSize(int size)
{
    if (m_fontSize != size) {
        m_fontSize = size;
        emit fontSizeChanged(m_fontSize);
    }
}

void TextSection::setFontFamily(const QString& family)
{
    if (m_fontFamily != family) {
        m_fontFamily = family;
        emit fontFamilyChanged(m_fontFamily);
    }
}

int TextSection::preferredWidth() const
{
    // B/I/U buttons + font size + font family dropdowns
    return SECTION_PADDING +
           (BUTTON_SIZE * 3 + BUTTON_SPACING * 2) +
           SECTION_SPACING + FONT_SIZE_WIDTH +
           SECTION_SPACING + FONT_FAMILY_WIDTH +
           SECTION_PADDING;
}

void TextSection::updateLayout(int containerTop, int containerHeight, int xOffset)
{
    m_sectionRect = QRect(xOffset, containerTop, preferredWidth(), containerHeight);

    // Center buttons vertically
    int buttonY = containerTop + (containerHeight - BUTTON_SIZE) / 2;
    int currentX = xOffset + SECTION_PADDING;

    // B/I/U buttons
    m_boldButtonRect = QRect(currentX, buttonY, BUTTON_SIZE, BUTTON_SIZE);
    currentX += BUTTON_SIZE + BUTTON_SPACING;

    m_italicButtonRect = QRect(currentX, buttonY, BUTTON_SIZE, BUTTON_SIZE);
    currentX += BUTTON_SIZE + BUTTON_SPACING;

    m_underlineButtonRect = QRect(currentX, buttonY, BUTTON_SIZE, BUTTON_SIZE);
    currentX += BUTTON_SIZE + SECTION_SPACING;

    // Font size dropdown
    m_fontSizeRect = QRect(currentX, buttonY, FONT_SIZE_WIDTH, BUTTON_SIZE);
    currentX += FONT_SIZE_WIDTH + SECTION_SPACING;

    // Font family dropdown
    m_fontFamilyRect = QRect(currentX, buttonY, FONT_FAMILY_WIDTH, BUTTON_SIZE);
}

void TextSection::draw(QPainter& painter, const ToolbarStyleConfig& styleConfig)
{
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Helper lambda to draw a toggle button
    auto drawToggleButton = [&](const QRect& rect, const QString& text, bool enabled, bool hovered,
                                bool isItalicStyle = false, bool isUnderlineStyle = false) {
        // Background
        QColor bgColor;
        if (enabled) {
            bgColor = styleConfig.buttonActiveColor;
        } else if (hovered) {
            bgColor = styleConfig.buttonHoverColor;
        } else {
            bgColor = styleConfig.buttonInactiveColor;
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(bgColor);
        painter.drawRoundedRect(rect, 4, 4);

        // Text
        QFont font = painter.font();
        font.setPointSize(11);
        font.setBold(text == "B");
        font.setItalic(isItalicStyle);
        font.setUnderline(isUnderlineStyle);
        painter.setFont(font);
        painter.setPen(enabled ? styleConfig.textActiveColor : styleConfig.textColor);
        painter.drawText(rect, Qt::AlignCenter, text);
    };

    // Helper lambda to draw a dropdown button
    auto drawDropdown = [&](const QRect& rect, const QString& text, bool hovered) {
        // Background
        QColor bgColor = hovered ? styleConfig.buttonHoverColor : styleConfig.buttonInactiveColor;
        painter.setPen(QPen(styleConfig.dropdownBorder, 1));
        painter.setBrush(bgColor);
        painter.drawRoundedRect(rect, 4, 4);

        // Text (with dropdown arrow)
        QFont font = painter.font();
        font.setPointSize(10);
        font.setBold(false);
        font.setItalic(false);
        font.setUnderline(false);
        painter.setFont(font);
        painter.setPen(styleConfig.textColor);

        // Draw text left-aligned with some padding
        QRect textRect = rect.adjusted(4, 0, -12, 0);
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);

        // Draw dropdown arrow
        int arrowX = rect.right() - 10;
        int arrowY = rect.center().y();
        QPainterPath arrow;
        arrow.moveTo(arrowX - 3, arrowY - 2);
        arrow.lineTo(arrowX + 3, arrowY - 2);
        arrow.lineTo(arrowX, arrowY + 2);
        arrow.closeSubpath();
        painter.fillPath(arrow, styleConfig.textColor);
    };

    // Draw B/I/U toggle buttons
    drawToggleButton(m_boldButtonRect, "B", m_boldEnabled, m_hoveredControl == 0);
    drawToggleButton(m_italicButtonRect, "I", m_italicEnabled, m_hoveredControl == 1, true);
    drawToggleButton(m_underlineButtonRect, "U", m_underlineEnabled, m_hoveredControl == 2, false, true);

    // Draw font size dropdown
    QString sizeText = QString::number(m_fontSize);
    drawDropdown(m_fontSizeRect, sizeText, m_hoveredControl == 3);

    // Draw font family dropdown
    QString familyText = m_fontFamily.isEmpty() ? tr("Default") : m_fontFamily;
    // Truncate if too long
    QFontMetrics fm(painter.font());
    familyText = fm.elidedText(familyText, Qt::ElideRight, FONT_FAMILY_WIDTH - 16);
    drawDropdown(m_fontFamilyRect, familyText, m_hoveredControl == 4);
}

bool TextSection::contains(const QPoint& pos) const
{
    return m_sectionRect.contains(pos);
}

int TextSection::controlAtPosition(const QPoint& pos) const
{
    if (m_boldButtonRect.contains(pos)) return 0;
    if (m_italicButtonRect.contains(pos)) return 1;
    if (m_underlineButtonRect.contains(pos)) return 2;
    if (m_fontSizeRect.contains(pos)) return 3;
    if (m_fontFamilyRect.contains(pos)) return 4;
    return -1;
}

bool TextSection::handleClick(const QPoint& pos)
{
    int control = controlAtPosition(pos);
    if (control >= 0) {
        switch (control) {
            case 0:  // Bold
                m_boldEnabled = !m_boldEnabled;
                emit boldToggled(m_boldEnabled);
                break;
            case 1:  // Italic
                m_italicEnabled = !m_italicEnabled;
                emit italicToggled(m_italicEnabled);
                break;
            case 2:  // Underline
                m_underlineEnabled = !m_underlineEnabled;
                emit underlineToggled(m_underlineEnabled);
                break;
            case 3:  // Font size dropdown
                emit fontSizeDropdownRequested(m_fontSizeRect.bottomLeft());
                break;
            case 4:  // Font family dropdown
                emit fontFamilyDropdownRequested(m_fontFamilyRect.bottomLeft());
                break;
        }
        return true;
    }
    return false;
}

bool TextSection::updateHovered(const QPoint& pos)
{
    int newHovered = controlAtPosition(pos);
    if (newHovered != m_hoveredControl) {
        m_hoveredControl = newHovered;
        return true;
    }
    return false;
}

void TextSection::resetHoverState()
{
    m_hoveredControl = -1;
}
