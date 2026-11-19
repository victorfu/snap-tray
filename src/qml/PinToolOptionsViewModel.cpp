#include "qml/PinToolOptionsViewModel.h"

#include "annotations/AnnotationZOrderCommands.h"
#include "annotations/AnnotationStyleLabels.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/LineStyle.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/StepBadgeAnnotation.h"
#include "pinwindow/ImageStripTransform.h"
#include "tools/CropDraftModel.h"
#include "tools/ToolSectionConfig.h"
#include "tools/ToolWidthSlot.h"
#include "ui/DesignSystem.h"
#include "settings/AnnotationSettingsManager.h"

#include <QCoreApplication>
#include <QLocale>

#include <algorithm>
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
    connect(&AnnotationSettingsManager::instance(), &AnnotationSettingsManager::arrowBindingChanged,
            this, &PinToolOptionsViewModel::arrowBindingEnabledChanged);
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
           m_showSize || m_showAutoBlur || m_showCrop || m_showCutOut;
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
    // Only showForTool() turns the crop section on.
    const bool hadCrop = m_showCrop || m_showCutOut;
    m_showCrop = false;
    m_showCutOut = false;
    emit sectionVisibilityChanged();
    if (hadCrop) emit targetChanged();   // the strip label said "Crop"
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
    if (config.showCropSection || config.showCutOutSection) {
        m_showCrop = config.showCropSection;
        m_showCutOut = config.showCutOutSection;
        emit sectionVisibilityChanged();
        emit targetChanged();
    }
}

QVariantList PinToolOptionsViewModel::cutOutOperationOptions() const
{
    return {
        QVariantMap{{"value", static_cast<int>(ImageStrip::Operation::Remove)},
                    {"label", tr("Remove")},
                    {"name", tr("Remove the strip and join the rest")}},
        QVariantMap{{"value", static_cast<int>(ImageStrip::Operation::Insert)},
                    {"label", tr("Insert")},
                    {"name", tr("Insert a transparent strip")}},
    };
}

QVariantList PinToolOptionsViewModel::cutOutAxisOptions() const
{
    return {
        QVariantMap{{"value", static_cast<int>(ImageStrip::Axis::Rows)},
                    {"label", tr("Horizontal")},
                    {"name", tr("Horizontal strip")}},
        QVariantMap{{"value", static_cast<int>(ImageStrip::Axis::Columns)},
                    {"label", tr("Vertical")},
                    {"name", tr("Vertical strip")}},
    };
}

void PinToolOptionsViewModel::setCutOutState(int operation, int axis)
{
    if (m_cutOutOperation == operation && m_cutOutAxis == axis) {
        return;
    }
    m_cutOutOperation = operation;
    m_cutOutAxis = axis;
    emit cutOutStateChanged();
}

void PinToolOptionsViewModel::setCutOutDraft(bool hasDraft, int length, const QSize& resultSize)
{
    // "-120 px > 800 x 480 px": what changes, then the image that results.
    const bool remove = m_cutOutOperation == static_cast<int>(ImageStrip::Operation::Remove);
    const QString sizeText = hasDraft
        ? QStringLiteral("%1%2 px \u2192 %3 \u00D7 %4 px")
              .arg(remove ? QStringLiteral("\u2212") : QStringLiteral("+"))
              .arg(length)
              .arg(resultSize.width())
              .arg(resultSize.height())
        : QString();
    if (m_cutOutHasDraft == hasDraft && m_cutOutSizeText == sizeText) {
        return;
    }
    m_cutOutHasDraft = hasDraft;
    m_cutOutSizeText = sizeText;
    emit cutOutDraftChanged();
}

QString PinToolOptionsViewModel::cutOutHintText() const
{
    if (m_cutOutAvailable) {
        return QString();
    }
    return m_cutOutZoomed
        ? tr("Reset zoom to 100% to cut out a strip")
        : QCoreApplication::translate("PinWindow", "This strip cannot be applied");
}

void PinToolOptionsViewModel::setCutOutAvailable(bool available, bool zoomed)
{
    if (m_cutOutAvailable == available && m_cutOutZoomed == zoomed) {
        return;
    }
    m_cutOutAvailable = available;
    m_cutOutZoomed = zoomed;
    emit cutOutDraftChanged();
}

void PinToolOptionsViewModel::handleCutOutOperationSelected(int operation)
{
    if (operation == static_cast<int>(ImageStrip::Operation::Remove)
        || operation == static_cast<int>(ImageStrip::Operation::Insert)) {
        emit cutOutOperationSelected(operation);
    }
}

