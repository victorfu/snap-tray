#ifndef CLICKRIPPLERENDERER_H
#define CLICKRIPPLERENDERER_H

#include <QObject>
#include <QPoint>
#include <QColor>
#include <QVector>
#include <QTimer>

class QPainter;

struct RippleAnimation {
    QPoint center;
    qint64 startTime;
};

class ClickRippleRenderer : public QObject
{
    Q_OBJECT

public:
    explicit ClickRippleRenderer(QObject *parent = nullptr);
    ~ClickRippleRenderer() override;

    void setEnabled(bool enabled);
    bool isEnabled() const;

    void setColor(const QColor &color);
    QColor color() const;

    void triggerRipple(const QPoint &center);
    void draw(QPainter &painter) const;
    bool hasActiveAnimations() const;

signals:
    void needsRepaint();

private slots:
    void onAnimationTimer();

private:
    void removeCompletedAnimations();
    void ensureTimerRunning();

    QVector<RippleAnimation> m_ripples;
    QTimer *m_animationTimer;
    QColor m_color;
    bool m_enabled;

    static constexpr int RIPPLE_DURATION_MS = 400;
    static constexpr int MAX_RADIUS = 40;
    static constexpr int TIMER_INTERVAL_MS = 16;  // ~60fps
};

#endif // CLICKRIPPLERENDERER_H
