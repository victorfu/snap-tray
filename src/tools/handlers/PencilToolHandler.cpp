#include "tools/handlers/PencilToolHandler.h"
#include "tools/ToolContext.h"

#include <QPainter>

void PencilToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos) {
    onMousePressF(ctx, QPointF(pos));
}

void PencilToolHandler::onMousePressF(ToolContext* ctx, const QPointF& pos) {
    m_isDrawing = true;
    m_straightStroke = ctx->shiftPressed;
    const QVector<QPointF> initialPoints{pos};

    m_smoother.reset(pos);

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
    if (!m_isDrawing || !m_currentStroke) {
        return;
    }

    if (m_straightStroke) {
        updateStraightStroke(pos);
        ctx->repaint();
        return;
    }

    const QRect oldTailBounds = m_currentStroke->previewAffectedBoundingRect();

    const QPointF smoothedPoint = m_smoother.smooth(pos);
    if (FreehandSmoother::shouldCommit(
            m_currentStroke->points().last(), smoothedPoint, ctx->devicePixelRatio)) {
        m_currentStroke->addPoint(smoothedPoint);
    }
    // Repaint on every move: the live tail reaches the raw cursor even when no
    // point was committed, so the stroke never trails behind it.
    m_currentStroke->setLivePoint(pos);

    m_previewDirtyRect = oldTailBounds.united(m_currentStroke->previewAffectedBoundingRect());
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
    if (m_straightStroke && m_currentStroke) {
        updateStraightStroke(finalPoint);
    } else if (m_currentStroke &&
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
    m_straightStroke = false;
    m_currentStroke.reset();
    m_previewDirtyRect = QRect();

    if (!committedStroke) {
        ctx->repaint();
    }
}

void PencilToolHandler::updateStraightStroke(const QPointF& pos)
{
    const QRect oldBounds = m_currentStroke->previewAffectedBoundingRect();
    const QPointF start = m_currentStroke->points().first();
    QVector<QPointF> points{start};
    if (pos != start) {
        points.append(pos);
    }
    // Rebuild only the two endpoints, retaining the style captured at press.
    m_currentStroke = std::make_unique<PencilStroke>(
        points, m_currentStroke->color(), m_currentStroke->width(), m_currentStroke->lineStyle());
    m_previewDirtyRect = oldBounds.united(m_currentStroke->previewAffectedBoundingRect());
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
    m_straightStroke = false;
    m_currentStroke.reset();
    m_previewDirtyRect = QRect();
}
