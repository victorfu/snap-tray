#include "tools/handlers/MarkerToolHandler.h"
#include "tools/ToolContext.h"

#include <QPainter>

// Marker uses fixed width
static constexpr int kMarkerWidth = 20;

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

void MarkerToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos) {
    onMousePressF(ctx, QPointF(pos));
}

void MarkerToolHandler::onMousePressF(ToolContext* ctx, const QPointF& pos) {
    m_isDrawing = true;
    m_straightStroke = ctx->shiftPressed;
    m_currentPath.clear();
    m_currentPath.append(pos);
    m_smoother.reset(pos);
    m_hasLivePoint = false;

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

    if (m_straightStroke) {
        updateStraightStroke(pos);
        ctx->repaint();
        return;
    }

    const QRect oldTailBounds = liveTailDirtyRect();

    const QPointF smoothedPoint = m_smoother.smooth(pos);
    if (FreehandSmoother::shouldCommit(m_currentPath.last(), smoothedPoint, ctx->devicePixelRatio)) {
        m_currentPath.append(smoothedPoint);
        m_currentStroke->addPoint(smoothedPoint);
    }
    // Repaint on every move: the live tail reaches the raw cursor even when no
    // point was committed, so the stroke never trails behind it.
    m_livePoint = pos;
    m_hasLivePoint = true;
    m_currentStroke->setLivePoint(pos);

    m_previewDirtyRect = oldTailBounds.united(liveTailDirtyRect());
    ctx->repaint();
}

void MarkerToolHandler::onMouseRelease(ToolContext* ctx, const QPoint& pos) {
    onMouseReleaseF(ctx, QPointF(pos));
}

void MarkerToolHandler::onMouseReleaseF(ToolContext* ctx, const QPointF& pos) {
    if (!m_isDrawing) {
        return;
    }

    // End exactly where the live tail ended. A tiny terminal segment shows as a
    // bump under the wide round cap, so a close release moves the last point.
    if (m_straightStroke && m_currentStroke) {
        updateStraightStroke(pos);
    } else if (m_currentStroke && pos != m_currentPath.last()) {
        if (m_currentPath.size() >= 2 &&
            !FreehandSmoother::shouldCommit(m_currentPath.last(), pos, ctx->devicePixelRatio)) {
            m_currentPath.last() = pos;
            m_currentStroke->moveLastPoint(pos);
        } else {
            m_currentPath.append(pos);
            m_currentStroke->addPoint(pos);
        }
    }
    if (m_currentStroke) {
        m_currentStroke->clearLivePoint();
    }

    // Add to annotation layer if we have a valid stroke
    bool committedStroke = false;
    if (m_currentStroke && m_currentStroke->points().size() >= 2) {
        ctx->addItem(std::move(m_currentStroke));
        committedStroke = true;
    }

    // Reset state
    m_isDrawing = false;
    m_straightStroke = false;
    m_currentPath.clear();
    m_currentStroke.reset();
    m_previewDirtyRect = QRect();
    m_hasLivePoint = false;

    if (!committedStroke) {
        ctx->repaint();
    }
}

void MarkerToolHandler::updateStraightStroke(const QPointF& pos)
{
    const QRect oldBounds = tailDirtyRect(m_currentPath, kMarkerWidth);
    const QPointF start = m_currentPath.first();
    m_currentPath = {start};
    if (pos != start) {
        m_currentPath.append(pos);
    }
    m_currentStroke = std::make_unique<MarkerStroke>(
        m_currentPath, m_currentStroke->color(), m_currentStroke->width());
    m_previewDirtyRect = oldBounds.united(tailDirtyRect(m_currentPath, kMarkerWidth));
}

QRect MarkerToolHandler::liveTailDirtyRect() const
{
    // The preview tail spans the last committed points plus the live cursor.
    QVector<QPointF> tail = m_currentPath.mid(qMax(0, m_currentPath.size() - 4));
    if (m_hasLivePoint) {
        tail.append(m_livePoint);
    }
    return tailDirtyRect(tail, kMarkerWidth);
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
    m_hasLivePoint = false;
    m_straightStroke = false;
    m_currentPath.clear();
    m_currentStroke.reset();
    m_previewDirtyRect = QRect();
}
