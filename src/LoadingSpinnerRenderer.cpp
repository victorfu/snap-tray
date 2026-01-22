#include "LoadingSpinnerRenderer.h"
#include <QPainter>
#include <QDateTime>
#include <cmath>

LoadingSpinnerRenderer::LoadingSpinnerRenderer(QObject *parent)
    : QObject(parent)
    , m_animationTimer(new QTimer(this))
    , m_startTime(0)
    , m_active(false)
{
    m_animationTimer->setInterval(TIMER_INTERVAL_MS);
    connect(m_animationTimer, &QTimer::timeout, this, &LoadingSpinnerRenderer::onAnimationTimer);
}

LoadingSpinnerRenderer::~LoadingSpinnerRenderer() = default;

void LoadingSpinnerRenderer::start()
{
    if (m_active) {
        return;
    }

    m_active = true;
    m_startTime = QDateTime::currentMSecsSinceEpoch();
    m_animationTimer->start();
    emit needsRepaint();
}

void LoadingSpinnerRenderer::stop()
{
    if (!m_active) {
        return;
    }

    m_active = false;
    m_animationTimer->stop();
    emit needsRepaint();
}

bool LoadingSpinnerRenderer::isActive() const
{
    return m_active;
}

void LoadingSpinnerRenderer::draw(QPainter &painter, const QPoint &center) const
{
    if (!m_active) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 elapsed = now - m_startTime;

    // Calculate rotation offset based on elapsed time
    qreal rotationProgress = static_cast<qreal>(elapsed % ROTATION_PERIOD_MS) / ROTATION_PERIOD_MS;
    qreal rotationOffset = rotationProgress * 2.0 * M_PI;

    // Draw dots in a circle
    for (int i = 0; i < DOT_COUNT; ++i) {
        qreal angle = (2.0 * M_PI * i / DOT_COUNT) - rotationOffset;

        // Calculate dot position
        int x = center.x() + static_cast<int>(SPINNER_RADIUS * std::cos(angle));
        int y = center.y() + static_cast<int>(SPINNER_RADIUS * std::sin(angle));

        // Calculate opacity: dots fade out as they trail behind the leading dot
        // The "leading" dot is at angle 0 (after rotation offset)
        qreal normalizedAngle = std::fmod(angle + rotationOffset + 2.0 * M_PI, 2.0 * M_PI);
        qreal opacity = 1.0 - (normalizedAngle / (2.0 * M_PI)) * 0.8;  // Range: 0.2 to 1.0

        QColor dotColor(59, 130, 246);  // Bright blue (#3b82f6) - visible on both dark and light backgrounds
        dotColor.setAlphaF(opacity);

        painter.setPen(Qt::NoPen);
        painter.setBrush(dotColor);
        painter.drawEllipse(QPoint(x, y), DOT_RADIUS, DOT_RADIUS);
    }

    painter.restore();
}

void LoadingSpinnerRenderer::onAnimationTimer()
{
    emit needsRepaint();
}
