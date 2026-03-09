#include "qml/PinToolOptionsViewModel.h"
#include "tools/ToolSectionConfig.h"

PinToolOptionsViewModel::PinToolOptionsViewModel(QObject* parent)
    : QObject(parent)
{
    buildColorPalette();
}

void PinToolOptionsViewModel::buildColorPalette()
{
    m_colorPalette.clear();
    // Match ColorSection's default palette
    const QColor colors[] = {
        QColor(220, 53, 69),    // Red
        QColor(255, 240, 120),  // Yellow
        QColor(80, 200, 120),   // Green
        QColor(0x6C, 0x5C, 0xE7), // Primary purple
        QColor(33, 37, 41),     // Black (soft)
        Qt::white               // White
    };
    for (const auto& c : colors)
        m_colorPalette.append(QVariant::fromValue(c));
}

bool PinToolOptionsViewModel::hasContent() const
{
    return m_showColor || m_showWidth || m_showText ||
           m_showArrowStyle || m_showLineStyle || m_showShape ||
           m_showSize || m_showAutoBlur;
}

// ── Section configuration ──

void PinToolOptionsViewModel::showForTool(int toolId)
{
    if (toolId < 0 || toolId >= static_cast<int>(ToolId::Count)) {
        m_showColor = m_showWidth = m_showText = false;
        m_showArrowStyle = m_showLineStyle = m_showShape = false;
        m_showSize = m_showAutoBlur = false;
        if (m_showingEmojiPicker) {
            m_showingEmojiPicker = false;
            emit showingEmojiPickerChanged();
        }
        emit sectionVisibilityChanged();
        return;
    }

    auto id = static_cast<ToolId>(toolId);

    // Special case: EmojiSticker shows emoji picker popup
    if (id == ToolId::EmojiSticker) {
        m_showColor = m_showWidth = m_showText = false;
        m_showArrowStyle = m_showLineStyle = m_showShape = false;
        m_showSize = m_showAutoBlur = false;
        if (!m_showingEmojiPicker) {
            m_showingEmojiPicker = true;
            emit showingEmojiPickerChanged();
        }
        emit sectionVisibilityChanged();
        emit emojiPickerRequested();
        return;
    }

    // Reset emoji picker
    if (m_showingEmojiPicker) {
        m_showingEmojiPicker = false;
        emit showingEmojiPickerChanged();
    }

    // Apply tool section config
    auto config = ToolSectionConfig::forTool(id);
    m_showColor = config.showColorSection;
    m_showWidth = config.showWidthSection;
    m_showText = config.showTextSection;
    m_showArrowStyle = config.showArrowStyleSection;
    m_showLineStyle = config.showLineStyleSection;
    m_showShape = config.showShapeSection;
    m_showSize = config.showSizeSection;
    m_showAutoBlur = config.showAutoBlurSection;

    emit sectionVisibilityChanged();
}

// ── Property setters ──

void PinToolOptionsViewModel::setCurrentColor(const QColor& color)
{
    if (m_currentColor != color) {
        m_currentColor = color;
        emit currentColorChanged();
    }
}

void PinToolOptionsViewModel::setCurrentWidth(int width)
{
    width = qBound(minWidth(), width, maxWidth());
    if (m_currentWidth != width) {
        m_currentWidth = width;
        emit currentWidthChanged();
    }
}

void PinToolOptionsViewModel::setBold(bool value)
{
    if (m_bold != value) {
        m_bold = value;
        emit boldChanged();
    }
}

void PinToolOptionsViewModel::setItalic(bool value)
{
    if (m_italic != value) {
        m_italic = value;
        emit italicChanged();
    }
}

void PinToolOptionsViewModel::setUnderline(bool value)
{
    if (m_underline != value) {
        m_underline = value;
        emit underlineChanged();
    }
}

void PinToolOptionsViewModel::setFontSize(int size)
{
    if (m_fontSize != size) {
        m_fontSize = size;
        emit fontSizeChanged();
    }
}

void PinToolOptionsViewModel::setFontFamily(const QString& family)
{
    if (m_fontFamily != family) {
        m_fontFamily = family;
        emit fontFamilyChanged();
    }
}

void PinToolOptionsViewModel::setShapeType(int type)
{
    if (m_shapeType != type) {
        m_shapeType = type;
        emit shapeTypeChanged();
    }
}

void PinToolOptionsViewModel::setShapeFillMode(int mode)
{
    if (m_shapeFillMode != mode) {
        m_shapeFillMode = mode;
        emit shapeFillModeChanged();
    }
}

void PinToolOptionsViewModel::setArrowStyle(int style)
{
    if (m_arrowStyle != style) {
        m_arrowStyle = style;
        emit arrowStyleChanged();
    }
}

void PinToolOptionsViewModel::setLineStyle(int style)
{
    if (m_lineStyle != style) {
        m_lineStyle = style;
        emit lineStyleChanged();
    }
}

void PinToolOptionsViewModel::setStepBadgeSize(int size)
{
    if (m_stepBadgeSize != size) {
        m_stepBadgeSize = size;
        emit stepBadgeSizeChanged();
    }
}

void PinToolOptionsViewModel::setAutoBlurEnabled(bool value)
{
    if (m_autoBlurEnabled != value) {
        m_autoBlurEnabled = value;
        emit autoBlurEnabledChanged();
    }
}

