#ifndef LOADINGSPINNERRENDERER_H
#define LOADINGSPINNERRENDERER_H

#include <QObject>
#include <QPoint>
#include <QColor>
#include <QTimer>

class QPainter;

class LoadingSpinnerRenderer : public QObject
{
    Q_OBJECT

public:
    explicit LoadingSpinnerRenderer(QObject *parent = nullptr);
    ~LoadingSpinnerRenderer() override;

    void start();
    void stop();
    bool isActive() const;

    void draw(QPainter &painter, const QPoint &center) const;

signals:
    void needsRepaint();

private slots:
    void onAnimationTimer();

private:
    QTimer *m_animationTimer;
    qint64 m_startTime;
    bool m_active;

    static constexpr int DOT_COUNT = 12;
    static constexpr int SPINNER_RADIUS = 20;
    static constexpr int DOT_RADIUS = 3;
    static constexpr int ROTATION_PERIOD_MS = 1000;
    static constexpr int TIMER_INTERVAL_MS = 16;  // ~60fps
};

#endif // LOADINGSPINNERRENDERER_H
