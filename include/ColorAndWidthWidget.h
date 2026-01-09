#ifndef COLORANDWIDTHWIDGET_H
#define COLORANDWIDTHWIDGET_H

#include <QObject>
#include <QVector>
#include <QColor>
#include <QRect>
#include <QPoint>
#include <QString>
#include "annotations/ShapeAnnotation.h"  // For ShapeType and ShapeFillMode enums
#include "annotations/ArrowAnnotation.h"  // For LineEndStyle enum
#include "annotations/LineStyle.h"        // For LineStyle enum
#include "annotations/StepBadgeAnnotation.h"  // For StepBadgeSize enum
#include "ToolbarStyle.h"

class QPainter;
class ColorSection;
class WidthSection;
class MosaicWidthSection;
class TextSection;
class ArrowStyleSection;
class LineStyleSection;
class ShapeSection;
class SizeSection;
class AutoBlurSection;

/**
 * @brief Unified color palette and line width picker component.
 *
 * This class acts as a coordinator/facade for multiple section components:
 * - ColorSection: Color palette with swatches
 * - WidthSection: Line width preview and adjustment
 * - TextSection: Text formatting (Bold/Italic/Underline, font size/family)
 * - ArrowStyleSection: Arrow line end style selection
 * - ShapeSection: Shape type and fill mode selection
 *
 * The public API remains unchanged for backward compatibility.
 */
class ColorAndWidthWidget : public QObject
{
    Q_OBJECT

public:
    explicit ColorAndWidthWidget(QObject* parent = nullptr);
    ~ColorAndWidthWidget();

    // =========================================================================
    // Color Methods
    // =========================================================================

    void setShowColorSection(bool show);
    bool showColorSection() const { return m_showColorSection; }
    void setColors(const QVector<QColor>& colors);
    void setCurrentColor(const QColor& color);
    QColor currentColor() const;
    void setShowMoreButton(bool show);

    // =========================================================================
    // Width Methods
    // =========================================================================

    void setShowWidthSection(bool show);
    bool showWidthSection() const { return m_showWidthSection; }
    void setWidthSectionHidden(bool hidden);  // Hide UI but keep wheel functionality
    bool isWidthSectionHidden() const { return m_widthSectionHidden; }
    void setWidthRange(int min, int max);
    void setCurrentWidth(int width);
    int currentWidth() const;

    // =========================================================================
    // Text Formatting Methods
    // =========================================================================

    void setShowTextSection(bool show);
    bool showTextSection() const { return m_showTextSection; }
    void setBold(bool enabled);
    bool isBold() const;
    void setItalic(bool enabled);
    bool isItalic() const;
    void setUnderline(bool enabled);
    bool isUnderline() const;
    void setFontSize(int size);
    int fontSize() const;
    void setFontFamily(const QString& family);
    QString fontFamily() const;

    // =========================================================================
    // Arrow Style Methods
    // =========================================================================

    void setShowArrowStyleSection(bool show);
    bool showArrowStyleSection() const { return m_showArrowStyleSection; }
    void setArrowStyle(LineEndStyle style);
    LineEndStyle arrowStyle() const;

    // =========================================================================
    // Line Style Methods
    // =========================================================================

    void setShowLineStyleSection(bool show);
    bool showLineStyleSection() const { return m_showLineStyleSection; }
    void setLineStyle(LineStyle style);
    LineStyle lineStyle() const;

    // =========================================================================
    // Shape Methods
    // =========================================================================

    void setShowShapeSection(bool show);
    bool showShapeSection() const { return m_showShapeSection; }
    void setShapeType(ShapeType type);
    ShapeType shapeType() const;
    void setShapeFillMode(ShapeFillMode mode);
    ShapeFillMode shapeFillMode() const;

    // =========================================================================
    // Size Methods (for Step Badge)
    // =========================================================================

    void setShowSizeSection(bool show);
    bool showSizeSection() const { return m_showSizeSection; }
    void setStepBadgeSize(StepBadgeSize size);
    StepBadgeSize stepBadgeSize() const;

