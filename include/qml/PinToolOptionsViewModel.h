#pragma once

#include "annotations/AnnotationStyle.h"
#include <QObject>
#include <QColor>
#include <QSize>
#include <QVariantList>
#include <QStringList>
#include <vector>
#include "tools/ToolId.h"

/**
 * @brief ViewModel exposing PinWindow sub-toolbar state to QML.
 *
 * Manages section visibility based on active tool (via ToolSectionConfig),
 * tracks current color/width/formatting state, and forwards QML interactions
 * back to PinWindow via signals.
 *
 * Popup delegation: EmojiPicker, font dropdowns, and color picker dialog
 * remain as QWidget popups triggered by C++ bridge code.
 */
class PinToolOptionsViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool markerTextSnapAvailable READ markerTextSnapAvailable NOTIFY markerTextSnapChanged)
    Q_PROPERTY(bool markerTextSnapEnabled READ markerTextSnapEnabled WRITE setMarkerTextSnapEnabled NOTIFY markerTextSnapEnabledChanged)
    Q_PROPERTY(bool arrowBindingAvailable READ arrowBindingAvailable NOTIFY targetChanged)
    Q_PROPERTY(bool arrowBindingEnabled READ arrowBindingEnabled WRITE setArrowBindingEnabled NOTIFY arrowBindingEnabledChanged)
    // Section visibility
    Q_PROPERTY(bool showColorSection READ showColorSection NOTIFY sectionVisibilityChanged)
    Q_PROPERTY(bool showWidthSection READ showWidthSection NOTIFY sectionVisibilityChanged)
    Q_PROPERTY(bool showTextSection READ showTextSection NOTIFY sectionVisibilityChanged)
    Q_PROPERTY(bool showArrowStyleSection READ showArrowStyleSection NOTIFY sectionVisibilityChanged)
    Q_PROPERTY(bool showLineStyleSection READ showLineStyleSection NOTIFY sectionVisibilityChanged)
    Q_PROPERTY(bool showShapeSection READ showShapeSection NOTIFY sectionVisibilityChanged)
    Q_PROPERTY(bool showSizeSection READ showSizeSection NOTIFY sectionVisibilityChanged)
    Q_PROPERTY(bool showAutoBlurSection READ showAutoBlurSection NOTIFY sectionVisibilityChanged)
    Q_PROPERTY(bool showCropSection READ showCropSection NOTIFY sectionVisibilityChanged)

    // Cut-out section (#371): remove or insert, band direction, sizes.
    Q_PROPERTY(bool showCutOutSection READ showCutOutSection NOTIFY sectionVisibilityChanged)
    Q_PROPERTY(QVariantList cutOutOperationOptions READ cutOutOperationOptions CONSTANT)
    Q_PROPERTY(QVariantList cutOutAxisOptions READ cutOutAxisOptions CONSTANT)
    Q_PROPERTY(int cutOutOperation READ cutOutOperation NOTIFY cutOutStateChanged)
    Q_PROPERTY(int cutOutAxis READ cutOutAxis NOTIFY cutOutStateChanged)
    Q_PROPERTY(bool cutOutHasDraft READ cutOutHasDraft NOTIFY cutOutDraftChanged)
    Q_PROPERTY(QString cutOutSizeText READ cutOutSizeText NOTIFY cutOutDraftChanged)
    // Shown instead of the size while no strip can be exact (odd zoom).
    Q_PROPERTY(QString cutOutHintText READ cutOutHintText NOTIFY cutOutDraftChanged)

    // Measure section (#348): manual line or image-based auto spans.
    Q_PROPERTY(bool showMeasureSection READ showMeasureSection NOTIFY sectionVisibilityChanged)
    Q_PROPERTY(QVariantList measureModeOptions READ measureModeOptions CONSTANT)
    Q_PROPERTY(int measureMode READ measureMode NOTIFY measureModeChanged)
    Q_PROPERTY(bool measureAutoMode READ measureAutoMode NOTIFY measureModeChanged)

    // Crop section (#358): aspect preset, draft size and apply / cancel.
    Q_PROPERTY(QVariantList cropAspectOptions READ cropAspectOptions CONSTANT)
    Q_PROPERTY(int cropAspect READ cropAspect NOTIFY cropAspectChanged)
    Q_PROPERTY(bool cropHasDraft READ cropHasDraft NOTIFY cropDraftChanged)
    Q_PROPERTY(QString cropSizeText READ cropSizeText NOTIFY cropDraftChanged)

    // Color section
    Q_PROPERTY(QVariantList colorPalette READ colorPalette CONSTANT)
    // Same entries as colorPalette, each as {color, name}, so a swatch's
    // screen reader name travels with its color (#248).
    Q_PROPERTY(QVariantList colorPaletteOptions READ colorPaletteOptions CONSTANT)
    Q_PROPERTY(QColor currentColor READ currentColor WRITE setCurrentColor NOTIFY currentColorChanged)

    // Width section
    Q_PROPERTY(int currentWidth READ currentWidth WRITE setCurrentWidth NOTIFY currentWidthChanged)
    Q_PROPERTY(int minWidth READ minWidth CONSTANT)
    Q_PROPERTY(int maxWidth READ maxWidth CONSTANT)
    Q_PROPERTY(int activeToolId READ activeToolId NOTIFY activeToolChanged)
    Q_PROPERTY(bool markerActive READ isMarkerActive NOTIFY activeToolChanged)
    Q_PROPERTY(bool mosaicActive READ isMosaicActive NOTIFY activeToolChanged)
    Q_PROPERTY(QVariantList mosaicWidthPresetOptions READ mosaicWidthPresetOptions CONSTANT)

    // Text formatting section
    Q_PROPERTY(bool isBold READ isBold WRITE setBold NOTIFY boldChanged)
    Q_PROPERTY(bool isItalic READ isItalic WRITE setItalic NOTIFY italicChanged)
    Q_PROPERTY(bool isUnderline READ isUnderline WRITE setUnderline NOTIFY underlineChanged)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY fontSizeChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY fontFamilyChanged)

    // Shape section
    Q_PROPERTY(int shapeType READ shapeType WRITE setShapeType NOTIFY shapeTypeChanged)
    Q_PROPERTY(int shapeFillMode READ shapeFillMode WRITE setShapeFillMode NOTIFY shapeFillModeChanged)

    // Arrow/Line style section
    Q_PROPERTY(int arrowStyle READ arrowStyle WRITE setArrowStyle NOTIFY arrowStyleChanged)
    Q_PROPERTY(int lineStyle READ lineStyle WRITE setLineStyle NOTIFY lineStyleChanged)
    // Translated names of the current styles, for screen readers (#248).
    Q_PROPERTY(QString arrowStyleLabel READ arrowStyleLabel NOTIFY arrowStyleChanged)
    Q_PROPERTY(QString lineStyleLabel READ lineStyleLabel NOTIFY lineStyleChanged)

    // Step badge size section
    Q_PROPERTY(int stepBadgeSize READ stepBadgeSize WRITE setStepBadgeSize NOTIFY stepBadgeSizeChanged)

    // Auto-blur section
    Q_PROPERTY(bool autoBlurEnabled READ autoBlurEnabled WRITE setAutoBlurEnabled NOTIFY autoBlurEnabledChanged)
    Q_PROPERTY(bool autoBlurProcessing READ autoBlurProcessing WRITE setAutoBlurProcessing NOTIFY autoBlurProcessingChanged)
    Q_PROPERTY(QString autoBlurHintText READ autoBlurHintText CONSTANT)

    // Arrow/line style option lists for QML Repeater
    Q_PROPERTY(QVariantList arrowStyleOptions READ arrowStyleOptions CONSTANT)
    Q_PROPERTY(QVariantList lineStyleOptions READ lineStyleOptions CONSTANT)
    Q_PROPERTY(QVariantList shapeOptions READ shapeOptions CONSTANT)
    Q_PROPERTY(QVariantList shapeFillOptions READ shapeFillOptions CONSTANT)
    Q_PROPERTY(QVariantList stepBadgeSizeOptions READ stepBadgeSizeOptions CONSTANT)

    // Emoji picker state
    Q_PROPERTY(bool showingEmojiPicker READ showingEmojiPicker NOTIFY showingEmojiPickerChanged)

    // Whether any section has content to show
    Q_PROPERTY(bool hasContent READ hasContent NOTIFY sectionVisibilityChanged)

    // Selected-object styling (#224)
    Q_PROPERTY(QString targetLabel READ targetLabel NOTIFY targetChanged)
    Q_PROPERTY(bool editingSelection READ editingSelection NOTIFY targetChanged)
    Q_PROPERTY(bool shapeTypeEditable READ shapeTypeEditable NOTIFY targetChanged)
    // Several annotations are targeted at once (#370).
    Q_PROPERTY(bool multiSelection READ multiSelection NOTIFY targetChanged)
    // Drawing order of the selected annotation (#268).
    Q_PROPERTY(QVariantList zOrderOptions READ zOrderOptions CONSTANT)
    Q_PROPERTY(bool canRaiseSelection READ canRaiseSelection NOTIFY zOrderAvailabilityChanged)
    Q_PROPERTY(bool canLowerSelection READ canLowerSelection NOTIFY zOrderAvailabilityChanged)

