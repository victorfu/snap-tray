#include "ClickRippleRenderer.h"
#include <QPainter>
#include <QDateTime>
#include <algorithm>

ClickRippleRenderer::ClickRippleRenderer(QObject *parent)
    : QObject(parent)
    , m_animationTimer(new QTimer(this))
    , m_color(QColor(255, 200, 0))  // Yellow/gold
    , m_enabled(false)
{
    m_animationTimer->setInterval(TIMER_INTERVAL_MS);
    connect(m_animationTimer, &QTimer::timeout, this, &ClickRippleRenderer::onAnimationTimer);
}

ClickRippleRenderer::~ClickRippleRenderer() = default;

void ClickRippleRenderer::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

bool ClickRippleRenderer::isEnabled() const
{
    return m_enabled;
}

void ClickRippleRenderer::setColor(const QColor &color)
{
    m_color = color;
}

QColor ClickRippleRenderer::color() const
{
    return m_color;
}

void ClickRippleRenderer::triggerRipple(const QPoint &center)
{
    if (!m_enabled) {
        return;
    }

    RippleAnimation ripple;
    ripple.center = center;
    ripple.startTime = QDateTime::currentMSecsSinceEpoch();
    m_ripples.append(ripple);

    ensureTimerRunning();
    emit needsRepaint();
}

void ClickRippleRenderer::draw(QPainter &painter) const
{
    if (m_ripples.isEmpty()) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    qint64 now = QDateTime::currentMSecsSinceEpoch();

    for (const auto &ripple : m_ripples) {
        qint64 elapsed = now - ripple.startTime;
        qreal progress = static_cast<qreal>(elapsed) / RIPPLE_DURATION_MS;
        progress = qBound(0.0, progress, 1.0);

        int radius = static_cast<int>(progress * MAX_RADIUS);
        qreal opacity = 1.0 - progress;

        QColor ringColor = m_color;
        ringColor.setAlphaF(opacity * 0.6);  // Semi-transparent

        QPen pen(ringColor, 3, Qt::SolidLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(ripple.center, radius, radius);
    }

    painter.restore();
}

bool ClickRippleRenderer::hasActiveAnimations() const
{
    return !m_ripples.isEmpty();
}

void ClickRippleRenderer::onAnimationTimer()
{
    removeCompletedAnimations();

    // Stop timer if no active animations
    if (m_ripples.isEmpty()) {
        m_animationTimer->stop();
    }

    emit needsRepaint();
}

void ClickRippleRenderer::removeCompletedAnimations()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    auto it = std::remove_if(m_ripples.begin(), m_ripples.end(),
        [now](const RippleAnimation &r) {
            return (now - r.startTime) > RIPPLE_DURATION_MS;
        });
    m_ripples.erase(it, m_ripples.end());
}

void ClickRippleRenderer::ensureTimerRunning()
{
    if (!m_animationTimer->isActive()) {
        m_animationTimer->start();
    }
}