    // =========================================================================
    // Mosaic Width Methods
    // =========================================================================

    void setShowMosaicWidthSection(bool show);
    bool showMosaicWidthSection() const { return m_showMosaicWidthSection; }
    void setMosaicWidthRange(int min, int max);
    void setMosaicWidth(int width);
    int mosaicWidth() const;

    // =========================================================================
    // Auto Blur Methods
    // =========================================================================

    void setShowAutoBlurSection(bool show);
    bool showAutoBlurSection() const { return m_showAutoBlurSection; }
    void setAutoBlurEnabled(bool enabled);
    void setAutoBlurProcessing(bool processing);

    // =========================================================================
    // Visibility and Positioning
    // =========================================================================

    void setVisible(bool visible) { m_visible = visible; }
    bool isVisible() const { return m_visible; }
    void updatePosition(const QRect& anchorRect, bool above = false, int screenWidth = 0);
    QRect boundingRect() const { return m_widgetRect; }

    // =========================================================================
    // Rendering
    // =========================================================================

    void draw(QPainter& painter);

    // =========================================================================
    // Event Handling
    // =========================================================================

    bool contains(const QPoint& pos) const;
    bool handleClick(const QPoint& pos);
    bool handleMousePress(const QPoint& pos);
    bool handleMouseMove(const QPoint& pos, bool pressed);
    bool handleMouseRelease(const QPoint& pos);
    bool updateHovered(const QPoint& pos);
    bool handleWheel(int delta);

signals:
    // Color signals
    void colorSelected(const QColor& color);
    void moreColorsRequested();

    // Width signals
    void widthChanged(int width);

    // Text formatting signals
    void boldToggled(bool enabled);
    void italicToggled(bool enabled);
    void underlineToggled(bool enabled);
    void fontSizeChanged(int size);
    void fontFamilyChanged(const QString& family);
    void fontSizeDropdownRequested(const QPoint& globalPos);
    void fontFamilyDropdownRequested(const QPoint& globalPos);

    // Arrow style signals
    void arrowStyleChanged(LineEndStyle style);

    // Line style signals
    void lineStyleChanged(LineStyle style);

    // Shape signals
    void shapeTypeChanged(ShapeType type);
    void shapeFillModeChanged(ShapeFillMode mode);

    // Size signals (Step Badge)
    void stepBadgeSizeChanged(StepBadgeSize size);

    // Mosaic width signals
    void mosaicWidthChanged(int width);

    // Auto blur signals
    void autoBlurRequested();

private:
    void updateLayout();
    void connectSectionSignals();

    // Section components
    ColorSection* m_colorSection;
    WidthSection* m_widthSection;
    MosaicWidthSection* m_mosaicWidthSection;
    TextSection* m_textSection;
    ArrowStyleSection* m_arrowStyleSection;
    LineStyleSection* m_lineStyleSection;
    ShapeSection* m_shapeSection;
    SizeSection* m_sizeSection;
    AutoBlurSection* m_autoBlurSection;

    // Section visibility
    bool m_showColorSection = true;
    bool m_showWidthSection = true;
    bool m_widthSectionHidden = false;  // Hide UI but keep wheel functionality
    bool m_showMosaicWidthSection = false;
    bool m_showTextSection = false;
    bool m_showArrowStyleSection = false;
    bool m_showLineStyleSection = false;
    bool m_showShapeSection = false;
    bool m_showSizeSection = false;
    bool m_showAutoBlurSection = false;

    // Layout state
    bool m_visible = false;
    bool m_dropdownExpandsUpward = false;
    QRect m_widgetRect;

    // Toolbar style configuration
    ToolbarStyleConfig m_styleConfig;

    // Layout constants
    static constexpr int WIDGET_HEIGHT = 28;  // Compact height for sub-toolbar
    static constexpr int SECTION_SPACING = 8;
    static constexpr int WIDTH_TO_COLOR_SPACING = 2;  // Smaller spacing between width and color sections
    static constexpr int WIDGET_RIGHT_MARGIN = 6;
};

#endif // COLORANDWIDTHWIDGET_H
