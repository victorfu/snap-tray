#include "tools/handlers/PencilToolHandler.h"
#include "tools/ToolContext.h"

#include <QPainter>
#include <QtMath>

namespace {

qreal effectiveMinPointDistance(const ToolContext* ctx, qreal targetPhysicalDistance)
{
    const qreal dpr = (ctx && ctx->devicePixelRatio > 0.0) ? ctx->devicePixelRatio : 1.0;
    return targetPhysicalDistance / dpr;
}

} // namespace

void PencilToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos) {
    onMousePressF(ctx, QPointF(pos));
}

void PencilToolHandler::onMousePressF(ToolContext* ctx, const QPointF& pos) {
    m_isDrawing = true;
    const QVector<QPointF> initialPoints{pos};

    // Initialize smoothing state
    m_smoothedPoint = pos;
    m_smoothedVelocity = QPointF(0, 0);
    m_lastRawPoint = pos;
    m_hasSmoothedPoint = true;

    m_currentStroke = std::make_unique<PencilStroke>(
        initialPoints, ctx->color, ctx->width, ctx->lineStyle
    );
    m_previewDirtyRect = m_currentStroke->previewAffectedBoundingRect();

    ctx->repaint();
}

void PencilToolHandler::onMouseMove(ToolContext* ctx, const QPoint& pos) {
    onMouseMoveF(ctx, QPointF(pos));
}

void PencilToolHandler::onMouseMoveF(ToolContext* ctx, const QPointF& pos) {
    if (!m_isDrawing || !m_currentStroke || !m_hasSmoothedPoint) {
        return;
    }

    const QPointF rawPoint = pos;

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
    const QVector<QPointF>& points = m_currentStroke->points();
    if (!points.isEmpty()) {
        QPointF delta = m_smoothedPoint - points.last();
        qreal distance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());

        if (distance < effectiveMinPointDistance(ctx, kMinPointDistancePhysical)) {
            return;
        }
    }

    const QRect oldTailBounds = m_currentStroke->previewAffectedBoundingRect();

    m_currentStroke->addPoint(m_smoothedPoint);
    const QRect newTailBounds = m_currentStroke->previewAffectedBoundingRect();
    m_previewDirtyRect = oldTailBounds.united(newTailBounds);

    ctx->repaint();
}

void PencilToolHandler::onMouseRelease(ToolContext* ctx, const QPoint& pos) {
    onMouseReleaseF(ctx, QPointF(pos));
}

void PencilToolHandler::onMouseReleaseF(ToolContext* ctx, const QPointF& pos) {
    if (!m_isDrawing) {
        return;
    }

    // Add final point (use raw position for accurate endpoint)
    const QPointF finalPoint = pos;
    if (m_currentStroke &&
        (m_currentStroke->points().isEmpty() || m_currentStroke->points().last() != finalPoint)) {
        m_currentStroke->addPoint(finalPoint);
    }

    // Add to annotation layer if we have a valid stroke
    bool committedStroke = false;
    if (m_currentStroke && m_currentStroke->points().size() >= 2) {
        m_currentStroke->finalize();
        ctx->addItem(std::move(m_currentStroke));
        committedStroke = true;
    }

    // Reset state
    m_isDrawing = false;
    m_currentStroke.reset();
    m_hasSmoothedPoint = false;
    m_previewDirtyRect = QRect();

    if (!committedStroke) {
        ctx->repaint();
    }
}

void PencilToolHandler::drawPreview(QPainter& painter) const {
    if (m_isDrawing && m_currentStroke) {
        m_currentStroke->drawPreview(painter);
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
    m_currentStroke.reset();
    m_hasSmoothedPoint = false;
    m_previewDirtyRect = QRect();
}