void PinToolOptionsViewModel::setAutoBlurProcessing(bool value)
{
    if (m_autoBlurProcessing != value) {
        m_autoBlurProcessing = value;
        emit autoBlurProcessingChanged();
    }
}

// ── Option lists for QML ──

QVariantList PinToolOptionsViewModel::arrowStyleOptions() const
{
    // Arrow style previews are drawn by QML/menus directly; keep this model
    // free of icon resource keys so shared consumers cannot accidentally
    // resolve stale SVG paths.
    return {
        QVariantMap{{"value", 0}},
        QVariantMap{{"value", 1}},
        QVariantMap{{"value", 2}},
        QVariantMap{{"value", 3}},
        QVariantMap{{"value", 4}},
        QVariantMap{{"value", 5}},
    };
}

QVariantList PinToolOptionsViewModel::lineStyleOptions() const
{
    // Line style previews are drawn by QML/menus directly; keep this model free
    // of icon resource keys so shared consumers cannot accidentally resolve SVGs.
    return {
        QVariantMap{{"value", 0}},
        QVariantMap{{"value", 1}},
        QVariantMap{{"value", 2}},
    };
}

QVariantList PinToolOptionsViewModel::shapeOptions() const
{
    // ShapeType enum: Rectangle=0, Ellipse=1
    return {
        QVariantMap{{"value", 0}, {"iconKey", "rectangle"}},
        QVariantMap{{"value", 1}, {"iconKey", "ellipse"}},
    };
}

QVariantList PinToolOptionsViewModel::shapeFillOptions() const
{
    // ShapeFillMode enum: Outline=0, Filled=1
    return {
        QVariantMap{{"value", 0}, {"iconKey", "shape-outline"}},
        QVariantMap{{"value", 1}, {"iconKey", "shape-filled"}},
    };
}

QVariantList PinToolOptionsViewModel::stepBadgeSizeOptions() const
{
    // StepBadgeSize enum: Small=0, Medium=1, Large=2
    return {
        QVariantMap{{"value", 0}, {"label", "S"}},
        QVariantMap{{"value", 1}, {"label", "M"}},
        QVariantMap{{"value", 2}, {"label", "L"}},
    };
}

// ── QML action handlers ──

void PinToolOptionsViewModel::handleColorClicked(int index)
{
    if (index >= 0 && index < m_colorPalette.size()) {
        QColor color = m_colorPalette[index].value<QColor>();
        setCurrentColor(color);
        emit colorSelected(color);
    }
}

void PinToolOptionsViewModel::handleCustomColorClicked()
{
    emit customColorPickerRequested();
}

void PinToolOptionsViewModel::handleWidthChanged(int width)
{
    setCurrentWidth(width);
    emit widthValueChanged(m_currentWidth);
}

bool PinToolOptionsViewModel::handleWidthWheelDelta(int delta)
{
    if (!m_showWidth || delta == 0)
        return false;
    int step = delta > 0 ? 1 : -1;
    handleWidthChanged(m_currentWidth + step);
    return true;
}

void PinToolOptionsViewModel::handleBoldToggled()
{
    setBold(!m_bold);
    emit boldToggled(m_bold);
}

void PinToolOptionsViewModel::handleItalicToggled()
{
    setItalic(!m_italic);
    emit italicToggled(m_italic);
}

void PinToolOptionsViewModel::handleUnderlineToggled()
{
    setUnderline(!m_underline);
    emit underlineToggled(m_underline);
}

void PinToolOptionsViewModel::handleFontSizeDropdown(double globalX, double globalY)
{
    emit fontSizeDropdownRequested(globalX, globalY);
}

void PinToolOptionsViewModel::handleFontFamilyDropdown(double globalX, double globalY)
{
    emit fontFamilyDropdownRequested(globalX, globalY);
}

void PinToolOptionsViewModel::handleArrowStyleDropdown(double globalX, double globalY)
{
    emit arrowStyleDropdownRequested(globalX, globalY);
}

void PinToolOptionsViewModel::handleLineStyleDropdown(double globalX, double globalY)
{
    emit lineStyleDropdownRequested(globalX, globalY);
}

void PinToolOptionsViewModel::handleShapeTypeSelected(int type)
{
    setShapeType(type);
    emit shapeTypeSelected(type);
}

void PinToolOptionsViewModel::handleShapeFillModeSelected(int mode)
{
    setShapeFillMode(mode);
    emit shapeFillModeSelected(mode);
}

void PinToolOptionsViewModel::handleArrowStyleSelected(int style)
{
    setArrowStyle(style);
    emit arrowStyleSelected(style);
}

void PinToolOptionsViewModel::handleLineStyleSelected(int style)
{
    setLineStyle(style);
    emit lineStyleSelected(style);
}

void PinToolOptionsViewModel::handleStepBadgeSizeSelected(int size)
{
    setStepBadgeSize(size);
    emit stepBadgeSizeSelected(size);
}

void PinToolOptionsViewModel::handleAutoBlurClicked()
{
    emit autoBlurRequested();
}

void PinToolOptionsViewModel::handleEmojiPickerRequested()
{
    emit emojiPickerRequested();
}