public:
    explicit PinToolOptionsViewModel(QObject* parent = nullptr);
    bool arrowBindingAvailable() const { return !m_editingSelection || m_targetKind == StyleTargetKind::Arrow; }
    bool arrowBindingEnabled() const;
    void setArrowBindingEnabled(bool enabled);

    // Section visibility
    bool showColorSection() const { return m_showColor; }
    bool showWidthSection() const { return m_showWidth; }
    bool showTextSection() const { return m_showText; }
    bool showArrowStyleSection() const { return m_showArrowStyle; }
    bool showLineStyleSection() const { return m_showLineStyle; }
    bool showShapeSection() const { return m_showShape; }
    bool showSizeSection() const { return m_showSize; }
    bool showAutoBlurSection() const { return m_showAutoBlur; }
    bool showCropSection() const { return m_showCrop; }
    bool hasContent() const;

    // Cut-out state, pushed by the host that owns the tool. Operation is an
    // ImageStrip::Operation; axis an ImageStrip::Axis as seen on screen.
    bool showCutOutSection() const { return m_showCutOut; }
    QVariantList cutOutOperationOptions() const;
    QVariantList cutOutAxisOptions() const;
    int cutOutOperation() const { return m_cutOutOperation; }
    int cutOutAxis() const { return m_cutOutAxis; }
    void setCutOutState(int operation, int axis);
    bool cutOutHasDraft() const { return m_cutOutHasDraft; }
    QString cutOutSizeText() const { return m_cutOutSizeText; }
    // `length` in source pixels; `resultSize` is the image after applying.
    void setCutOutDraft(bool hasDraft, int length, const QSize& resultSize);
    QString cutOutHintText() const;
    // `zoomed`: the pin is not at 100%, so resetting the zoom may help.
    void setCutOutAvailable(bool available, bool zoomed);

    // Measure mode, pushed by the host that owns the tool.
    enum MeasureMode { MeasureManual = 0, MeasureAuto = 1 };
    bool showMeasureSection() const { return m_showMeasure; }
    QVariantList measureModeOptions() const;
    int measureMode() const { return m_measureMode; }
    bool measureAutoMode() const { return m_measureMode == MeasureAuto; }
    void setMeasureMode(int mode);

    // Crop draft state, pushed by the host that owns the crop tool.
    QVariantList cropAspectOptions() const;
    int cropAspect() const { return m_cropAspect; }
    void setCropAspect(int aspect);
    bool cropHasDraft() const { return m_cropHasDraft; }
    QString cropSizeText() const { return m_cropSizeText; }
    void setCropDraft(bool hasDraft, const QSize& pixelSize);

    bool markerTextSnapAvailable() const { return m_markerTextSnapAvailable; }
    bool markerTextSnapEnabled() const { return m_markerTextSnapEnabled; }
    void setMarkerTextSnapAvailable(bool value) {
        if (m_markerTextSnapAvailable == value) return;
        m_markerTextSnapAvailable = value;
        emit markerTextSnapChanged();
    }
    void setMarkerTextSnapEnabled(bool value) {
        if (m_markerTextSnapEnabled == value) return;
        m_markerTextSnapEnabled = value;
        emit markerTextSnapEnabledChanged();
    }

    // Color
    QVariantList colorPalette() const { return m_colorPalette; }
    QVariantList colorPaletteOptions() const;
    QColor currentColor() const { return m_currentColor; }
    void setCurrentColor(const QColor& color);

    // Width
    int currentWidth() const { return m_currentWidth; }
    void setCurrentWidth(int width);
    int minWidth() const { return 1; }
    int maxWidth() const { return 30; }
    int activeToolId() const { return static_cast<int>(m_activeToolId); }
    // A targeted selection uses the plain width slider, not the brush presets.
    bool isMarkerActive() const { return m_activeToolId == ToolId::Marker; }
    bool isMosaicActive() const { return m_activeToolId == ToolId::Mosaic && !m_editingSelection; }
    QVariantList mosaicWidthPresetOptions() const;

    // Text formatting
    bool isBold() const { return m_bold; }
    void setBold(bool value);
    bool isItalic() const { return m_italic; }
    void setItalic(bool value);
    bool isUnderline() const { return m_underline; }
    void setUnderline(bool value);
    int fontSize() const { return m_fontSize; }
    void setFontSize(int size);
    QString fontFamily() const { return m_fontFamily; }
    void setFontFamily(const QString& family);

    // Shape
    int shapeType() const { return m_shapeType; }
    void setShapeType(int type);
    int shapeFillMode() const { return m_shapeFillMode; }
    void setShapeFillMode(int mode);

    // Arrow/Line style
    int arrowStyle() const { return m_arrowStyle; }
    void setArrowStyle(int style);
    int lineStyle() const { return m_lineStyle; }
    void setLineStyle(int style);
    QString arrowStyleLabel() const;
    QString lineStyleLabel() const;

    // Step badge size
    int stepBadgeSize() const { return m_stepBadgeSize; }
    void setStepBadgeSize(int size);

    // Auto-blur
    bool autoBlurEnabled() const { return m_autoBlurEnabled; }
    void setAutoBlurEnabled(bool value);
    bool autoBlurProcessing() const { return m_autoBlurProcessing; }
    void setAutoBlurProcessing(bool value);
    QString autoBlurHintText() const;

    // Option lists
    QVariantList arrowStyleOptions() const;
    QVariantList lineStyleOptions() const;
    QVariantList shapeOptions() const;
    QVariantList shapeFillOptions() const;
    QVariantList stepBadgeSizeOptions() const;

    // Emoji picker
    bool showingEmojiPicker() const { return m_showingEmojiPicker; }

    /**
     * @brief Configure sections for the given tool.
     * Uses ToolSectionConfig to determine which sections to show.
     * Special cases: EmojiSticker shows emoji picker, Eraser shows nothing.
     */
    void showForTool(int toolId);

    // ── Selected-object styling (#224) ──
    // "Selected: Arrow" while a selection is targeted, otherwise "New object".
    QString targetLabel() const;
    bool editingSelection() const { return m_editingSelection; }
    bool shapeTypeEditable() const { return !m_editingSelection; }
    // Shows the sections and current values of a selected annotation. While
    // targeted, strip edits emit selectionStyleEdited instead of the default
    // signals, and the tool defaults are kept aside untouched.
    void showForSelection(const AnnotationStyleValues& values);
    // Targets several annotations at once (#370). `targets` holds the style of
    // each stylable one; only sections every one of them has are shown, with
    // the first one's values, and properties whose values differ are named as
    // mixed in the target label. `selectedCount` counts every selected
    // annotation, stylable or not.
    void showForMultiSelection(const std::vector<AnnotationStyleValues>& targets, int selectedCount);
    // Strip sections a selected annotation kind offers.
    struct SelectionSections {
        bool color, width, text, arrowStyle, lineStyle, shape;
    };
    bool multiSelection() const { return m_multiTargetCount > 0; }
    QStringList mixedProperties() const { return m_mixedProperties; }
    // Returns to the tool defaults captured when targeting began.
    void clearSelectionTarget();
    // For host-side dropdown/dialog results: emits selectionStyleEdited and
    // returns true when a selection is targeted; otherwise does nothing.
    bool routeToSelection(StyleProperty property, const QVariant& value);
    void clearSections();
    void showLaserPointerOptions();

    // Arrange buttons (#268): one entry per move with its icon, name and shortcut.
    QVariantList zOrderOptions() const;
    bool canRaiseSelection() const { return m_canRaiseSelection; }
    bool canLowerSelection() const { return m_canLowerSelection; }
    void setZOrderAvailability(bool canRaise, bool canLower);

