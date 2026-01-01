#include "recording/SpotlightEffect.h"
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <cmath>

SpotlightEffect::SpotlightEffect(QObject *parent)
    : QObject(parent)
    , m_updateTimer(new QTimer(this))
{
    m_updateTimer->setInterval(kUpdateIntervalMs);
    connect(m_updateTimer, &QTimer::timeout, this, &SpotlightEffect::onUpdateTimer);
}

SpotlightEffect::~SpotlightEffect() = default;

void SpotlightEffect::setEnabled(bool enabled)
{
    if (m_enabled != enabled) {
        m_enabled = enabled;
        if (enabled && m_mode == Mode::FollowCursor && m_hasPosition) {
            m_updateTimer->start();
        } else {
            m_updateTimer->stop();
        }
        emit needsRepaint();
    }
}

void SpotlightEffect::setMode(Mode mode)
{
    if (m_mode != mode) {
        m_mode = mode;
        if (m_enabled) {
            if (mode == Mode::FollowCursor && m_hasPosition) {
                m_updateTimer->start();
            } else {
                m_updateTimer->stop();
            }
        }
        emit needsRepaint();
    }
}

void SpotlightEffect::setFollowRadius(int radius)
{
    int newRadius = qBound(50, radius, 300);
    if (m_followRadius != newRadius) {
        m_followRadius = newRadius;
        emit needsRepaint();
    }
}

void SpotlightEffect::setDimOpacity(qreal opacity)
{
    qreal newOpacity = qBound(0.3, opacity, 0.9);
    if (!qFuzzyCompare(m_dimOpacity, newOpacity)) {
        m_dimOpacity = newOpacity;
        emit needsRepaint();
    }
}

void SpotlightEffect::setFeatherRadius(int radius)
{
    int newRadius = qBound(0, radius, 50);
    if (m_featherRadius != newRadius) {
        m_featherRadius = newRadius;
        emit needsRepaint();
    }
}

void SpotlightEffect::updateCursorPosition(const QPoint &localPos)
{
    m_cursorPos = localPos;

    if (!m_hasPosition) {
        // First position - no smoothing
        m_smoothCursorPos = m_cursorPos;
        m_hasPosition = true;
        if (m_enabled && m_mode == Mode::FollowCursor && !m_updateTimer->isActive()) {
            m_updateTimer->start();
        }
    }

    emit needsRepaint();
}

void SpotlightEffect::onUpdateTimer()
{
    if (!m_enabled || !m_hasPosition || m_mode != Mode::FollowCursor) {
        m_updateTimer->stop();
        return;
    }

    // Smooth interpolation towards current position
    QPointF delta = m_cursorPos - m_smoothCursorPos;
    qreal distance = std::sqrt(delta.x() * delta.x() + delta.y() * delta.y());

    if (distance > 0.5) {
        m_smoothCursorPos = m_smoothCursorPos + delta * kSmoothingFactor;
        emit needsRepaint();
    }
}

void SpotlightEffect::render(QPainter &painter, const QRect &recordingRegion) const
{
    if (!m_enabled || !m_hasPosition) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    // Create the full region path
    QPainterPath fullPath;
    fullPath.addRect(recordingRegion);

    // Create the spotlight path based on mode
    QPainterPath spotPath;
    if (m_mode == Mode::FollowCursor) {
        spotPath.addEllipse(m_smoothCursorPos, m_followRadius, m_followRadius);
    }

    // Calculate dimmed area = full region - spotlight
    QPainterPath dimPath = fullPath.subtracted(spotPath);

    // Draw the dimmed area
    QColor dimColor(0, 0, 0, int(255 * m_dimOpacity));
    painter.setPen(Qt::NoPen);
    painter.setBrush(dimColor);
    painter.drawPath(dimPath);

    // Draw feathered edge around spotlight for smooth transition
    if (m_featherRadius > 0 && m_mode == Mode::FollowCursor) {
        // Create a gradient ring around the spotlight edge
        int outerRadius = m_followRadius + m_featherRadius;

        QRadialGradient gradient(m_smoothCursorPos, outerRadius);

        // Inside the spotlight - transparent
        qreal innerStop = static_cast<qreal>(m_followRadius) / outerRadius;
        gradient.setColorAt(0.0, Qt::transparent);
        gradient.setColorAt(innerStop, Qt::transparent);

        // Feather zone - gradual dim
        gradient.setColorAt(1.0, dimColor);

        // Create ring path (outer circle minus inner circle)
        QPainterPath ringPath;
        ringPath.addEllipse(m_smoothCursorPos, outerRadius, outerRadius);
        QPainterPath innerPath;
        innerPath.addEllipse(m_smoothCursorPos, m_followRadius, m_followRadius);
        ringPath = ringPath.subtracted(innerPath);

        // Also subtract anything outside the recording region
        ringPath = ringPath.intersected(fullPath);

        painter.setBrush(gradient);
        painter.drawPath(ringPath);
    }

    painter.restore();
}

bool SpotlightEffect::hasVisibleContent() const
{
    return m_enabled && m_hasPosition;
}
