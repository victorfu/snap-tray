#include "ColorAndWidthWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QDebug>
#include <QtMath>

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

void ColorAndWidthWidget::setShowTextSection(bool show)
{
    m_showTextSection = show;
}

void ColorAndWidthWidget::setBold(bool enabled)
{
    if (m_boldEnabled != enabled) {
        m_boldEnabled = enabled;
        emit boldToggled(m_boldEnabled);
    }
}

void ColorAndWidthWidget::setItalic(bool enabled)
{
    if (m_italicEnabled != enabled) {
        m_italicEnabled = enabled;
        emit italicToggled(m_italicEnabled);
    }
}

void ColorAndWidthWidget::setUnderline(bool enabled)
{
    if (m_underlineEnabled != enabled) {
        m_underlineEnabled = enabled;
        emit underlineToggled(m_underlineEnabled);
    }
}

void ColorAndWidthWidget::setFontSize(int size)
{
    if (m_fontSize != size) {
        m_fontSize = size;
        emit fontSizeChanged(m_fontSize);
    }
}

void ColorAndWidthWidget::setFontFamily(const QString& family)
{
    if (m_fontFamily != family) {
        m_fontFamily = family;
        emit fontFamilyChanged(m_fontFamily);
    }
}

void ColorAndWidthWidget::setShowArrowStyleSection(bool show)
{
    if (m_showArrowStyleSection != show) {
        m_showArrowStyleSection = show;
        if (!show) {
            m_arrowStyleDropdownOpen = false;
            m_hoveredArrowStyleOption = -1;
        }
    }
}

void ColorAndWidthWidget::setArrowStyle(LineEndStyle style)
{
    if (m_arrowStyle != style) {
        m_arrowStyle = style;
        emit arrowStyleChanged(m_arrowStyle);
    }
}

void ColorAndWidthWidget::setShowMosaicStrengthSection(bool show)
{
    if (m_showMosaicStrengthSection != show) {
        m_showMosaicStrengthSection = show;
        if (!show) {
            m_hoveredMosaicStrengthButton = -1;
        }
    }
}

void ColorAndWidthWidget::setMosaicStrength(MosaicStrength strength)
{
    if (m_mosaicStrength != strength) {
        m_mosaicStrength = strength;
        emit mosaicStrengthChanged(m_mosaicStrength);
    }
}

void ColorAndWidthWidget::setShowShapeSection(bool show)
{
    if (m_showShapeSection != show) {
        m_showShapeSection = show;
        if (!show) {
            m_hoveredShapeButton = -1;
        }
    }
}

void ColorAndWidthWidget::setShapeType(ShapeType type)
{
    if (m_shapeType != type) {
        m_shapeType = type;
        emit shapeTypeChanged(m_shapeType);
    }
}

void ColorAndWidthWidget::setShapeFillMode(ShapeFillMode mode)
{
    if (m_shapeFillMode != mode) {
        m_shapeFillMode = mode;
        emit shapeFillModeChanged(m_shapeFillMode);
    }
}