signals:
    void arrowBindingEnabledChanged();
    void markerTextSnapChanged();
    void markerTextSnapEnabledChanged();
    void sectionVisibilityChanged();
    void currentColorChanged();
    void currentWidthChanged();
    void activeToolChanged();
    void boldChanged();
    void italicChanged();
    void underlineChanged();
    void fontSizeChanged();
    void fontFamilyChanged();
    void shapeTypeChanged();
    void shapeFillModeChanged();
    void arrowStyleChanged();
    void lineStyleChanged();
    void stepBadgeSizeChanged();
    void autoBlurEnabledChanged();
    void autoBlurProcessingChanged();
    void showingEmojiPickerChanged();
    void targetChanged();
    void selectionStyleEdited(int property, const QVariant& value);
    void zOrderAvailabilityChanged();
    void zOrderRequested(int move);   // AnnotationZOrderMove

    // Action signals (QML → C++)
    void colorSelected(const QColor& color);
    void customColorPickerRequested();
    // Eyedropper (#271): the host samples a color from its own image.
    void colorPickRequested();
    void widthValueChanged(int width);
    void boldToggled(bool enabled);
    void italicToggled(bool enabled);
    void underlineToggled(bool enabled);
    void fontSizeDropdownRequested(double globalX, double globalY);
    void fontFamilyDropdownRequested(double globalX, double globalY);
    void arrowStyleDropdownRequested(double globalX, double globalY);
    void lineStyleDropdownRequested(double globalX, double globalY);
    void shapeTypeSelected(int type);
    void shapeFillModeSelected(int mode);
    void arrowStyleSelected(int style);
    void lineStyleSelected(int style);
    void stepBadgeSizeSelected(int size);
    void autoBlurRequested();
    void emojiPickerRequested();
    void cropAspectChanged();
    void cropDraftChanged();
    void cropAspectSelected(int aspect);   // CropDraftModel::Aspect
    void cropApplyRequested();
    void cropCancelRequested();
    void cutOutStateChanged();
    void cutOutDraftChanged();
    void cutOutOperationSelected(int operation);
    void cutOutAxisSelected(int axis);
    void cutOutApplyRequested();
    void cutOutCancelRequested();
    void measureModeChanged();
    void measureModeSelected(int mode);   // MeasureMode

