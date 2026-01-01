#include "recording/CursorHighlightEffect.h"
#include <QPainter>
#include <QRadialGradient>
#include <cmath>

CursorHighlightEffect::CursorHighlightEffect(QObject *parent)
    : QObject(parent)
    , m_updateTimer(new QTimer(this))
{
    m_updateTimer->setInterval(kUpdateIntervalMs);
    connect(m_updateTimer, &QTimer::timeout, this, &CursorHighlightEffect::onUpdateTimer);
}

CursorHighlightEffect::~CursorHighlightEffect() = default;

void CursorHighlightEffect::setEnabled(bool enabled)
{
    if (m_enabled != enabled) {
        m_enabled = enabled;
        if (enabled && m_hasPosition) {
            m_updateTimer->start();
        } else {
            m_updateTimer->stop();
        }
        emit needsRepaint();
    }
}

void CursorHighlightEffect::setColor(const QColor &color)
{
    if (m_color != color) {
        m_color = color;
        emit needsRepaint();
    }
}

void CursorHighlightEffect::setRadius(int radius)
{
    int newRadius = qBound(10, radius, 100);
    if (m_radius != newRadius) {
        m_radius = newRadius;
        emit needsRepaint();
    }
}

void CursorHighlightEffect::setOpacity(qreal opacity)
{
    qreal newOpacity = qBound(0.1, opacity, 0.8);
    if (!qFuzzyCompare(m_opacity, newOpacity)) {
        m_opacity = newOpacity;
        emit needsRepaint();
    }
}

void CursorHighlightEffect::updatePosition(const QPoint &globalPos, const QPoint &regionTopLeft)
{
    updateLocalPosition(globalPos - regionTopLeft);
}

void CursorHighlightEffect::updateLocalPosition(const QPoint &localPos)
{
    m_currentPos = localPos;

    if (!m_hasPosition) {
        // First position - no smoothing
        m_smoothPos = m_currentPos;
        m_hasPosition = true;
        if (m_enabled && !m_updateTimer->isActive()) {
            m_updateTimer->start();
        }
    }

    emit needsRepaint();
}

void CursorHighlightEffect::onUpdateTimer()
{
    if (!m_enabled || !m_hasPosition) {
        m_updateTimer->stop();
        return;
    }

    // Smooth interpolation towards current position
    QPointF delta = m_currentPos - m_smoothPos;
    qreal distance = std::sqrt(delta.x() * delta.x() + delta.y() * delta.y());

    if (distance > 0.5) {
        m_smoothPos = m_smoothPos + delta * kSmoothingFactor;
        emit needsRepaint();
    }
}

void CursorHighlightEffect::render(QPainter &painter) const
{
    if (!m_enabled || !m_hasPosition) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    // Create radial gradient for soft edge
    QRadialGradient gradient(m_smoothPos, m_radius);

    QColor centerColor = m_color;
    centerColor.setAlphaF(m_opacity);

    QColor midColor = m_color;
    midColor.setAlphaF(m_opacity * 0.5);

    QColor edgeColor = m_color;
    edgeColor.setAlphaF(0.0);

    gradient.setColorAt(0.0, centerColor);
    gradient.setColorAt(0.6, centerColor);
    gradient.setColorAt(0.85, midColor);
    gradient.setColorAt(1.0, edgeColor);

    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawEllipse(m_smoothPos, m_radius, m_radius);

    painter.restore();
}

bool CursorHighlightEffect::hasVisibleContent() const
{
    return m_enabled && m_hasPosition;
}
