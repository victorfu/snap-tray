#include "qml/PinToolOptionsViewModel.h"
#include "annotations/AnnotationStyleLabels.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/LineStyle.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/StepBadgeAnnotation.h"
#include "tools/ToolSectionConfig.h"
#include "tools/ToolWidthSlot.h"
#include "ui/DesignSystem.h"

#include <iterator>

namespace {
struct MosaicWidthPreset
{
    int value;
    int previewDiameter;
};

constexpr MosaicWidthPreset kMosaicWidthPresets[] = {
    {ToolWidthDefaults::kMosaicBrushSmall, 6},
    {ToolWidthDefaults::kMosaicBrushMedium, 10},
    {ToolWidthDefaults::kMosaicBrushLarge, 14},
};
} // namespace

PinToolOptionsViewModel::PinToolOptionsViewModel(QObject* parent)
    : QObject(parent)
{
    buildColorPalette();
}

void PinToolOptionsViewModel::buildColorPalette()
{
    m_colorPalette.clear();
    m_colorPaletteNames.clear();
    // Match ColorSection's default palette. Each color keeps its name, which
    // screen readers read for its swatch (#248).
    const struct {
        QColor color;
        const char* name;
    } entries[] = {
        {DesignSystem::instance().annotationRed(), QT_TR_NOOP("Red")},
        {QColor(255, 240, 120), QT_TR_NOOP("Yellow")},
        {QColor(80, 200, 120), QT_TR_NOOP("Green")},
        {QColor(0x6C, 0x5C, 0xE7), QT_TR_NOOP("Purple")}, // Primary purple
        {QColor(33, 37, 41), QT_TR_NOOP("Black")},         // Soft black
        {QColor(Qt::white), QT_TR_NOOP("White")},
    };
    for (const auto& entry : entries) {
        m_colorPalette.append(QVariant::fromValue(entry.color));
        m_colorPaletteNames.append(QString::fromLatin1(entry.name));
    }
}

QVariantList PinToolOptionsViewModel::colorPaletteOptions() const
{
    QVariantList options;
    options.reserve(m_colorPalette.size());
    for (qsizetype i = 0; i < m_colorPalette.size(); ++i) {
        options.append(QVariantMap{
            {"color", m_colorPalette.at(i)},
            {"name", tr(m_colorPaletteNames.at(i).toLatin1().constData())},
        });
    }
    return options;
}

bool PinToolOptionsViewModel::hasContent() const
{
    return m_showColor || m_showWidth || m_showText ||
           m_showArrowStyle || m_showLineStyle || m_showShape ||
           m_showSize || m_showAutoBlur;
}

void PinToolOptionsViewModel::applySectionVisibility(bool showColor,
                                                     bool showWidth,
                                                     bool showText,
                                                     bool showArrowStyle,
                                                     bool showLineStyle,
                                                     bool showShape,
                                                     bool showSize,
                                                     bool showAutoBlur)
{
    m_showColor = showColor;
    m_showWidth = showWidth;
    m_showText = showText;
    m_showArrowStyle = showArrowStyle;
    m_showLineStyle = showLineStyle;
    m_showShape = showShape;
    m_showSize = showSize;
    m_showAutoBlur = showAutoBlur;
    emit sectionVisibilityChanged();
}

void PinToolOptionsViewModel::setShowingEmojiPicker(bool visible)
{
    if (m_showingEmojiPicker == visible) {
        return;
    }

    m_showingEmojiPicker = visible;
    emit showingEmojiPickerChanged();
}

void PinToolOptionsViewModel::clearSections()
{
    setActiveTool(ToolId::Count);
    setShowingEmojiPicker(false);
    applySectionVisibility(false, false, false, false, false, false, false, false);
}

void PinToolOptionsViewModel::showLaserPointerOptions()
{
    setActiveTool(ToolId::Selection);
    setShowingEmojiPicker(false);
    applySectionVisibility(true, true, false, false, false, false, false, false);
}

// ── Section configuration ──

void PinToolOptionsViewModel::showForTool(int toolId)
{
    if (toolId < 0 || toolId >= static_cast<int>(ToolId::Count)) {
        clearSections();
        return;
    }

    auto id = static_cast<ToolId>(toolId);

    // Special case: EmojiSticker shows emoji picker popup
    if (id == ToolId::EmojiSticker) {
        setActiveTool(id);
        setShowingEmojiPicker(false);
        applySectionVisibility(false, false, false, false,
                               false, false, false, false);
        setShowingEmojiPicker(true);
        emit emojiPickerRequested();
        return;
    }

    setActiveTool(id);
    setShowingEmojiPicker(false);

    // Apply tool section config
    auto config = ToolSectionConfig::forTool(id);
    applySectionVisibility(config.showColorSection,
                           config.showWidthSection,
                           config.showTextSection,
                           config.showArrowStyleSection,
                           config.showLineStyleSection,
                           config.showShapeSection,
                           config.showSizeSection,
                           config.showAutoBlurSection);
}

