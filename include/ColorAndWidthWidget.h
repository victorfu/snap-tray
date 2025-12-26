#ifndef COLORANDWIDTHWIDGET_H
#define COLORANDWIDTHWIDGET_H

#include <QObject>
#include <QVector>
#include <QColor>
#include <QRect>
#include <QPoint>
#include <QString>
#include "annotations/AnnotationLayer.h"
#include "annotations/ShapeAnnotation.h"  // For ShapeType and ShapeFillMode enums
#include "annotations/ArrowAnnotation.h"  // For LineEndStyle enum
#include "ToolbarStyle.h"

class QPainter;

/**
 * @brief Unified color palette and line width picker component.
 *
 * Combines ColorPaletteWidget and LineWidthWidget into a single horizontal
 * component with color swatches on the left and line width slider on the right.
 * Reduces vertical toolbar space from 60px to 32px.
 *
 * When text tool is active, shows additional text formatting controls:
 * Bold, Italic, Underline toggles, font size dropdown, font family dropdown.
 */
class ColorAndWidthWidget : public QObject
{
    Q_OBJECT

public:
    explicit ColorAndWidthWidget(QObject* parent = nullptr);

    // Color palette methods
    /**
     * @brief Set the available colors in the palette.
     */
    void setColors(const QVector<QColor>& colors);

    /**
     * @brief Set the currently selected color.
     */
    void setCurrentColor(const QColor& color);

    /**
     * @brief Get the currently selected color.
     */
    QColor currentColor() const { return m_currentColor; }

    /**
     * @brief Set whether to show the "more colors" button.
     */
    void setShowMoreButton(bool show) { m_showMoreButton = show; }

    /**
     * @brief Set whether to show the width section.
     */
    void setShowWidthSection(bool show);

    /**
     * @brief Check if the width section is shown.
     */
    bool showWidthSection() const { return m_showWidthSection; }

    // Text formatting methods
    /**
     * @brief Set whether to show the text formatting section.
     */
    void setShowTextSection(bool show);

    /**
     * @brief Check if the text formatting section is shown.
     */
    bool showTextSection() const { return m_showTextSection; }

    /**
     * @brief Set bold state.
     */
    void setBold(bool enabled);

    /**
     * @brief Get bold state.
     */
    bool isBold() const { return m_boldEnabled; }

    /**
     * @brief Set italic state.
     */
    void setItalic(bool enabled);

    /**
     * @brief Get italic state.
     */
    bool isItalic() const { return m_italicEnabled; }

    /**
     * @brief Set underline state.
     */
    void setUnderline(bool enabled);

    /**
     * @brief Get underline state.
     */
    bool isUnderline() const { return m_underlineEnabled; }

    /**
     * @brief Set font size.
     */
    void setFontSize(int size);

    /**
     * @brief Get font size.
     */
    int fontSize() const { return m_fontSize; }

    /**
     * @brief Set font family.
     */
    void setFontFamily(const QString& family);

    /**
     * @brief Get font family.
     */
    QString fontFamily() const { return m_fontFamily; }

    // Arrow style methods
    /**
     * @brief Set whether to show the arrow style section.
     */
    void setShowArrowStyleSection(bool show);

    /**
     * @brief Check if the arrow style section is shown.
     */
    bool showArrowStyleSection() const { return m_showArrowStyleSection; }

    /**
     * @brief Set the arrow line end style.
     */
    void setArrowStyle(LineEndStyle style);

    /**
     * @brief Get the current arrow style.
     */
    LineEndStyle arrowStyle() const { return m_arrowStyle; }

    // Shape section methods
    /**
     * @brief Set whether to show the shape section.
     */
    void setShowShapeSection(bool show);

    /**
     * @brief Check if the shape section is shown.
     */
    bool showShapeSection() const { return m_showShapeSection; }

    /**
     * @brief Set the shape type.
     */
    void setShapeType(ShapeType type);

    /**
     * @brief Get the current shape type.
     */
    ShapeType shapeType() const { return m_shapeType; }