public slots:
    // Called from QML
    void handleColorClicked(int index);
    void handleCustomColorClicked();
    void handleColorPickClicked();
    void handleWidthChanged(int width);
    void handleMosaicWidthPresetSelected(int width);
    void handleBoldToggled();
    void handleItalicToggled();
    void handleUnderlineToggled();
    void handleFontSizeDropdown(double globalX, double globalY);
    void handleFontFamilyDropdown(double globalX, double globalY);
    void handleArrowStyleDropdown(double globalX, double globalY);
    void handleLineStyleDropdown(double globalX, double globalY);
    void handleShapeTypeSelected(int type);
    void handleShapeFillModeSelected(int mode);
    void handleArrowStyleSelected(int style);
    void handleLineStyleSelected(int style);
    void handleStepBadgeSizeSelected(int size);
    void handleAutoBlurClicked();
    void handleEmojiPickerRequested();
    void handleCropAspectSelected(int aspect);
    void handleCropApply();
    void handleCropCancel();
    void handleCutOutOperationSelected(int operation);
    void handleCutOutAxisSelected(int axis);
    void handleCutOutApply();
    void handleCutOutCancel();
    void handleMeasureModeSelected(int mode);
    void handleZOrderRequested(int move);

    // Returns true if the wheel delta was consumed (width adjusted)
    bool handleWidthWheelDelta(int delta);
    // One size per wheel notch, stopping at Small and Large (no wrap-around).
    bool handleStepBadgeSizeWheelDelta(int delta);