void ColorAndWidthWidget::updatePosition(const QRect& anchorRect, bool above, int screenWidth)
{
    // Calculate total width (using 2 rows, 8 columns - "..." is part of row 2)
    int colorSectionWidth = COLORS_PER_ROW * SWATCH_SIZE + (COLORS_PER_ROW - 1) * SWATCH_SPACING + SECTION_PADDING * 2;
    int totalWidth = colorSectionWidth;
    if (m_showWidthSection) {
        totalWidth += SECTION_SPACING + WIDTH_SECTION_SIZE;
    }
    if (m_showArrowStyleSection) {
        totalWidth += SECTION_SPACING + ARROW_STYLE_BUTTON_WIDTH;
    }
    if (m_showTextSection) {
        // B/I/U buttons + font size + font family dropdowns
        int textSectionWidth = (TEXT_BUTTON_SIZE * 3 + TEXT_BUTTON_SPACING * 2) +
                               SECTION_SPACING + FONT_SIZE_WIDTH +
                               SECTION_SPACING + FONT_FAMILY_WIDTH + SECTION_PADDING;
        totalWidth += SECTION_SPACING + textSectionWidth;
    }
    if (m_showMosaicStrengthSection) {
        // 4 buttons for L/N/S/P
        int mosaicSectionWidth = MOSAIC_BUTTON_WIDTH * 4 + MOSAIC_BUTTON_SPACING * 3 + SECTION_PADDING;
        totalWidth += SECTION_SPACING + mosaicSectionWidth;
    }
    if (m_showShapeSection) {
        // 3 buttons: Rect, Ellipse, Fill toggle
        int shapeSectionWidth = SHAPE_BUTTON_SIZE * 3 + SHAPE_BUTTON_SPACING * 2 + SECTION_PADDING;
        totalWidth += SECTION_SPACING + shapeSectionWidth;
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

    // When widget is above anchor, dropdowns should expand upward
    m_dropdownExpandsUpward = above;

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
    int widthSectionLeft = m_colorSectionRect.right() + SECTION_SPACING;
    if (m_showWidthSection) {
        m_widthSectionRect = QRect(
            widthSectionLeft,
            m_widgetRect.top(),
            WIDTH_SECTION_SIZE,
            WIDGET_HEIGHT
        );
    } else {
        m_widthSectionRect = QRect();
    }

    // === ARROW STYLE SECTION ===
    if (m_showArrowStyleSection) {
        int arrowStyleLeft = m_showWidthSection ?
            m_widthSectionRect.right() + SECTION_SPACING :
            m_colorSectionRect.right() + SECTION_SPACING;

        // Center the button vertically
        int buttonY = m_widgetRect.top() + (WIDGET_HEIGHT - ARROW_STYLE_BUTTON_HEIGHT) / 2;
        m_arrowStyleButtonRect = QRect(arrowStyleLeft, buttonY, ARROW_STYLE_BUTTON_WIDTH, ARROW_STYLE_BUTTON_HEIGHT);

        // Dropdown rect - expand upward when widget is above anchor (ScreenCanvas)
        int dropdownWidth = ARROW_STYLE_BUTTON_WIDTH + 20;  // Slightly wider
        int dropdownHeight = 2 * ARROW_STYLE_DROPDOWN_OPTION_HEIGHT;  // 2 options
        if (m_dropdownExpandsUpward) {
            // Expand upward (widget is above toolbar)
            m_arrowStyleDropdownRect = QRect(
                m_arrowStyleButtonRect.left(),
                m_arrowStyleButtonRect.top() - dropdownHeight - 2,
                dropdownWidth,
                dropdownHeight
            );
        } else {
            // Expand downward (default)
            m_arrowStyleDropdownRect = QRect(
                m_arrowStyleButtonRect.left(),
                m_arrowStyleButtonRect.bottom() + 2,
                dropdownWidth,
                dropdownHeight
            );
        }
    } else {
        m_arrowStyleButtonRect = QRect();
        m_arrowStyleDropdownRect = QRect();
    }

    // === TEXT SECTION (B/I/U toggles + font size + font family) ===
    if (m_showTextSection) {
        int textSectionLeft;
        if (m_showArrowStyleSection) {
            textSectionLeft = m_arrowStyleButtonRect.right() + SECTION_SPACING;
        } else if (m_showWidthSection) {
            textSectionLeft = m_widthSectionRect.right() + SECTION_SPACING;
        } else {
            textSectionLeft = m_colorSectionRect.right() + SECTION_SPACING;
        }

        int textSectionWidth = (TEXT_BUTTON_SIZE * 3 + TEXT_BUTTON_SPACING * 2) +
                               SECTION_SPACING + FONT_SIZE_WIDTH +
                               SECTION_SPACING + FONT_FAMILY_WIDTH + SECTION_PADDING;

        m_textSectionRect = QRect(
            textSectionLeft,
            m_widgetRect.top(),
            textSectionWidth,
            WIDGET_HEIGHT
        );

        // Center buttons vertically
        int buttonY = m_widgetRect.top() + (WIDGET_HEIGHT - TEXT_BUTTON_SIZE) / 2;
        int currentX = textSectionLeft + SECTION_PADDING;

        // B/I/U buttons
        m_boldButtonRect = QRect(currentX, buttonY, TEXT_BUTTON_SIZE, TEXT_BUTTON_SIZE);
        currentX += TEXT_BUTTON_SIZE + TEXT_BUTTON_SPACING;

        m_italicButtonRect = QRect(currentX, buttonY, TEXT_BUTTON_SIZE, TEXT_BUTTON_SIZE);
        currentX += TEXT_BUTTON_SIZE + TEXT_BUTTON_SPACING;

        m_underlineButtonRect = QRect(currentX, buttonY, TEXT_BUTTON_SIZE, TEXT_BUTTON_SIZE);
        currentX += TEXT_BUTTON_SIZE + SECTION_SPACING;

        // Font size dropdown
        m_fontSizeRect = QRect(currentX, buttonY, FONT_SIZE_WIDTH, TEXT_BUTTON_SIZE);
        currentX += FONT_SIZE_WIDTH + SECTION_SPACING;

        // Font family dropdown
        m_fontFamilyRect = QRect(currentX, buttonY, FONT_FAMILY_WIDTH, TEXT_BUTTON_SIZE);
    } else {
        m_textSectionRect = QRect();
        m_boldButtonRect = QRect();
        m_italicButtonRect = QRect();
        m_underlineButtonRect = QRect();
        m_fontSizeRect = QRect();
        m_fontFamilyRect = QRect();
    }

    // === MOSAIC STRENGTH SECTION (4 buttons: L/N/S/P) ===
    if (m_showMosaicStrengthSection) {
        int mosaicSectionLeft;
        if (m_showTextSection) {
            mosaicSectionLeft = m_textSectionRect.right() + SECTION_SPACING;
        } else if (m_showArrowStyleSection) {
            mosaicSectionLeft = m_arrowStyleButtonRect.right() + SECTION_SPACING;
        } else if (m_showWidthSection) {
            mosaicSectionLeft = m_widthSectionRect.right() + SECTION_SPACING;
        } else {
            mosaicSectionLeft = m_colorSectionRect.right() + SECTION_SPACING;
        }

        int mosaicSectionWidth = MOSAIC_BUTTON_WIDTH * 4 + MOSAIC_BUTTON_SPACING * 3 + SECTION_PADDING;
        m_mosaicStrengthSectionRect = QRect(
            mosaicSectionLeft,
            m_widgetRect.top(),
            mosaicSectionWidth,
            WIDGET_HEIGHT
        );

        // Center buttons vertically
        int buttonY = m_widgetRect.top() + (WIDGET_HEIGHT - MOSAIC_BUTTON_HEIGHT) / 2;
        m_mosaicStrengthButtonRects.resize(4);
        for (int i = 0; i < 4; ++i) {
            m_mosaicStrengthButtonRects[i] = QRect(
                mosaicSectionLeft + SECTION_PADDING / 2 + i * (MOSAIC_BUTTON_WIDTH + MOSAIC_BUTTON_SPACING),
                buttonY,
                MOSAIC_BUTTON_WIDTH,
                MOSAIC_BUTTON_HEIGHT
            );
        }
    } else {
        m_mosaicStrengthSectionRect = QRect();
        m_mosaicStrengthButtonRects.clear();
    }

    // === SHAPE SECTION (3 buttons: Rect, Ellipse, Fill toggle) ===
    if (m_showShapeSection) {
        int shapeSectionLeft;
        if (m_showMosaicStrengthSection) {
            shapeSectionLeft = m_mosaicStrengthSectionRect.right() + SECTION_SPACING;
        } else if (m_showTextSection) {
            shapeSectionLeft = m_textSectionRect.right() + SECTION_SPACING;
        } else if (m_showArrowStyleSection) {
            shapeSectionLeft = m_arrowStyleButtonRect.right() + SECTION_SPACING;
        } else if (m_showWidthSection) {
            shapeSectionLeft = m_widthSectionRect.right() + SECTION_SPACING;
        } else {
            shapeSectionLeft = m_colorSectionRect.right() + SECTION_SPACING;
        }

        int shapeSectionWidth = SHAPE_BUTTON_SIZE * 3 + SHAPE_BUTTON_SPACING * 2 + SECTION_PADDING;
        m_shapeSectionRect = QRect(
            shapeSectionLeft,
            m_widgetRect.top(),
            shapeSectionWidth,
            WIDGET_HEIGHT
        );

        // Center buttons vertically
        int buttonY = m_widgetRect.top() + (WIDGET_HEIGHT - SHAPE_BUTTON_SIZE) / 2;
        m_shapeButtonRects.resize(3);
        for (int i = 0; i < 3; ++i) {
            m_shapeButtonRects[i] = QRect(
                shapeSectionLeft + SECTION_PADDING / 2 + i * (SHAPE_BUTTON_SIZE + SHAPE_BUTTON_SPACING),
                buttonY,
                SHAPE_BUTTON_SIZE,
                SHAPE_BUTTON_SIZE
            );
        }
    } else {
        m_shapeSectionRect = QRect();
        m_shapeButtonRects.clear();
    }
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

    // Draw arrow style section (if enabled)
    if (m_showArrowStyleSection) {
        drawArrowStyleSection(painter);
    }

    // Draw text section (if enabled)
    if (m_showTextSection) {
        drawTextSection(painter);
    }

    // Draw mosaic strength section (if enabled)
    if (m_showMosaicStrengthSection) {
        drawMosaicStrengthSection(painter);
        drawMosaicStrengthTooltip(painter);
    }

    // Draw shape section (if enabled)
    if (m_showShapeSection) {
        drawShapeSection(painter);
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

void ColorAndWidthWidget::drawTextSection(QPainter& painter)
{
    // Helper lambda to draw a toggle button
    auto drawToggleButton = [&](const QRect& rect, const QString& text, bool enabled, bool hovered, bool isItalicStyle = false, bool isUnderlineStyle = false) {
        // Background
        QColor bgColor;
        if (enabled) {
            bgColor = QColor(0, 122, 255);  // Blue when active
        } else if (hovered) {
            bgColor = QColor(80, 80, 80);
        } else {
            bgColor = QColor(50, 50, 50);
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
        painter.setPen(enabled ? Qt::white : QColor(200, 200, 200));
        painter.drawText(rect, Qt::AlignCenter, text);
    };

    // Helper lambda to draw a dropdown button
    auto drawDropdown = [&](const QRect& rect, const QString& text, bool hovered) {
        // Background
        QColor bgColor = hovered ? QColor(80, 80, 80) : QColor(50, 50, 50);
        painter.setPen(QPen(QColor(70, 70, 70), 1));
        painter.setBrush(bgColor);
        painter.drawRoundedRect(rect, 4, 4);

        // Text (with dropdown arrow)
        QFont font = painter.font();
        font.setPointSize(10);
        font.setBold(false);
        font.setItalic(false);
        font.setUnderline(false);
        painter.setFont(font);
        painter.setPen(QColor(200, 200, 200));

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
        painter.fillPath(arrow, QColor(180, 180, 180));
    };

    // Draw B/I/U toggle buttons
    drawToggleButton(m_boldButtonRect, "B", m_boldEnabled, m_hoveredTextControl == 0);
    drawToggleButton(m_italicButtonRect, "I", m_italicEnabled, m_hoveredTextControl == 1, true);
    drawToggleButton(m_underlineButtonRect, "U", m_underlineEnabled, m_hoveredTextControl == 2, false, true);

    // Draw font size dropdown
    QString sizeText = QString::number(m_fontSize);
    drawDropdown(m_fontSizeRect, sizeText, m_hoveredTextControl == 3);

    // Draw font family dropdown
    QString familyText = m_fontFamily.isEmpty() ? tr("Default") : m_fontFamily;
    // Truncate if too long
    QFontMetrics fm(painter.font());
    familyText = fm.elidedText(familyText, Qt::ElideRight, FONT_FAMILY_WIDTH - 16);
    drawDropdown(m_fontFamilyRect, familyText, m_hoveredTextControl == 4);
}

void ColorAndWidthWidget::drawArrowStyleSection(QPainter& painter)
{
    // Draw the button showing current style
    bool buttonHovered = m_hoveredArrowStyleOption == -2;  // Special value for button hover
    QColor bgColor = buttonHovered ? QColor(80, 80, 80) : QColor(50, 50, 50);
    painter.setPen(QPen(QColor(70, 70, 70), 1));
    painter.setBrush(bgColor);
    painter.drawRoundedRect(m_arrowStyleButtonRect, 4, 4);

    // Draw current style icon in the button
    QRect iconRect = m_arrowStyleButtonRect.adjusted(4, 4, -12, -4);
    drawArrowStyleIcon(painter, m_arrowStyle, iconRect);

    // Draw dropdown arrow
    int arrowX = m_arrowStyleButtonRect.right() - 8;
    int arrowY = m_arrowStyleButtonRect.center().y();
    QPainterPath arrow;
    arrow.moveTo(arrowX - 3, arrowY - 2);
    arrow.lineTo(arrowX + 3, arrowY - 2);
    arrow.lineTo(arrowX, arrowY + 2);
    arrow.closeSubpath();
    painter.fillPath(arrow, QColor(180, 180, 180));

    // Draw dropdown menu if open
    if (m_arrowStyleDropdownOpen) {
        // Draw dropdown background
        painter.setPen(QPen(QColor(70, 70, 70), 1));
        painter.setBrush(QColor(50, 50, 50, 250));
        painter.drawRoundedRect(m_arrowStyleDropdownRect, 4, 4);

        // Draw each option (2 options: None, EndArrow)
        LineEndStyle styles[] = { LineEndStyle::None, LineEndStyle::EndArrow };
        for (int i = 0; i < 2; ++i) {
            QRect optionRect(
                m_arrowStyleDropdownRect.left(),
                m_arrowStyleDropdownRect.top() + i * ARROW_STYLE_DROPDOWN_OPTION_HEIGHT,
                m_arrowStyleDropdownRect.width(),
                ARROW_STYLE_DROPDOWN_OPTION_HEIGHT
            );

            bool isHovered = (m_hoveredArrowStyleOption == i);
            bool isSelected = (m_arrowStyle == styles[i]);

            // Highlight hovered or selected option
            if (isHovered || isSelected) {
                QColor highlightColor = isSelected ? QColor(0, 122, 255, 100) : QColor(80, 80, 80);
                painter.setPen(Qt::NoPen);
                painter.setBrush(highlightColor);
                painter.drawRoundedRect(optionRect.adjusted(2, 2, -2, -2), 3, 3);
            }

            // Draw the style icon
            QRect iconRect = optionRect.adjusted(8, 4, -8, -4);
            drawArrowStyleIcon(painter, styles[i], iconRect, isHovered);
        }
    }
}

void ColorAndWidthWidget::drawArrowStyleIcon(QPainter& painter, LineEndStyle style, const QRect& rect, bool isHovered) const
{
    Q_UNUSED(isHovered);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Line parameters
    int lineY = rect.center().y();
    int lineLeft = rect.left() + 4;
    int lineRight = rect.right() - 4;
    int lineWidth = 2;

    QPen linePen(QColor(200, 200, 200), lineWidth, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(linePen);
    painter.setBrush(QColor(200, 200, 200));

    // Draw the line
    painter.drawLine(lineLeft, lineY, lineRight, lineY);

    // Arrowhead parameters
    int arrowSize = 6;
    int dotRadius = 4;

    auto drawArrowhead = [&](int tipX, bool pointRight) {
        int direction = pointRight ? -1 : 1;  // Arrow body extends opposite to tip direction
        QPointF tip(tipX, lineY);
        QPointF p1(tipX + direction * arrowSize, lineY - arrowSize * 0.5);
        QPointF p2(tipX + direction * arrowSize, lineY + arrowSize * 0.5);

        QPainterPath arrowPath;
        arrowPath.moveTo(tip);
        arrowPath.lineTo(p1);
        arrowPath.lineTo(p2);
        arrowPath.closeSubpath();
        painter.drawPath(arrowPath);
    };

    auto drawDot = [&](int centerX) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(200, 200, 200));
        painter.drawEllipse(QPoint(centerX, lineY), dotRadius, dotRadius);
    };

    switch (style) {
    case LineEndStyle::None:
        // Just the line, no arrowheads or dots
        break;
    case LineEndStyle::EndArrow:
        drawArrowhead(lineRight, true);
        break;
    }

    painter.restore();
}

int ColorAndWidthWidget::arrowStyleOptionAtPosition(const QPoint& pos) const
{
    if (!m_showArrowStyleSection) return -1;

    // Check if in dropdown
    if (m_arrowStyleDropdownOpen && m_arrowStyleDropdownRect.contains(pos)) {
        int relativeY = pos.y() - m_arrowStyleDropdownRect.top();
        int option = relativeY / ARROW_STYLE_DROPDOWN_OPTION_HEIGHT;
        if (option >= 0 && option < 2) {  // 2 options
            return option;
        }
    }

    // Check if on button
    if (m_arrowStyleButtonRect.contains(pos)) {
        return -2;  // Special value for button
    }

    return -1;
}

int ColorAndWidthWidget::textControlAtPosition(const QPoint& pos) const
{
    if (!m_showTextSection) return -1;

    if (m_boldButtonRect.contains(pos)) return 0;
    if (m_italicButtonRect.contains(pos)) return 1;
    if (m_underlineButtonRect.contains(pos)) return 2;
    if (m_fontSizeRect.contains(pos)) return 3;
    if (m_fontFamilyRect.contains(pos)) return 4;

    return -1;
}

void ColorAndWidthWidget::drawMosaicStrengthSection(QPainter& painter)
{
    // Labels for strength levels: Light, Normal, Strong, Paranoid
    static const char* labels[] = { "L", "N", "S", "P" };
    static const MosaicStrength strengths[] = {
        MosaicStrength::Light, MosaicStrength::Normal,
        MosaicStrength::Strong, MosaicStrength::Paranoid
    };

    for (int i = 0; i < 4; ++i) {
        if (i >= m_mosaicStrengthButtonRects.size()) break;

        QRect rect = m_mosaicStrengthButtonRects[i];
        bool isSelected = (m_mosaicStrength == strengths[i]);
        bool isHovered = (m_hoveredMosaicStrengthButton == i);

        // Background
        QColor bgColor;
        if (isSelected) {
            bgColor = QColor(0, 122, 255);  // Blue when active
        } else if (isHovered) {
            bgColor = QColor(80, 80, 80);
        } else {
            bgColor = QColor(50, 50, 50);
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(bgColor);
        painter.drawRoundedRect(rect, 4, 4);

        // Text
        QFont font = painter.font();
        font.setPointSize(10);
        font.setBold(isSelected);
        painter.setFont(font);
        painter.setPen(isSelected ? Qt::white : QColor(200, 200, 200));
        painter.drawText(rect, Qt::AlignCenter, labels[i]);
    }
}

int ColorAndWidthWidget::mosaicStrengthButtonAtPosition(const QPoint& pos) const
{
    if (!m_showMosaicStrengthSection) return -1;

    for (int i = 0; i < m_mosaicStrengthButtonRects.size(); ++i) {
        if (m_mosaicStrengthButtonRects[i].contains(pos)) {
            return i;
        }
    }

    return -1;
}

void ColorAndWidthWidget::drawShapeSection(QPainter& painter)
{
    for (int i = 0; i < 3; ++i) {
        if (i >= m_shapeButtonRects.size()) break;

        QRect rect = m_shapeButtonRects[i];
        // Buttons 0-1 are shape type, button 2 is fill mode toggle
        bool isSelected;
        if (i < 2) {
            isSelected = (m_shapeType == (i == 0 ? ShapeType::Rectangle : ShapeType::Ellipse));
        } else {
            isSelected = (m_shapeFillMode == ShapeFillMode::Filled);
        }
        bool isHovered = (m_hoveredShapeButton == i);

        // Background
        QColor bgColor;
        if (isSelected) {
            bgColor = QColor(0, 122, 255);  // Blue when active
        } else if (isHovered) {
            bgColor = QColor(80, 80, 80);
        } else {
            bgColor = QColor(50, 50, 50);
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(bgColor);
        painter.drawRoundedRect(rect, 4, 4);

        // Draw icon based on button index
        QColor iconColor = isSelected ? Qt::white : QColor(200, 200, 200);
        QRect iconRect = rect.adjusted(5, 5, -5, -5);

        switch (i) {
        case 0:  // Rectangle shape - outline square
            painter.setPen(QPen(iconColor, 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(iconRect);
            break;
        case 1:  // Ellipse shape - outline circle
            painter.setPen(QPen(iconColor, 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(iconRect);
            break;
        case 2:  // Fill toggle - shows filled or outline based on current mode
            if (m_shapeFillMode == ShapeFillMode::Filled) {
                // Show filled square
                painter.setPen(QPen(iconColor, 1.5));
                painter.setBrush(iconColor);
                painter.drawRect(iconRect);
            } else {
                // Show outline square (dashed to indicate "outline mode")
                QPen dashedPen(iconColor, 1.5, Qt::DashLine);
                dashedPen.setDashPattern({2, 2});
                painter.setPen(dashedPen);
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(iconRect);
            }
            break;
        }
    }
}

int ColorAndWidthWidget::shapeButtonAtPosition(const QPoint& pos) const
{
    if (!m_showShapeSection) return -1;

    for (int i = 0; i < m_shapeButtonRects.size(); ++i) {
        if (m_shapeButtonRects[i].contains(pos)) {
            return i;
        }
    }

    return -1;
}

void ColorAndWidthWidget::drawMosaicStrengthTooltip(QPainter& painter)
{
    if (m_hoveredMosaicStrengthButton < 0 || m_mosaicStrengthTooltip.isEmpty()) return;
    if (m_hoveredMosaicStrengthButton >= m_mosaicStrengthButtonRects.size()) return;

    QFont font = painter.font();
    font.setPointSize(11);
    painter.setFont(font);

    QFontMetrics fm(font);
    QRect textRect = fm.boundingRect(m_mosaicStrengthTooltip);
    textRect.adjust(-8, -4, 8, 4);

    // Position tooltip above the button
    QRect btnRect = m_mosaicStrengthButtonRects[m_hoveredMosaicStrengthButton];
    int tooltipX = btnRect.center().x() - textRect.width() / 2;
    int tooltipY = m_widgetRect.top() - textRect.height() - 6;

    textRect.moveTo(tooltipX, tooltipY);

    // Draw tooltip background
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(30, 30, 30, 230));
    painter.drawRoundedRect(textRect, 4, 4);

    // Draw tooltip border
    painter.setPen(QColor(80, 80, 80));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(textRect, 4, 4);

    // Draw tooltip text
    painter.setPen(Qt::white);
    painter.drawText(textRect, Qt::AlignCenter, m_mosaicStrengthTooltip);
}

bool ColorAndWidthWidget::contains(const QPoint& pos) const
{
    if (m_widgetRect.contains(pos)) return true;
    // Also include dropdown area when open
    if (m_arrowStyleDropdownOpen && m_arrowStyleDropdownRect.contains(pos)) return true;
    return false;
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
    // Handle arrow style dropdown clicks (even outside main widget)
    if (m_showArrowStyleSection) {
        int arrowOption = arrowStyleOptionAtPosition(pos);
        if (arrowOption >= 0) {
            // Clicked on a dropdown option (2 options: None, EndArrow)
            LineEndStyle styles[] = { LineEndStyle::None, LineEndStyle::EndArrow };
            m_arrowStyle = styles[arrowOption];
            m_arrowStyleDropdownOpen = false;
            m_hoveredArrowStyleOption = -1;
            emit arrowStyleChanged(m_arrowStyle);
            return true;
        } else if (arrowOption == -2) {
            // Clicked on the button - toggle dropdown
            m_arrowStyleDropdownOpen = !m_arrowStyleDropdownOpen;
            return true;
        } else if (m_arrowStyleDropdownOpen) {
            // Clicked outside dropdown while open - close it
            m_arrowStyleDropdownOpen = false;
            m_hoveredArrowStyleOption = -1;
            // Don't return true - let other handlers process the click
        }
    }

    if (!m_widgetRect.contains(pos)) return false;

    // Text section
    if (m_showTextSection) {
        int textControl = textControlAtPosition(pos);
        if (textControl >= 0) {
            switch (textControl) {
                case 0:  // Bold
                    m_boldEnabled = !m_boldEnabled;
                    emit boldToggled(m_boldEnabled);
                    qDebug() << "ColorAndWidthWidget: Bold toggled:" << m_boldEnabled;
                    break;
                case 1:  // Italic
                    m_italicEnabled = !m_italicEnabled;
                    emit italicToggled(m_italicEnabled);
                    qDebug() << "ColorAndWidthWidget: Italic toggled:" << m_italicEnabled;
                    break;
                case 2:  // Underline
                    m_underlineEnabled = !m_underlineEnabled;
                    emit underlineToggled(m_underlineEnabled);
                    qDebug() << "ColorAndWidthWidget: Underline toggled:" << m_underlineEnabled;
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
    }

    // Mosaic strength section
    if (m_showMosaicStrengthSection) {
        int btn = mosaicStrengthButtonAtPosition(pos);
        if (btn >= 0) {
            MosaicStrength strengths[] = {
                MosaicStrength::Light, MosaicStrength::Normal,
                MosaicStrength::Strong, MosaicStrength::Paranoid
            };
            m_mosaicStrength = strengths[btn];
            emit mosaicStrengthChanged(m_mosaicStrength);
            qDebug() << "ColorAndWidthWidget: Mosaic strength changed to" << btn;
            return true;
        }
    }

    // Shape section
    if (m_showShapeSection) {
        int btn = shapeButtonAtPosition(pos);
        if (btn >= 0) {
            if (btn < 2) {
                // Shape type buttons (0 = Rectangle, 1 = Ellipse)
                m_shapeType = (btn == 0) ? ShapeType::Rectangle : ShapeType::Ellipse;
                emit shapeTypeChanged(m_shapeType);
                qDebug() << "ColorAndWidthWidget: Shape type changed to" << btn;
            } else {
                // Button 2: Toggle fill mode
                m_shapeFillMode = (m_shapeFillMode == ShapeFillMode::Outline)
                    ? ShapeFillMode::Filled : ShapeFillMode::Outline;
                emit shapeFillModeChanged(m_shapeFillMode);
                qDebug() << "ColorAndWidthWidget: Shape fill mode toggled to" << static_cast<int>(m_shapeFillMode);
            }
            return true;
        }
    }

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

    // Update arrow style option hover
    if (m_showArrowStyleSection) {
        int newHoveredArrowOption = arrowStyleOptionAtPosition(pos);
        if (newHoveredArrowOption != m_hoveredArrowStyleOption) {
            m_hoveredArrowStyleOption = newHoveredArrowOption;
            changed = true;
        }
    } else if (m_hoveredArrowStyleOption != -1) {
        m_hoveredArrowStyleOption = -1;
        changed = true;
    }

    // Update text control hover
    if (m_showTextSection) {
        int newHoveredTextControl = textControlAtPosition(pos);
        if (newHoveredTextControl != m_hoveredTextControl) {
            m_hoveredTextControl = newHoveredTextControl;
            changed = true;
        }
    } else if (m_hoveredTextControl != -1) {
        m_hoveredTextControl = -1;
        changed = true;
    }

    // Update mosaic strength button hover
    if (m_showMosaicStrengthSection) {
        int newHoveredMosaicButton = mosaicStrengthButtonAtPosition(pos);
        if (newHoveredMosaicButton != m_hoveredMosaicStrengthButton) {
            m_hoveredMosaicStrengthButton = newHoveredMosaicButton;
            // Update tooltip text
            static const QString tooltips[] = { "Light", "Normal", "Strong", "Paranoid" };
            if (m_hoveredMosaicStrengthButton >= 0 && m_hoveredMosaicStrengthButton < 4) {
                m_mosaicStrengthTooltip = tooltips[m_hoveredMosaicStrengthButton];
            } else {
                m_mosaicStrengthTooltip.clear();
            }
            changed = true;
        }
    } else if (m_hoveredMosaicStrengthButton != -1) {
        m_hoveredMosaicStrengthButton = -1;
        m_mosaicStrengthTooltip.clear();
        changed = true;
    }

    // Update shape button hover
    if (m_showShapeSection) {
        int newHoveredShapeButton = shapeButtonAtPosition(pos);
        if (newHoveredShapeButton != m_hoveredShapeButton) {
            m_hoveredShapeButton = newHoveredShapeButton;
            changed = true;
        }
    } else if (m_hoveredShapeButton != -1) {
        m_hoveredShapeButton = -1;
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
