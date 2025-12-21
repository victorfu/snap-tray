#ifndef MAGNIFIEROVERLAY_H
#define MAGNIFIEROVERLAY_H

#include <QObject>
#include <QPoint>
#include <QImage>
#include <QRect>
#include <QColor>

class QPainter;

/**
 * @brief Reusable magnifier and crosshair overlay component.
 *
 * Provides zoom preview of cursor position with color information.
 */
class MagnifierOverlay : public QObject
{
    Q_OBJECT

public:
    explicit MagnifierOverlay(QObject* parent = nullptr);

    /**
     * @brief Set the source image for sampling.
     */
    void setSourceImage(const QImage& image);

    /**
     * @brief Set the device pixel ratio for HiDPI support.
     */
    void setDevicePixelRatio(qreal ratio);

    /**
     * @brief Set the current cursor position (logical coordinates).
     */
    void setCursorPosition(const QPoint& pos);

    /**
     * @brief Set viewport bounds for positioning.
     */
    void setViewportRect(const QRect& rect);

    /**
     * @brief Toggle color format (HEX vs RGB).
     */
    void setShowHexColor(bool hex);
    bool showHexColor() const { return m_showHexColor; }
    void toggleColorFormat();

    /**
     * @brief Get the color at current position.
     */
    QColor colorAtCursor() const;

    /**
     * @brief Get the color string (HEX or RGB based on settings).
     */
    QString colorString() const;

    /**
     * @brief Draw the crosshair lines.
     */
    void drawCrosshair(QPainter& painter) const;

    /**
     * @brief Draw the magnifier panel with zoom preview and color info.
     */
    void drawMagnifier(QPainter& painter) const;

private:
    QImage m_sourceImage;
    qreal m_devicePixelRatio;
    QPoint m_cursorPosition;
    QRect m_viewportRect;
    bool m_showHexColor;

    // Layout constants
    static const int PANEL_WIDTH = 180;
    static const int PANEL_HEIGHT = 120;
    static const int GRID_COUNT = 15;
    static const int PANEL_PADDING = 10;
};

#endif // MAGNIFIEROVERLAY_H
