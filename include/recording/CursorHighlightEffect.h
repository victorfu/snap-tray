#ifndef CURSORHIGHLIGHTEFFECT_H
#define CURSORHIGHLIGHTEFFECT_H

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QColor>
#include <QTimer>

class QPainter;

/**
 * @brief Persistent cursor highlight circle effect.
 *
 * Unlike ClickRippleRenderer which shows ripples on click,
 * this effect continuously follows the cursor with a highlight circle
 * to emphasize cursor position during recording.
 */
class CursorHighlightEffect : public QObject
{
    Q_OBJECT

public:
    explicit CursorHighlightEffect(QObject *parent = nullptr);
    ~CursorHighlightEffect() override;

    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    void setColor(const QColor &color);
    QColor color() const { return m_color; }

    void setRadius(int radius);
    int radius() const { return m_radius; }

    void setOpacity(qreal opacity);
    qreal opacity() const { return m_opacity; }

    /**
     * @brief Update cursor position (called each frame).
     * @param globalPos Global screen coordinates
     * @param regionTopLeft Top-left corner of recording region (for local conversion)
     */
    void updatePosition(const QPoint &globalPos, const QPoint &regionTopLeft);

    /**
     * @brief Update position directly with local coordinates.
     */
    void updateLocalPosition(const QPoint &localPos);

    /**
     * @brief Render the highlight circle.
     */
    void render(QPainter &painter) const;

    /**
     * @brief Check if there's anything to render.
     */
    bool hasVisibleContent() const;

signals:
    void needsRepaint();

private slots:
    void onUpdateTimer();

private:
    bool m_enabled = false;
    QColor m_color = QColor(255, 255, 0, 80);  // Semi-transparent yellow
    int m_radius = 40;
    qreal m_opacity = 0.3;
    QPointF m_currentPos;
    QPointF m_smoothPos;  // Smoothed position for animation
    QTimer *m_updateTimer;
    bool m_hasPosition = false;

    static constexpr int kUpdateIntervalMs = 16;  // ~60fps
    static constexpr qreal kSmoothingFactor = 0.3;  // Smoothing coefficient
};

#endif // CURSORHIGHLIGHTEFFECT_H