private:
    bool m_markerTextSnapAvailable = false;
    bool m_markerTextSnapEnabled = false;
    void buildColorPalette();
    void applySectionVisibility(bool showColor,
                                bool showWidth,
                                bool showText,
                                bool showArrowStyle,
                                bool showLineStyle,
                                bool showShape,
                                bool showSize,
                                bool showAutoBlur);
    void setShowingEmojiPicker(bool visible);
    void setActiveTool(ToolId toolId);

    // Section visibility
    bool m_showColor = false;
    bool m_showWidth = false;
    bool m_showText = false;
    bool m_showArrowStyle = false;
    bool m_showLineStyle = false;
    bool m_showShape = false;
    bool m_showSize = false;
    bool m_showAutoBlur = false;
    bool m_showCrop = false;
    bool m_showingEmojiPicker = false;

    // Cut-out
    bool m_showCutOut = false;
    int m_cutOutOperation = 0;
    int m_cutOutAxis = 0;
    bool m_cutOutHasDraft = false;
    QString m_cutOutSizeText;
    bool m_cutOutAvailable = true;
    bool m_cutOutZoomed = false;

    // Measure
    bool m_showMeasure = false;
    int m_measureMode = MeasureManual;

    // Crop
    int m_cropAspect = 0;
    bool m_cropHasDraft = false;
    QString m_cropSizeText;

    // Selected-object styling
    struct SavedDefaults {
        QColor color;
        int width = 0;
        bool bold = false, italic = false, underline = false;
        int fontSize = 0;
        QString fontFamily;
        int arrowStyle = 0, lineStyle = 0, shapeType = 0, shapeFillMode = 0;
        bool sections[8] = {};
        ToolId activeTool = ToolId::Count;
    };
    void applyDisplayedValue(StyleProperty property, const QVariant& value);
    bool m_editingSelection = false;
    bool m_canRaiseSelection = false;
    bool m_canLowerSelection = false;
    StyleTargetKind m_targetKind = StyleTargetKind::None;
    int m_targetShapeType = 0;
    int m_multiTargetCount = 0;
    QStringList m_mixedProperties;
    void showSelectionValues(const AnnotationStyleValues& values, const SelectionSections& sections,
                             bool targetChangedAnyway);
    SavedDefaults m_savedDefaults;

    // Color
    QVariantList m_colorPalette;
    QStringList m_colorPaletteNames; // untranslated, same order as m_colorPalette
    QColor m_currentColor{0xFF, 0x3B, 0x30}; // Default red (DesignSystem::annotationRed)

    // Width
    int m_currentWidth = 3;
    ToolId m_activeToolId = ToolId::Count;

    // Text formatting
    bool m_bold = false;
    bool m_italic = false;
    bool m_underline = false;
    int m_fontSize = 16;
    QString m_fontFamily;

    // Shape
    int m_shapeType = 0;    // ShapeType::Rectangle
    int m_shapeFillMode = 0; // ShapeFillMode::Outline

    // Arrow/Line style
    int m_arrowStyle = 1;   // LineEndStyle::EndArrow
    int m_lineStyle = 0;    // LineStyle::Solid

    // Step badge size
    int m_stepBadgeSize = 1; // StepBadgeSize::Medium

    // Auto-blur
    bool m_autoBlurEnabled = false;
    bool m_autoBlurProcessing = false;
};
