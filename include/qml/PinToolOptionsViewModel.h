#pragma once

#include <QObject>
#include <QColor>
#include <QVariantList>
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

    // Section visibility
    Q_PROPERTY(bool showColorSection READ showColorSection NOTIFY sectionVisibilityChanged)
    Q_PROPERTY(bool showWidthSection READ showWidthSection NOTIFY sectionVisibilityChanged)
    Q_PROPERTY(bool showTextSection READ showTextSection NOTIFY sectionVisibilityChanged)
    Q_PROPERTY(bool showArrowStyleSection READ showArrowStyleSection NOTIFY sectionVisibilityChanged)
    Q_PROPERTY(bool showLineStyleSection READ showLineStyleSection NOTIFY sectionVisibilityChanged)
    Q_PROPERTY(bool showShapeSection READ showShapeSection NOTIFY sectionVisibilityChanged)
    Q_PROPERTY(bool showSizeSection READ showSizeSection NOTIFY sectionVisibilityChanged)
    Q_PROPERTY(bool showAutoBlurSection READ showAutoBlurSection NOTIFY sectionVisibilityChanged)

    // Color section
    Q_PROPERTY(QVariantList colorPalette READ colorPalette CONSTANT)
    Q_PROPERTY(QColor currentColor READ currentColor WRITE setCurrentColor NOTIFY currentColorChanged)

    // Width section
    Q_PROPERTY(int currentWidth READ currentWidth WRITE setCurrentWidth NOTIFY currentWidthChanged)
    Q_PROPERTY(int minWidth READ minWidth CONSTANT)
    Q_PROPERTY(int maxWidth READ maxWidth CONSTANT)

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

    // Step badge size section
    Q_PROPERTY(int stepBadgeSize READ stepBadgeSize WRITE setStepBadgeSize NOTIFY stepBadgeSizeChanged)

    // Auto-blur section
    Q_PROPERTY(bool autoBlurEnabled READ autoBlurEnabled WRITE setAutoBlurEnabled NOTIFY autoBlurEnabledChanged)
    Q_PROPERTY(bool autoBlurProcessing READ autoBlurProcessing WRITE setAutoBlurProcessing NOTIFY autoBlurProcessingChanged)

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

public:
    explicit PinToolOptionsViewModel(QObject* parent = nullptr);

    // Section visibility
    bool showColorSection() const { return m_showColor; }
    bool showWidthSection() const { return m_showWidth; }
    bool showTextSection() const { return m_showText; }
    bool showArrowStyleSection() const { return m_showArrowStyle; }
    bool showLineStyleSection() const { return m_showLineStyle; }
    bool showShapeSection() const { return m_showShape; }
    bool showSizeSection() const { return m_showSize; }
    bool showAutoBlurSection() const { return m_showAutoBlur; }
    bool hasContent() const;

    // Color
    QVariantList colorPalette() const { return m_colorPalette; }
    QColor currentColor() const { return m_currentColor; }
    void setCurrentColor(const QColor& color);

    // Width
    int currentWidth() const { return m_currentWidth; }
    void setCurrentWidth(int width);
    int minWidth() const { return 1; }
    int maxWidth() const { return 30; }

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

    // Step badge size
    int stepBadgeSize() const { return m_stepBadgeSize; }
    void setStepBadgeSize(int size);

    // Auto-blur
    bool autoBlurEnabled() const { return m_autoBlurEnabled; }
    void setAutoBlurEnabled(bool value);
    bool autoBlurProcessing() const { return m_autoBlurProcessing; }
    void setAutoBlurProcessing(bool value);

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

signals:
    void sectionVisibilityChanged();
    void currentColorChanged();
    void currentWidthChanged();
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

    // Action signals (QML → C++)
    void colorSelected(const QColor& color);
    void customColorPickerRequested();
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

public slots:
    // Called from QML
    void handleColorClicked(int index);
    void handleCustomColorClicked();
    void handleWidthChanged(int width);
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

    // Returns true if the wheel delta was consumed (width adjusted)
    bool handleWidthWheelDelta(int delta);

private:
    void buildColorPalette();

    // Section visibility
    bool m_showColor = false;
    bool m_showWidth = false;
    bool m_showText = false;
    bool m_showArrowStyle = false;
    bool m_showLineStyle = false;
    bool m_showShape = false;
    bool m_showSize = false;
    bool m_showAutoBlur = false;
    bool m_showingEmojiPicker = false;

    // Color
    QVariantList m_colorPalette;
    QColor m_currentColor{220, 53, 69}; // Default red

    // Width
    int m_currentWidth = 3;

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