void PinToolOptionsViewModel::handleCutOutAxisSelected(int axis)
{
    if (axis == static_cast<int>(ImageStrip::Axis::Rows)
        || axis == static_cast<int>(ImageStrip::Axis::Columns)) {
        emit cutOutAxisSelected(axis);
    }
}

void PinToolOptionsViewModel::handleCutOutApply()
{
    emit cutOutApplyRequested();
}

void PinToolOptionsViewModel::handleCutOutCancel()
{
    emit cutOutCancelRequested();
}

QVariantList PinToolOptionsViewModel::cropAspectOptions() const
{
    QVariantList options;
    for (const CropDraftModel::AspectOption& option : CropDraftModel::aspectOptions()) {
        QString label;
        QString name;
        if (option.aspect == CropDraftModel::Aspect::Free) {
            label = tr("Free");
            name = tr("Free aspect ratio");
        } else if (option.aspect == CropDraftModel::Aspect::Original) {
            label = tr("Original");
            name = tr("Original aspect ratio");
        } else {
            label = QStringLiteral("%1:%2").arg(option.widthUnits).arg(option.heightUnits);
            name = tr("Aspect ratio %1").arg(label);
        }
        options.append(QVariantMap{
            {"value", static_cast<int>(option.aspect)}, {"label", label}, {"name", name}});
    }
    return options;
}

void PinToolOptionsViewModel::setCropAspect(int aspect)
{
    if (m_cropAspect == aspect || !CropDraftModel::isValidAspect(aspect)) {
        return;
    }
    m_cropAspect = aspect;
    emit cropAspectChanged();
}

void PinToolOptionsViewModel::setCropDraft(bool hasDraft, const QSize& pixelSize)
{
    const QString sizeText = hasDraft
        ? QStringLiteral("%1 \u00D7 %2 px").arg(pixelSize.width()).arg(pixelSize.height())
        : QString();
    if (m_cropHasDraft == hasDraft && m_cropSizeText == sizeText) {
        return;
    }
    m_cropHasDraft = hasDraft;
    m_cropSizeText = sizeText;
    emit cropDraftChanged();
}

void PinToolOptionsViewModel::handleCropAspectSelected(int aspect)
{
    if (CropDraftModel::isValidAspect(aspect)) {
        emit cropAspectSelected(aspect);
    }
}

void PinToolOptionsViewModel::handleCropApply()
{
    emit cropApplyRequested();
}

