#ifndef REGIONCONTROLWIDGET_H
#define REGIONCONTROLWIDGET_H

#include <QObject>
#include <QRect>
#include <QPoint>
#include <QString>
#include "ToolbarStyle.h"

class QPainter;

/**
 * @brief Unified region control widget combining radius toggle and aspect ratio lock.
 *
 * Layout:
 * +------------------------------------------+
 * |  [Radius]  |  [Ratio Lock] [16:9]       |
 * +------------------------------------------+
 *       ^
 *       |  (hover popup)
 * +----------------------+
 * |  [-] ====O=== [+] 25 |
 * +----------------------+
 *
 * Features:
 * - Shared glass panel background
 * - Radius toggle on the left (dimmed when radius=0)
 * - Aspect ratio lock on the right
 * - Slider popup appears on hover over radius toggle
 * - Mouse wheel adjusts radius
 */
class RegionControlWidget : public QObject
{
    Q_OBJECT

public:
    explicit RegionControlWidget(QObject* parent = nullptr);

    // =========================================================================
    // Radius Methods
    // =========================================================================

    void setCurrentRadius(int radius);
    int currentRadius() const { return m_currentRadius; }

    void setRadiusEnabled(bool enabled);
    bool isRadiusEnabled() const { return m_radiusEnabled; }

    // =========================================================================
    // Aspect Ratio Methods
    // =========================================================================

    bool isLocked() const { return m_ratioLocked; }
    void setLockedRatio(int width, int height);

    // =========================================================================
    // Visibility and Positioning
    // =========================================================================

    void setVisible(bool visible) { m_visible = visible; }
    bool isVisible() const { return m_visible; }
    void updatePosition(const QRect& anchorRect, int screenWidth, int screenHeight);
    QRect boundingRect() const;

    // =========================================================================
    // Rendering
    // =========================================================================

    void draw(QPainter& painter);

    // =========================================================================
    // Event Handling
    // =========================================================================

    bool contains(const QPoint& pos) const;
    bool handleMousePress(const QPoint& pos);
    bool handleMouseMove(const QPoint& pos, bool pressed);
    bool handleMouseRelease(const QPoint& pos);

signals:
    void radiusChanged(int radius);
    void radiusEnabledChanged(bool enabled);
    void lockChanged(bool locked);

private:
    void updateLayout();
    void drawRadiusToggle(QPainter& painter);
    void drawRatioSection(QPainter& painter);
    void drawSliderPopup(QPainter& painter);
    void updateSliderPopupVisibility(const QPoint& pos);
    QString ratioLabelText() const;

    // Slider helpers
    int positionToRadius(int x) const;
    int radiusToPosition(int radius) const;

    // =========================================================================
    // State
    // =========================================================================

    // Visibility
    bool m_visible = false;

    // Radius state
    int m_minRadius = 0;
    int m_maxRadius = 100;
    int m_currentRadius = 0;
    bool m_radiusEnabled = false;        // Toggle state: true = radius feature ON
    bool m_radiusHovered = false;
    bool m_sliderPopupVisible = false;
    bool m_isDraggingSlider = false;
    int m_hoveredSliderButton = -1;  // -1: none, 0: minus, 1: plus

    // Aspect ratio state
    bool m_ratioLocked = false;
    int m_ratioWidth = 1;
    int m_ratioHeight = 1;
    bool m_ratioHovered = false;

    // =========================================================================
    // Layout Rects
    // =========================================================================

    QRect m_widgetRect;           // Main widget (shared glass panel)
    QRect m_radiusToggleRect;     // Radius toggle area within widget
    QRect m_radiusIconRect;       // Radius icon position
    QRect m_ratioSectionRect;     // Ratio section area within widget
    QRect m_ratioIconRect;        // Ratio icon position
    QRect m_ratioTextRect;        // Ratio text position (when locked)

    // Slider popup rects
    QRect m_sliderPopupRect;      // Popup container
    QRect m_sliderMinusRect;      // Minus button
    QRect m_sliderPlusRect;       // Plus button
    QRect m_sliderTrackRect;      // Slider track
    QRect m_sliderValueRect;      // Value display

    // =========================================================================
    // Layout Constants
    // =========================================================================

    static constexpr int WIDGET_HEIGHT = 28;
    static constexpr int TOGGLE_SIZE = 36;              // Radius toggle width
    static constexpr int RATIO_WIDTH_FREE = 36;         // Ratio section when unlocked
    static constexpr int RATIO_WIDTH_LOCKED = 95;       // Ratio section when locked
    static constexpr int SECTION_SPACING = 0;           // Between radius and ratio sections
    static constexpr int PADDING = 8;
    static constexpr int ICON_SIZE = 18;
    static constexpr int GAP = 8;                       // Gap from anchor rect

    // Slider popup constants
    static constexpr int POPUP_HEIGHT = 28;
    static constexpr int POPUP_WIDTH = 160;
    static constexpr int POPUP_GAP = 4;                 // Gap between toggle and popup
    static constexpr int SLIDER_HEIGHT = 6;
    static constexpr int HANDLE_SIZE = 10;
    static constexpr int BUTTON_SIZE = 22;
    static constexpr int VALUE_WIDTH = 28;

    ToolbarStyleConfig m_styleConfig;
};

#endif // REGIONCONTROLWIDGET_H