QString PinToolOptionsViewModel::autoBlurHintText() const
{
    return tr("Automatically detect and blur faces and credentials");
}

QString PinToolOptionsViewModel::arrowStyleLabel() const
{
    return AnnotationStyleLabels::arrowStyle(static_cast<LineEndStyle>(m_arrowStyle));
}

QString PinToolOptionsViewModel::lineStyleLabel() const
{
    return AnnotationStyleLabels::lineStyle(static_cast<LineStyle>(m_lineStyle));
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

QVariantList PinToolOptionsViewModel::mosaicWidthPresetOptions() const
{
    QVariantList options;
    options.reserve(std::size(kMosaicWidthPresets));
    for (const auto& preset : kMosaicWidthPresets) {
        options.append(QVariantMap{
            {"value", preset.value},
            {"previewDiameter", preset.previewDiameter},
        });
    }
    return options;
}

// ── QML action handlers ──

void PinToolOptionsViewModel::handleColorClicked(int index)
{
    if (index >= 0 && index < m_colorPalette.size()) {
        QColor color = m_colorPalette[index].value<QColor>();
        if (routeToSelection(StyleProperty::Color, color)) return;
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
    if (routeToSelection(StyleProperty::Width, qBound(minWidth(), width, maxWidth()))) return;
    setCurrentWidth(width);
    emit widthValueChanged(m_currentWidth);
}

void PinToolOptionsViewModel::handleMosaicWidthPresetSelected(int width)
{
    if (!isMosaicActive() || !isMosaicWidthPreset(width))
        return;

    handleWidthChanged(width);
}

bool PinToolOptionsViewModel::handleWidthWheelDelta(int delta)
{
    if (!m_showWidth || isMosaicActive() || delta == 0)
        return false;
    int step = delta > 0 ? 1 : -1;
    handleWidthChanged(m_currentWidth + step);
    return true;
}

bool PinToolOptionsViewModel::handleStepBadgeSizeWheelDelta(int delta)
{
    if (delta == 0)
        return false;
    const int smallest = static_cast<int>(StepBadgeSize::Small);
    const int largest = static_cast<int>(StepBadgeSize::Large);
    const int next = qBound(smallest, m_stepBadgeSize + (delta > 0 ? 1 : -1), largest);
    if (next != m_stepBadgeSize)
        handleStepBadgeSizeSelected(next);
    return true;
}

void PinToolOptionsViewModel::setActiveTool(ToolId toolId)
{
    if (m_activeToolId == toolId)
        return;

    m_activeToolId = toolId;
    emit activeToolChanged();
}

void PinToolOptionsViewModel::handleBoldToggled()
{
    if (routeToSelection(StyleProperty::Bold, !m_bold)) return;
    setBold(!m_bold);
    emit boldToggled(m_bold);
}

void PinToolOptionsViewModel::handleItalicToggled()
{
    if (routeToSelection(StyleProperty::Italic, !m_italic)) return;
    setItalic(!m_italic);
    emit italicToggled(m_italic);
}

void PinToolOptionsViewModel::handleUnderlineToggled()
{
    if (routeToSelection(StyleProperty::Underline, !m_underline)) return;
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
    if (routeToSelection(StyleProperty::Fill, mode == static_cast<int>(ShapeFillMode::Filled))) return;
    setShapeFillMode(mode);
    emit shapeFillModeSelected(mode);
}

void PinToolOptionsViewModel::handleArrowStyleSelected(int style)
{
    if (routeToSelection(StyleProperty::ArrowStyle, style)) return;
    setArrowStyle(style);
    emit arrowStyleSelected(style);
}

void PinToolOptionsViewModel::handleLineStyleSelected(int style)
{
    if (routeToSelection(StyleProperty::LineStyle, style)) return;
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

// ── Selected-object styling (#224) ──

namespace {
struct SelectionSections {
    bool color, width, text, arrowStyle, lineStyle, shape;
};
// Indexed by StyleTargetKind.
constexpr SelectionSections kSelectionSections[] = {
    {false, false, false, false, false, false},  // None
    {true, false, true, false, false, false},    // Text
    {true, true, false, true, true, false},      // Arrow
    {true, true, false, true, true, false},      // Polyline
    {true, true, false, false, false, true},     // Shape
};
} // namespace

QString PinToolOptionsViewModel::targetLabel() const
{
    if (!m_editingSelection) return tr("New object");
    QString name;
    switch (m_targetKind) {
    case StyleTargetKind::Text: name = tr("Text"); break;
    case StyleTargetKind::Arrow: name = tr("Arrow"); break;
    case StyleTargetKind::Polyline: name = tr("Line"); break;
    case StyleTargetKind::Shape:
        name = m_targetShapeType == static_cast<int>(ShapeType::Ellipse) ? tr("Ellipse") : tr("Rectangle");
        break;
    case StyleTargetKind::None: return tr("New object");
    }
    return tr("Selected: %1").arg(name);
}

void PinToolOptionsViewModel::showForSelection(const AnnotationStyleValues& values)
{
    if (values.kind == StyleTargetKind::None) {
        clearSelectionTarget();
        return;
    }
    if (!m_editingSelection) {
        m_savedDefaults = {m_currentColor, m_currentWidth, m_bold, m_italic, m_underline,
                           m_fontSize, m_fontFamily, m_arrowStyle, m_lineStyle, m_shapeType,
                           m_shapeFillMode,
                           {m_showColor, m_showWidth, m_showText, m_showArrowStyle,
                            m_showLineStyle, m_showShape, m_showSize, m_showAutoBlur},
                           m_activeToolId};
    }
    const bool changed = !m_editingSelection || m_targetKind != values.kind
        || m_targetShapeType != values.shapeType;
    const bool enteringSelection = !m_editingSelection;
    m_editingSelection = true;
    if (enteringSelection) emit activeToolChanged();   // mosaicActive changed
    m_targetKind = values.kind;
    m_targetShapeType = values.shapeType;

    if (values.color.isValid()) setCurrentColor(values.color);
    if (values.width > 0) setCurrentWidth(values.width);
    setArrowStyle(values.arrowStyle);
    setLineStyle(values.lineStyle);
    setShapeType(values.shapeType);
    setShapeFillMode(values.filled ? static_cast<int>(ShapeFillMode::Filled)
                                   : static_cast<int>(ShapeFillMode::Outline));
    if (values.kind == StyleTargetKind::Text) {
        setFontFamily(values.font.family());
        if (values.font.pointSize() > 0) setFontSize(values.font.pointSize());
        setBold(values.font.bold());
        setItalic(values.font.italic());
        setUnderline(values.font.underline());
    }
    setShowingEmojiPicker(false);
    const SelectionSections& sec = kSelectionSections[static_cast<int>(values.kind)];
    applySectionVisibility(sec.color, sec.width, sec.text, sec.arrowStyle, sec.lineStyle,
                           sec.shape, false, false);
    if (changed) emit targetChanged();
}

void PinToolOptionsViewModel::clearSelectionTarget()
{
    if (!m_editingSelection) return;
    m_editingSelection = false;
    m_targetKind = StyleTargetKind::None;
    emit activeToolChanged();   // mosaicActive changed
    const SavedDefaults& d = m_savedDefaults;
    setCurrentColor(d.color);
    setCurrentWidth(d.width);
    setBold(d.bold);
    setItalic(d.italic);
    setUnderline(d.underline);
    if (d.fontSize > 0) setFontSize(d.fontSize);
    setFontFamily(d.fontFamily);
    setArrowStyle(d.arrowStyle);
    setLineStyle(d.lineStyle);
    setShapeType(d.shapeType);
    setShapeFillMode(d.shapeFillMode);
    applySectionVisibility(d.sections[0], d.sections[1], d.sections[2], d.sections[3],
                           d.sections[4], d.sections[5], d.sections[6], d.sections[7]);
    emit targetChanged();
}

void PinToolOptionsViewModel::applyDisplayedValue(StyleProperty property, const QVariant& value)
{
    switch (property) {
    case StyleProperty::Color: setCurrentColor(value.value<QColor>()); break;
    case StyleProperty::Width: setCurrentWidth(value.toInt()); break;
    case StyleProperty::Fill:
        setShapeFillMode(value.toBool() ? static_cast<int>(ShapeFillMode::Filled)
                                        : static_cast<int>(ShapeFillMode::Outline));
        break;
    case StyleProperty::ArrowStyle: setArrowStyle(value.toInt()); break;
    case StyleProperty::LineStyle: setLineStyle(value.toInt()); break;
    case StyleProperty::FontFamily: setFontFamily(value.toString()); break;
    case StyleProperty::FontSize: setFontSize(value.toInt()); break;
    case StyleProperty::Bold: setBold(value.toBool()); break;
    case StyleProperty::Italic: setItalic(value.toBool()); break;
    case StyleProperty::Underline: setUnderline(value.toBool()); break;
    }
}

bool PinToolOptionsViewModel::routeToSelection(StyleProperty property, const QVariant& value)
{
    if (!m_editingSelection) return false;
    applyDisplayedValue(property, value);
    emit selectionStyleEdited(static_cast<int>(property), value);
    return true;
}
