#ifndef RADIUSSLIDERWIDGET_H
#define RADIUSSLIDERWIDGET_H

#include <QObject>
#include <QRect>
#include <QPoint>
#include "ToolbarStyle.h"

class QPainter;

/**
 * @brief Radius slider component for adjusting corner radius.
 *
 * Displays a compact horizontal slider with:
 * - Label "Radius" on the left
 * - Slider track in the middle
 * - Minus/Plus buttons for fine adjustment
 * - Value display on the right (e.g., "25")
 */
class RadiusSliderWidget : public QObject
{
    Q_OBJECT

public:
    explicit RadiusSliderWidget(QObject* parent = nullptr);

    // Value management
    void setRadiusRange(int min, int max);
    void setCurrentRadius(int radius);
    int currentRadius() const { return m_currentRadius; }

    // Visibility
    void setVisible(bool visible) { m_visible = visible; }
    bool isVisible() const { return m_visible; }

    // Positioning - positions to the right of dimension info panel
    void updatePosition(const QRect& dimensionInfoRect, int screenWidth);

    // Rendering
    void draw(QPainter& painter);

    // Hit testing and events
    QRect boundingRect() const { return m_widgetRect; }
    bool contains(const QPoint& pos) const;
    bool handleMousePress(const QPoint& pos);
    bool handleMouseMove(const QPoint& pos, bool pressed);
    bool handleMouseRelease(const QPoint& pos);

signals:
    void radiusChanged(int radius);

private:
    void updateLayout();
    int positionToRadius(int x) const;
    int radiusToPosition(int radius) const;

    // State
    int m_minRadius = 0;
    int m_maxRadius = 100;
    int m_currentRadius = 0;
    bool m_visible = false;
    bool m_isDragging = false;
    int m_hoveredButton = -1;  // -1: none, 0: minus, 1: plus

    // Layout rects
    QRect m_widgetRect;
    QRect m_labelRect;
    QRect m_sliderTrackRect;
    QRect m_valueRect;
    QRect m_minusButtonRect;
    QRect m_plusButtonRect;

    // Layout constants
    static constexpr int WIDGET_HEIGHT = 28;
    static constexpr int WIDGET_WIDTH = 200;
    static constexpr int SLIDER_HEIGHT = 6;
    static constexpr int HANDLE_SIZE = 10;
    static constexpr int BUTTON_SIZE = 22;
    static constexpr int PADDING = 8;
    static constexpr int LABEL_WIDTH = 42;  // "Radius"
    static constexpr int VALUE_WIDTH = 28;  // "100"
    static constexpr int GAP = 8;  // Gap between dimension info and this widget

    ToolbarStyleConfig m_styleConfig;
};

#endif // RADIUSSLIDERWIDGET_H
