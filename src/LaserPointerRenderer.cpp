#include "LaserPointerRenderer.h"
#include <QPainter>
#include <QPainterPath>
#include <QDateTime>
#include <algorithm>

LaserPointerRenderer::LaserPointerRenderer(QObject *parent)
    : QObject(parent)
    , m_fadeTimer(new QTimer(this))
    , m_color(Qt::red)
    , m_width(3)
    , m_isDrawing(false)
{
    m_fadeTimer->setInterval(TIMER_INTERVAL_MS);
    connect(m_fadeTimer, &QTimer::timeout, this, &LaserPointerRenderer::onFadeTimer);
}

LaserPointerRenderer::~LaserPointerRenderer() = default;

void LaserPointerRenderer::setColor(const QColor &color)
{
    m_color = color;
}

QColor LaserPointerRenderer::color() const
{
    return m_color;
}

void LaserPointerRenderer::setWidth(int width)
{
    m_width = width;
}

int LaserPointerRenderer::width() const
{
    return m_width;
}

void LaserPointerRenderer::startDrawing(const QPoint &pos)
{
    m_isDrawing = true;
    m_currentStroke.clear();

    LaserPoint point;
    point.position = pos;
    point.timestamp = QDateTime::currentMSecsSinceEpoch();
    m_currentStroke.append(point);

    ensureTimerRunning();
    emit needsRepaint();
}

void LaserPointerRenderer::updateDrawing(const QPoint &pos)
{
    if (!m_isDrawing) {
        return;
    }

    LaserPoint point;
    point.position = pos;
    point.timestamp = QDateTime::currentMSecsSinceEpoch();
    m_currentStroke.append(point);

    emit needsRepaint();
}

void LaserPointerRenderer::stopDrawing()
{
    m_isDrawing = false;

    // Move current stroke to completed strokes if it has points
    if (!m_currentStroke.isEmpty()) {
        m_strokes.append(m_currentStroke);
        m_currentStroke.clear();
    }
}

bool LaserPointerRenderer::isDrawing() const
{
    return m_isDrawing;
}

// Helper function to draw a single stroke
static void drawStroke(QPainter &painter, const QVector<LaserPoint> &stroke,
                       const QColor &color, int width, qint64 now, int fadeDuration)
{
    if (stroke.size() < 2) {
        return;
    }

    QPainterPath path;
    bool pathStarted = false;
    qreal totalOpacity = 0.0;
    int visibleCount = 0;

    for (const auto &point : stroke) {
        qint64 age = now - point.timestamp;
        qreal opacity = 1.0 - static_cast<qreal>(age) / fadeDuration;
        opacity = qBound(0.0, opacity, 1.0);

        if (opacity > 0.01) {
            if (!pathStarted) {
                path.moveTo(point.position);
                pathStarted = true;
            } else {
                path.lineTo(point.position);
            }
            totalOpacity += opacity;
            visibleCount++;
        }
    }

    if (pathStarted && visibleCount > 0) {
        qreal avgOpacity = totalOpacity / visibleCount;

        QColor pathColor = color;
        pathColor.setAlphaF(avgOpacity);

        QPen pen(pathColor, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    }
}

void LaserPointerRenderer::draw(QPainter &painter) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    qint64 now = QDateTime::currentMSecsSinceEpoch();

    // Draw all completed strokes
    for (const auto &stroke : m_strokes) {
        drawStroke(painter, stroke, m_color, m_width, now, FADE_DURATION_MS);
    }

    // Draw current stroke being drawn
    if (!m_currentStroke.isEmpty()) {
        drawStroke(painter, m_currentStroke, m_color, m_width, now, FADE_DURATION_MS);
    }

    painter.restore();
}

bool LaserPointerRenderer::hasVisiblePoints() const
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    // Check current stroke
    for (const auto &point : m_currentStroke) {
        if ((now - point.timestamp) < FADE_DURATION_MS) {
            return true;
        }
    }

    // Check completed strokes
    for (const auto &stroke : m_strokes) {
        for (const auto &point : stroke) {
            if ((now - point.timestamp) < FADE_DURATION_MS) {
                return true;
            }
        }
    }

    return false;
}

void LaserPointerRenderer::onFadeTimer()
{
    removeExpiredStrokes();

    // Stop timer if no visible points and not drawing
    if (m_strokes.isEmpty() && m_currentStroke.isEmpty() && !m_isDrawing) {
        m_fadeTimer->stop();
    }

    emit needsRepaint();
}

void LaserPointerRenderer::removeExpiredStrokes()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    // Remove strokes where all points have expired
    auto it = std::remove_if(m_strokes.begin(), m_strokes.end(),
        [now, this](const QVector<LaserPoint> &stroke) {
            // A stroke is expired if ALL its points are older than FADE_DURATION_MS
            for (const auto &point : stroke) {
                if ((now - point.timestamp) <= FADE_DURATION_MS) {
                    return false;  // At least one point is still visible
                }
            }
            return true;  // All points expired
        });
    m_strokes.erase(it, m_strokes.end());
}

void LaserPointerRenderer::ensureTimerRunning()
{
    if (!m_fadeTimer->isActive()) {
        m_fadeTimer->start();
    }
}