    /**
     * @brief Set the shape fill mode.
     */
    void setShapeFillMode(ShapeFillMode mode);

    /**
     * @brief Get the current shape fill mode.
     */
    ShapeFillMode shapeFillMode() const { return m_shapeFillMode; }

    // Line width methods
    /**
     * @brief Set the width range.
     */
    void setWidthRange(int min, int max);

    /**
     * @brief Set the current width value.
     */
    void setCurrentWidth(int width);

    /**
     * @brief Get the current width value.
     */
    int currentWidth() const { return m_currentWidth; }

    // Visibility and positioning
    /**
     * @brief Set the widget visibility.
     */
    void setVisible(bool visible) { m_visible = visible; }

    /**
     * @brief Check if the widget is visible.
     */
    bool isVisible() const { return m_visible; }

    /**
     * @brief Update the widget position.
     * @param anchorRect The rect to anchor the widget to (e.g., toolbar)
     * @param above If true, position above the anchor; otherwise below
     * @param screenWidth Screen width for right boundary check (0 to disable)
     */
    void updatePosition(const QRect& anchorRect, bool above = false, int screenWidth = 0);

    /**
     * @brief Get the bounding rectangle of the widget.
     */
    QRect boundingRect() const { return m_widgetRect; }

    // Rendering
    /**
     * @brief Draw the unified widget.
     * @param painter The QPainter to draw with
     */
    void draw(QPainter& painter);

    // Event handling
    /**
     * @brief Check if position is within the widget.
     */
    bool contains(const QPoint& pos) const;

    /**
     * @brief Handle a click at the given position.
     * @return true if the click was handled (was on widget)
     */
    bool handleClick(const QPoint& pos);

    /**
     * @brief Handle mouse press event.
     * @return true if the event was handled
     */
    bool handleMousePress(const QPoint& pos);

    /**
     * @brief Handle mouse move event.
     * @return true if the event was handled
     */
    bool handleMouseMove(const QPoint& pos, bool pressed);

    /**
     * @brief Handle mouse release event.
     * @return true if the event was handled
     */
    bool handleMouseRelease(const QPoint& pos);

    /**
     * @brief Update hover state.
     * @return true if the state changed
     */
    bool updateHovered(const QPoint& pos);

    /**
     * @brief Handle mouse wheel event for width adjustment.
     * @param delta Wheel delta (positive = scroll up, negative = scroll down)
     * @return true if the event was handled
     */
    bool handleWheel(int delta);

signals:
    /**
     * @brief Emitted when a color is selected from the palette.
     */
    void colorSelected(const QColor& color);

    /**
     * @brief Emitted when "more colors" button is clicked.
     */
    void moreColorsRequested();

    /**
     * @brief Emitted when the width value changes.
     */
    void widthChanged(int width);

    // Text formatting signals
    /**
     * @brief Emitted when bold state is toggled.
     */
    void boldToggled(bool enabled);

    /**
     * @brief Emitted when italic state is toggled.
     */
    void italicToggled(bool enabled);

    /**
     * @brief Emitted when underline state is toggled.
     */
    void underlineToggled(bool enabled);

    /**
     * @brief Emitted when font size is changed.
     */
    void fontSizeChanged(int size);

    /**
     * @brief Emitted when font family is changed.
     */
    void fontFamilyChanged(const QString& family);

    /**
     * @brief Emitted when font size dropdown is requested.
     */
    void fontSizeDropdownRequested(const QPoint& globalPos);

    /**
     * @brief Emitted when font family dropdown is requested.
     */
    void fontFamilyDropdownRequested(const QPoint& globalPos);

    /**
     * @brief Emitted when arrow style is changed.
     */
    void arrowStyleChanged(LineEndStyle style);

    /**
     * @brief Emitted when shape type is changed.
     */
    void shapeTypeChanged(ShapeType type);

    /**
     * @brief Emitted when shape fill mode is changed.
     */
    void shapeFillModeChanged(ShapeFillMode mode);

private:
    // Layout calculation
    void updateLayout();

    // Color section helpers
    void drawColorSection(QPainter& painter);
    int swatchAtPosition(const QPoint& pos) const;

