#ifndef ASPECTRATIOWIDGET_H
#define ASPECTRATIOWIDGET_H

#include <QObject>
#include <QRect>
#include <QPoint>
#include "ToolbarStyle.h"

class QPainter;

/**
 * @brief Aspect ratio lock widget for region selection.
 *
 * Displays a lock button that toggles aspect ratio locking based on
 * the current selection.
 */
class AspectRatioWidget : public QObject
{
    Q_OBJECT

public:
    explicit AspectRatioWidget(QObject* parent = nullptr);

    void setLocked(bool locked);
    bool isLocked() const { return m_locked; }
    void setLockedRatio(int width, int height);

    void setVisible(bool visible) { m_visible = visible; }
    bool isVisible() const { return m_visible; }

    // Positioning - typically to the right of dimension info panel
    void updatePosition(const QRect& anchorRect, int screenWidth, int screenHeight);

    // Rendering
    void draw(QPainter& painter);

    // Hit testing and events
    QRect boundingRect() const { return m_widgetRect; }
    bool contains(const QPoint& pos) const;
    bool handleMousePress(const QPoint& pos);
    bool handleMouseMove(const QPoint& pos, bool pressed);
    bool handleMouseRelease(const QPoint& pos);

signals:
    void lockChanged(bool locked);

private:
    void updateLayout();
    QString labelText() const;

    // State
    bool m_locked = false;
    int m_ratioWidth = 1;
    int m_ratioHeight = 1;
    bool m_visible = false;
    bool m_hovered = false;

    // Layout rects
    QRect m_widgetRect;
    QRect m_textRect;

    ToolbarStyleConfig m_styleConfig;

    // Layout constants
    static constexpr int WIDGET_HEIGHT = 28;
    static constexpr int WIDGET_WIDTH = 110;
    static constexpr int PADDING = 8;
    static constexpr int GAP = 8;
};

#endif // ASPECTRATIOWIDGET_H
