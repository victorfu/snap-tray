#ifndef LINEWIDTHWIDGET_H
#define LINEWIDTHWIDGET_H

#include <QObject>
#include <QRect>
#include <QPoint>
#include <QColor>

class QPainter;

/**
 * @brief Line width picker component for annotation tools.
 *
 * Provides a slider and preview for adjusting stroke width.
 * Follows the same popup pattern as ColorPaletteWidget.
 */
class LineWidthWidget : public QObject
{
    Q_OBJECT

public:
    explicit LineWidthWidget(QObject* parent = nullptr);

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

    /**
     * @brief Set the preview color (annotation color).
     */
    void setPreviewColor(const QColor& color) { m_previewColor = color; }

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
     * @param anchorRect The rect to anchor the widget to
     * @param above If true, position above the anchor; otherwise below
     * @param screenWidth Screen width for right boundary check (0 to disable)
     */
    void updatePosition(const QRect& anchorRect, bool above = false, int screenWidth = 0);

    /**
     * @brief Draw the line width widget.
     * @param painter The QPainter to draw with
     */
    void draw(QPainter& painter);

    /**
     * @brief Get the bounding rectangle of the widget.
     */
    QRect boundingRect() const { return m_widgetRect; }

    /**
     * @brief Check if position is within the widget.
     */
    bool contains(const QPoint& pos) const;

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

signals:
    /**
     * @brief Emitted when the width value changes.
     */
    void widthChanged(int width);

private:
    void updateLayout();
    int positionToWidth(int x) const;
    int widthToPosition(int width) const;

    int m_minWidth;
    int m_maxWidth;
    int m_currentWidth;
    bool m_visible;
    bool m_isDragging;
    bool m_hovered;
    QColor m_previewColor;

    QRect m_widgetRect;
    QRect m_sliderTrackRect;
    QRect m_previewRect;
    QRect m_labelRect;

    // Layout constants
    static const int WIDGET_HEIGHT = 28;
    static const int WIDGET_WIDTH = 160;
    static const int SLIDER_HEIGHT = 6;
    static const int HANDLE_SIZE = 14;
    static const int PREVIEW_SIZE = 18;
    static const int PADDING = 8;
};

#endif // LINEWIDTHWIDGET_H