    // Width section helpers
    void drawWidthSection(QPainter& painter);
    bool isInWidthSection(const QPoint& pos) const;

    // Text section helpers
    void drawTextSection(QPainter& painter);
    int textControlAtPosition(const QPoint& pos) const;

    // Arrow style section helpers
    void drawArrowStyleSection(QPainter& painter);
    void drawArrowStyleIcon(QPainter& painter, LineEndStyle style, const QRect& rect, bool isHovered = false) const;
    int arrowStyleOptionAtPosition(const QPoint& pos) const;

    // Shape section helpers
    void drawShapeSection(QPainter& painter);
    int shapeButtonAtPosition(const QPoint& pos) const;

    // Color state
    QVector<QColor> m_colors;
    QColor m_currentColor;
    bool m_showMoreButton;
    QVector<QRect> m_swatchRects;
    int m_hoveredSwatch;

    // Width state
    int m_minWidth;
    int m_maxWidth;
    int m_currentWidth;
    bool m_widthSectionHovered;
    bool m_showWidthSection;

    // Text formatting state
    bool m_showTextSection = false;
    bool m_boldEnabled = true;
    bool m_italicEnabled = false;
    bool m_underlineEnabled = false;
    int m_fontSize = 16;
    QString m_fontFamily;
    QRect m_textSectionRect;
    QRect m_boldButtonRect;
    QRect m_italicButtonRect;
    QRect m_underlineButtonRect;
    QRect m_fontSizeRect;
    QRect m_fontFamilyRect;
    int m_hoveredTextControl = -1;  // 0=B, 1=I, 2=U, 3=size, 4=family

    // Arrow style state
    bool m_showArrowStyleSection = false;
    LineEndStyle m_arrowStyle = LineEndStyle::EndArrow;
    QRect m_arrowStyleButtonRect;
    QRect m_arrowStyleDropdownRect;
    bool m_arrowStyleDropdownOpen = false;
    int m_hoveredArrowStyleOption = -1;  // -1=none, 0-3=style options

    // Shape section state
    bool m_showShapeSection = false;
    ShapeType m_shapeType = ShapeType::Rectangle;
    ShapeFillMode m_shapeFillMode = ShapeFillMode::Outline;
    QRect m_shapeSectionRect;
    QVector<QRect> m_shapeButtonRects;  // 4 buttons: [Rect, Ellipse, Outline, Filled]
    int m_hoveredShapeButton = -1;

    // Layout state
    bool m_visible;
    bool m_dropdownExpandsUpward = false;  // True when widget is above toolbar (ScreenCanvas)
    QRect m_widgetRect;
    QRect m_colorSectionRect;
    QRect m_widthSectionRect;

    // Layout constants
    static const int WIDGET_HEIGHT = 36;
    static const int SWATCH_SIZE = 16;
    static const int SWATCH_SPACING = 2;
    static const int COLORS_PER_ROW = 8;
    static const int SECTION_PADDING = 6;
    static const int SECTION_SPACING = 8;
    static const int WIDTH_SECTION_SIZE = 36;  // Size for the dot preview area
    static const int MAX_DOT_SIZE = 24;        // Max visual dot size

    // Text section layout constants
    static const int TEXT_BUTTON_SIZE = 20;
    static const int TEXT_BUTTON_SPACING = 2;
    static const int FONT_SIZE_WIDTH = 40;
    static const int FONT_FAMILY_WIDTH = 90;

    // Arrow style section layout constants
    static const int ARROW_STYLE_BUTTON_WIDTH = 50;
    static const int ARROW_STYLE_BUTTON_HEIGHT = 24;
    static const int ARROW_STYLE_DROPDOWN_OPTION_HEIGHT = 28;

    // Shape section layout constants
    static const int SHAPE_BUTTON_SIZE = 24;
    static const int SHAPE_BUTTON_SPACING = 2;

    // Toolbar style configuration
    ToolbarStyleConfig m_styleConfig;
};

#endif // COLORANDWIDTHWIDGET_H
