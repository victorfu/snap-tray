#include "LaserPointerRenderer.h"
#include <QPainter>
#include <QPainterPath>
#include <QDateTime>
#include <QtMath>
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

    QPointF startPoint(pos);
    m_smoothedPoint = startPoint;
    m_smoothedVelocity = QPointF(0, 0);
    m_lastRawPoint = startPoint;
    m_hasSmoothedPoint = true;

    LaserPoint point;
    point.position = startPoint;
    point.timestamp = QDateTime::currentMSecsSinceEpoch();
    m_currentStroke.append(point);

    ensureTimerRunning();
    emit needsRepaint();
}

void LaserPointerRenderer::updateDrawing(const QPoint &pos)
{
    if (!m_isDrawing || !m_hasSmoothedPoint) {
        return;
    }

    QPointF rawPoint(pos);
    QPointF rawVelocity = rawPoint - m_lastRawPoint;
    m_lastRawPoint = rawPoint;

    // Smooth velocity first to stabilize direction changes.
    m_smoothedVelocity = kVelocitySmoothing * rawVelocity
                       + (1.0 - kVelocitySmoothing) * m_smoothedVelocity;

    // Match Pencil adaptive smoothing behavior:
    // - more smoothing at low speed
    // - less smoothing at high speed
    qreal speed = qSqrt(rawVelocity.x() * rawVelocity.x()
                      + rawVelocity.y() * rawVelocity.y());
    qreal adaptiveFactor;
    if (speed <= kSpeedThresholdLow) {
        adaptiveFactor = kBaseSmoothing;
    } else if (speed >= kSpeedThresholdHigh) {
        adaptiveFactor = 0.85;
    } else {
        qreal t = (speed - kSpeedThresholdLow) / (kSpeedThresholdHigh - kSpeedThresholdLow);
        adaptiveFactor = kBaseSmoothing + t * (0.85 - kBaseSmoothing);
    }

    m_smoothedPoint = adaptiveFactor * rawPoint + (1.0 - adaptiveFactor) * m_smoothedPoint;

    if (!m_currentStroke.isEmpty()) {
        QPointF delta = m_smoothedPoint - m_currentStroke.last().position;
        qreal distance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
        if (distance < kMinPointDistance) {
            return;
        }
    }

    LaserPoint point;
    point.position = m_smoothedPoint;
    point.timestamp = QDateTime::currentMSecsSinceEpoch();
    m_currentStroke.append(point);

    emit needsRepaint();
}

void LaserPointerRenderer::stopDrawing()
{
    m_isDrawing = false;
    m_hasSmoothedPoint = false;

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

static void appendCatmullRomSegment(QPainterPath &path,
                                    const QPointF &p0, const QPointF &p1,
                                    const QPointF &p2, const QPointF &p3)
{
    constexpr qreal alpha = 1.0 / 6.0;
    QPointF c1 = p1 + alpha * (p2 - p0);
    QPointF c2 = p2 - alpha * (p3 - p1);
    path.cubicTo(c1, c2, p2);
}

// Helper function to draw a single stroke
static void drawStroke(QPainter &painter, const QVector<LaserPoint> &stroke,
                       const QColor &color, int width, qint64 now, int fadeDuration)
{
    if (stroke.size() < 2) {
        return;
    }

    QVector<QPointF> visiblePoints;
    visiblePoints.reserve(stroke.size());
    qreal totalOpacity = 0.0;
    int visibleCount = 0;

    for (const auto &point : stroke) {
        qint64 age = now - point.timestamp;
        qreal opacity = 1.0 - static_cast<qreal>(age) / fadeDuration;
        opacity = qBound(0.0, opacity, 1.0);

        if (opacity > 0.01) {
            visiblePoints.append(point.position);
            totalOpacity += opacity;
            visibleCount++;
        }
    }

    if (visiblePoints.size() >= 2 && visibleCount > 0) {
        qreal avgOpacity = totalOpacity / visibleCount;

        QPainterPath path;
        path.moveTo(visiblePoints[0]);

        for (int i = 0; i < visiblePoints.size() - 1; ++i) {
            const QPointF p0 = (i == 0)
                ? visiblePoints[0] * 2.0 - visiblePoints[1]
                : visiblePoints[i - 1];
            const QPointF &p1 = visiblePoints[i];
            const QPointF &p2 = visiblePoints[i + 1];
            const QPointF p3 = (i == visiblePoints.size() - 2)
                ? visiblePoints[i + 1] * 2.0 - visiblePoints[i]
                : visiblePoints[i + 2];

            appendCatmullRomSegment(path, p0, p1, p2, p3);
        }

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