void PinToolOptionsViewModel::handleCropCancel()
{
    emit cropCancelRequested();
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

QVariantList PinToolOptionsViewModel::zOrderOptions() const
{
    using Move = AnnotationZOrderCommands::Move;
    struct Option {
        Move move;
        const char* iconKey;
        bool raises;
    };
    static constexpr Option kOptions[] = {
        {Move::ToBack, "send-to-back", false},
        {Move::Backward, "send-backward", false},
        {Move::Forward, "bring-forward", true},
        {Move::ToFront, "bring-to-front", true},
    };
    QVariantList options;
    for (const auto& option : kOptions) {
        options.append(QVariantMap{
            {"value", static_cast<int>(option.move)},
            {"iconKey", QString::fromLatin1(option.iconKey)},
            {"raises", option.raises},
            {"label", AnnotationZOrderCommands::actionText(option.move)},
            {"shortcut", AnnotationZOrderCommands::shortcut(option.move)
                             .toString(QKeySequence::NativeText)},
        });
    }
    return options;
}

void PinToolOptionsViewModel::setZOrderAvailability(bool canRaise, bool canLower)
{
    if (m_canRaiseSelection == canRaise && m_canLowerSelection == canLower) return;
    m_canRaiseSelection = canRaise;
    m_canLowerSelection = canLower;
    emit zOrderAvailabilityChanged();
}

void PinToolOptionsViewModel::handleZOrderRequested(int move)
{
    if (m_editingSelection) emit zOrderRequested(move);
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

void PinToolOptionsViewModel::handleColorPickClicked()
{
    emit colorPickRequested();
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
using SelectionSections = PinToolOptionsViewModel::SelectionSections;
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
    if (m_showCrop) return tr("Crop");
    if (m_showCutOut) return tr("Cut Out");
    if (!m_editingSelection) return tr("New object");
    if (m_multiTargetCount > 0) {
        if (m_mixedProperties.isEmpty()) return tr("Selected objects: %1").arg(m_multiTargetCount);
        // Settings whose values differ are named here; the controls show the
        // first selected annotation's value (#370).
        return tr("Selected objects: %1 · Mixed: %2")
            .arg(m_multiTargetCount)
            .arg(QLocale().createSeparatedList(m_mixedProperties));
    }
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
    const bool wasMulti = m_multiTargetCount > 0;
    m_multiTargetCount = 0;
    m_mixedProperties.clear();
    const SelectionSections& sec = kSelectionSections[static_cast<int>(values.kind)];
    showSelectionValues(values, sec, wasMulti);
}

void PinToolOptionsViewModel::showForMultiSelection(
    const std::vector<AnnotationStyleValues>& targets, int selectedCount)
{
    if (targets.empty() || selectedCount < 2) {
        clearSelectionTarget();
        return;
    }
    SelectionSections sections{true, true, true, true, true, true};
    bool sameKind = true;
    for (const auto& target : targets) {
        const SelectionSections& own = kSelectionSections[static_cast<int>(target.kind)];
        sections.color = sections.color && own.color;
        sections.width = sections.width && own.width;
        sections.text = sections.text && own.text;
        sections.arrowStyle = sections.arrowStyle && own.arrowStyle;
        sections.lineStyle = sections.lineStyle && own.lineStyle;
        sections.shape = sections.shape && own.shape;
        sameKind = sameKind && target.kind == targets.front().kind;
    }
    // A property is mixed when the shown section's targets disagree on it.
    const AnnotationStyleValues& first = targets.front();
    auto differs = [&targets](auto value) {
        return std::any_of(targets.cbegin(), targets.cend(),
            [&](const AnnotationStyleValues& t) { return value(t) != value(targets.front()); });
    };
    QStringList mixed;
    if (sections.color && differs([](const auto& t) { return t.color.rgba(); })) mixed << tr("Color");
    if (sections.width && differs([](const auto& t) { return t.width; })) mixed << tr("Width");
    if (sections.arrowStyle && differs([](const auto& t) { return t.arrowStyle; })) mixed << tr("Line ends");
    if (sections.lineStyle && differs([](const auto& t) { return t.lineStyle; })) mixed << tr("Line style");
    if (sections.shape && differs([](const auto& t) { return t.filled; })) mixed << tr("Fill");
    if (sections.text && differs([](const auto& t) { return t.font; })) mixed << tr("Font");

    const bool changed = m_multiTargetCount != selectedCount || m_mixedProperties != mixed;
    m_multiTargetCount = selectedCount;
    m_mixedProperties = mixed;
    AnnotationStyleValues shown = first;
    if (!sameKind) shown.kind = StyleTargetKind::None;   // no kind-specific options
    showSelectionValues(shown, sections, changed);
}

void PinToolOptionsViewModel::showSelectionValues(const AnnotationStyleValues& values,
                                                  const SelectionSections& sections,
                                                  bool targetChangedAnyway)
{
    if (!m_editingSelection) {
        m_savedDefaults = {m_currentColor, m_currentWidth, m_bold, m_italic, m_underline,
                           m_fontSize, m_fontFamily, m_arrowStyle, m_lineStyle, m_shapeType,
                           m_shapeFillMode,
                           {m_showColor, m_showWidth, m_showText, m_showArrowStyle,
                            m_showLineStyle, m_showShape, m_showSize, m_showAutoBlur},
                           m_activeToolId};
    }
    const bool changed = targetChangedAnyway || !m_editingSelection
        || m_targetKind != values.kind || m_targetShapeType != values.shapeType;
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
    if (sections.text) {
        setFontFamily(values.font.family());
        if (values.font.pointSize() > 0) setFontSize(values.font.pointSize());
        setBold(values.font.bold());
        setItalic(values.font.italic());
        setUnderline(values.font.underline());
    }
    setShowingEmojiPicker(false);
    applySectionVisibility(sections.color, sections.width, sections.text, sections.arrowStyle,
                           sections.lineStyle, sections.shape, false, false);
    if (changed) emit targetChanged();
}

void PinToolOptionsViewModel::clearSelectionTarget()
{
    if (!m_editingSelection) return;
    m_editingSelection = false;
    m_targetKind = StyleTargetKind::None;
    m_multiTargetCount = 0;
    m_mixedProperties.clear();
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


bool PinToolOptionsViewModel::arrowBindingEnabled() const
{
    return AnnotationSettingsManager::instance().loadArrowBinding();
}
void PinToolOptionsViewModel::setArrowBindingEnabled(bool enabled)
{
    if (arrowBindingEnabled() == enabled) return;
    AnnotationSettingsManager::instance().saveArrowBinding(enabled);
}
