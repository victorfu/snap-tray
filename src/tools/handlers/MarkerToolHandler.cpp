#include "tools/handlers/MarkerToolHandler.h"
#include "tools/ToolContext.h"

#include <QPainter>

// Marker uses fixed width
static constexpr int kMarkerWidth = 20;

namespace {

qreal effectiveMinPointDistance(const ToolContext* ctx, qreal targetPhysicalDistance)
{
    const qreal dpr = (ctx && ctx->devicePixelRatio > 0.0) ? ctx->devicePixelRatio : 1.0;
    return targetPhysicalDistance / dpr;
}

constexpr qreal kMarkerMinPointDistancePhysical = 2.5;

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

void MarkerToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos) {
    onMousePressF(ctx, QPointF(pos));
}

void MarkerToolHandler::onMousePressF(ToolContext* ctx, const QPointF& pos) {
    m_isDrawing = true;
    m_currentPath.clear();
    m_currentPath.append(pos);

    m_currentStroke = std::make_unique<MarkerStroke>(
        m_currentPath, ctx->color, kMarkerWidth
    );
    m_previewDirtyRect = tailDirtyRect(m_currentPath, kMarkerWidth);

    ctx->repaint();
}

void MarkerToolHandler::onMouseMove(ToolContext* ctx, const QPoint& pos) {
    onMouseMoveF(ctx, QPointF(pos));
}

void MarkerToolHandler::onMouseMoveF(ToolContext* ctx, const QPointF& pos) {
    if (!m_isDrawing || !m_currentStroke) {
        return;
    }

    if (!m_currentPath.isEmpty()) {
        const QPointF delta = pos - m_currentPath.last();
        const qreal distance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
        if (distance < effectiveMinPointDistance(ctx, kMarkerMinPointDistancePhysical)) {
            return;
        }
    }

    const QRect oldTailBounds = tailDirtyRect(m_currentPath, kMarkerWidth);

    m_currentPath.append(pos);
    m_currentStroke->addPoint(pos);
    const QRect newTailBounds = tailDirtyRect(m_currentPath, kMarkerWidth);
    m_previewDirtyRect = oldTailBounds.united(newTailBounds);

    ctx->repaint();
}

void MarkerToolHandler::onMouseRelease(ToolContext* ctx, const QPoint& pos) {
    onMouseReleaseF(ctx, QPointF(pos));
}

void MarkerToolHandler::onMouseReleaseF(ToolContext* ctx, const QPointF& pos) {
    if (!m_isDrawing) {
        return;
    }

    // Add final point if different from last
    if (m_currentPath.isEmpty() || m_currentPath.last() != pos) {
        m_currentPath.append(pos);
        if (m_currentStroke) {
            m_currentStroke->addPoint(pos);
        }
    }

    // Add to annotation layer if we have a valid stroke
    if (m_currentStroke && m_currentStroke->points().size() >= 2) {
        ctx->addItem(std::move(m_currentStroke));
    }

    // Reset state
    m_isDrawing = false;
    m_currentPath.clear();
    m_currentStroke.reset();
    m_previewDirtyRect = QRect();

    ctx->repaint();
}

void MarkerToolHandler::drawPreview(QPainter& painter) const {
    if (m_isDrawing && m_currentStroke) {
        m_currentStroke->drawPreview(painter);
    }
}

QRect MarkerToolHandler::previewBounds() const
{
    if (!m_isDrawing || m_previewDirtyRect.isEmpty()) {
        return QRect();
    }
    return m_previewDirtyRect;
}

void MarkerToolHandler::cancelDrawing() {
    m_isDrawing = false;
    m_currentPath.clear();
    m_currentStroke.reset();
    m_previewDirtyRect = QRect();
}
