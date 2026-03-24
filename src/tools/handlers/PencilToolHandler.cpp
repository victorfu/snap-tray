#include "tools/handlers/PencilToolHandler.h"
#include "tools/ToolContext.h"

#include <QPainter>
#include <QtMath>

namespace {

QRect tailDirtyRect(const QVector<QPointF>& points, int width, int tailPointCount = 4)
{
    if (points.isEmpty()) {
        return {};
    }

    const int startIndex = qMax(0, points.size() - tailPointCount);
    qreal minX = points[startIndex].x();
    qreal maxX = points[startIndex].x();
    qreal minY = points[startIndex].y();
    qreal maxY = points[startIndex].y();

    for (int i = startIndex + 1; i < points.size(); ++i) {
        const QPointF& point = points[i];
        minX = qMin(minX, point.x());
        maxX = qMax(maxX, point.x());
        minY = qMin(minY, point.y());
        maxY = qMax(maxY, point.y());
    }

    const int margin = width / 2 + 4;
    return QRect(
        static_cast<int>(std::floor(minX)) - margin,
        static_cast<int>(std::floor(minY)) - margin,
        static_cast<int>(std::ceil(maxX - minX)) + margin * 2,
        static_cast<int>(std::ceil(maxY - minY)) + margin * 2);
}

} // namespace

void PencilToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos) {
    m_isDrawing = true;
    m_currentPath.clear();

    QPointF startPoint(pos);
    m_currentPath.append(startPoint);

    // Initialize smoothing state
    m_smoothedPoint = startPoint;
    m_smoothedVelocity = QPointF(0, 0);
    m_lastRawPoint = startPoint;
    m_hasSmoothedPoint = true;

    m_currentStroke = std::make_unique<PencilStroke>(
        m_currentPath, ctx->color, ctx->width, ctx->lineStyle
    );
    m_previewDirtyRect = tailDirtyRect(m_currentPath, ctx->width);

    ctx->repaint();
}

void PencilToolHandler::onMouseMove(ToolContext* ctx, const QPoint& pos) {
    if (!m_isDrawing || !m_currentStroke || !m_hasSmoothedPoint) {
        return;
    }

    QPointF rawPoint(pos);

    // Calculate raw velocity (displacement since last event)
    QPointF rawVelocity = rawPoint - m_lastRawPoint;
    m_lastRawPoint = rawPoint;

    // Smooth the velocity to stabilize direction changes
    m_smoothedVelocity = kVelocitySmoothing * rawVelocity
                       + (1.0 - kVelocitySmoothing) * m_smoothedVelocity;

    // Calculate speed for adaptive smoothing
    qreal speed = qSqrt(rawVelocity.x() * rawVelocity.x()
                      + rawVelocity.y() * rawVelocity.y());

    // Adaptive smoothing: less smoothing at high speed, more at low speed
    // This keeps fast strokes responsive while smoothing slow/shaky movements
    qreal adaptiveFactor;
    if (speed <= kSpeedThresholdLow) {
        adaptiveFactor = kBaseSmoothing;  // Maximum smoothing for slow movement
    } else if (speed >= kSpeedThresholdHigh) {
        adaptiveFactor = 0.85;  // Minimal smoothing for fast movement
    } else {
        // Linear interpolation between thresholds
        qreal t = (speed - kSpeedThresholdLow) / (kSpeedThresholdHigh - kSpeedThresholdLow);
        adaptiveFactor = kBaseSmoothing + t * (0.85 - kBaseSmoothing);
    }

    // Apply EMA smoothing with adaptive factor
    m_smoothedPoint = adaptiveFactor * rawPoint + (1.0 - adaptiveFactor) * m_smoothedPoint;

    // Check minimum distance from last recorded point
    if (!m_currentPath.isEmpty()) {
        QPointF delta = m_smoothedPoint - m_currentPath.last();
        qreal distance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());

        if (distance < kMinPointDistance) {
            return;
        }
    }

    const QRect oldTailBounds = tailDirtyRect(m_currentPath, ctx->width);

    m_currentPath.append(m_smoothedPoint);
    m_currentStroke->addPoint(m_smoothedPoint);
    const QRect newTailBounds = tailDirtyRect(m_currentPath, ctx->width);
    m_previewDirtyRect = oldTailBounds.united(newTailBounds);

    ctx->repaint();
}

void PencilToolHandler::onMouseRelease(ToolContext* ctx, const QPoint& pos) {
    if (!m_isDrawing) {
        return;
    }

    // Add final point (use raw position for accurate endpoint)
    QPointF finalPoint(pos);
    if (m_currentPath.isEmpty() || m_currentPath.last() != finalPoint) {
        m_currentPath.append(finalPoint);
        if (m_currentStroke) {
            m_currentStroke->addPoint(finalPoint);
        }
    }

    // Add to annotation layer if we have a valid stroke
    if (m_currentStroke && m_currentPath.size() >= 2) {
        m_currentStroke->finalize();
        ctx->addItem(std::move(m_currentStroke));
    }

    // Reset state
    m_isDrawing = false;
    m_currentPath.clear();
    m_currentStroke.reset();
    m_hasSmoothedPoint = false;
    m_previewDirtyRect = QRect();

    ctx->repaint();
}

void PencilToolHandler::drawPreview(QPainter& painter) const {
    if (m_isDrawing && m_currentStroke) {
        m_currentStroke->draw(painter);
    }
}

QRect PencilToolHandler::previewBounds() const
{
    if (!m_isDrawing || m_previewDirtyRect.isEmpty()) {
        return QRect();
    }
    return m_previewDirtyRect;
}

void PencilToolHandler::cancelDrawing() {
    m_isDrawing = false;
    m_currentPath.clear();
    m_currentStroke.reset();
    m_hasSmoothedPoint = false;
    m_previewDirtyRect = QRect();
}
