#ifndef LASERPOINTERRENDERER_H
#define LASERPOINTERRENDERER_H

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QColor>
#include <QVector>
#include <QTimer>

class QPainter;

struct LaserPoint {
    QPointF position;
    qint64 timestamp;  // ms since epoch
};

class LaserPointerRenderer : public QObject
{
    Q_OBJECT

public:
    explicit LaserPointerRenderer(QObject *parent = nullptr);
    ~LaserPointerRenderer() override;

    void setColor(const QColor &color);
    QColor color() const;

    void setWidth(int width);
    int width() const;

    void startDrawing(const QPoint &pos);
    void updateDrawing(const QPoint &pos);
    void stopDrawing();
    bool isDrawing() const;

    void draw(QPainter &painter) const;
    bool hasVisiblePoints() const;

signals:
    void needsRepaint();

private slots:
    void onFadeTimer();

private:
    void removeExpiredStrokes();
    void ensureTimerRunning();

    QVector<QVector<LaserPoint>> m_strokes;  // Completed strokes
    QVector<LaserPoint> m_currentStroke;      // Stroke being drawn
    QTimer *m_fadeTimer;
    QColor m_color;
    int m_width;
    bool m_isDrawing;

    // Smoothing state (mirrors PencilTool behavior for consistent stroke feel)
    QPointF m_smoothedPoint;
    QPointF m_smoothedVelocity;
    QPointF m_lastRawPoint;
    bool m_hasSmoothedPoint = false;

    static constexpr qreal kMinPointDistance = 2.0;
    static constexpr qreal kBaseSmoothing = 0.3;
    static constexpr qreal kVelocitySmoothing = 0.4;
    static constexpr qreal kSpeedThresholdLow = 3.0;
    static constexpr qreal kSpeedThresholdHigh = 15.0;

    static constexpr int FADE_DURATION_MS = 2000;
    static constexpr int TIMER_INTERVAL_MS = 16;  // ~60fps
};

#endif // LASERPOINTERRENDERER_H
