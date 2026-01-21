#include "tools/handlers/PencilToolHandler.h"
#include "tools/ToolContext.h"

#include <QPainter>

void PencilToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos) {
    m_isDrawing = true;
    m_currentPath.clear();
    m_currentPath.append(QPointF(pos));

    m_currentStroke = std::make_unique<PencilStroke>(
        m_currentPath, ctx->color, ctx->width, ctx->lineStyle
    );

    ctx->repaint();
}

void PencilToolHandler::onMouseMove(ToolContext* ctx, const QPoint& pos) {
    if (!m_isDrawing || !m_currentStroke) {
        return;
    }

    // Filter out points that are too close together (improves smoothness on high DPI)
    if (!m_currentPath.isEmpty()) {
        QPointF lastPoint = m_currentPath.last();
        qreal dx = pos.x() - lastPoint.x();
        qreal dy = pos.y() - lastPoint.y();
        qreal distSq = dx * dx + dy * dy;

        // Minimum distance threshold scaled by device pixel ratio
        qreal minDist = 2.0 * ctx->devicePixelRatio;
        if (distSq < minDist * minDist) {
            return;  // Too close, skip this point
        }
    }

    m_currentPath.append(QPointF(pos));
    m_currentStroke->addPoint(QPointF(pos));

    ctx->repaint();
}

void PencilToolHandler::onMouseRelease(ToolContext* ctx, const QPoint& pos) {
    if (!m_isDrawing) {
        return;
    }

    // Add final point if different from last
    if (m_currentPath.isEmpty() || m_currentPath.last() != QPointF(pos)) {
        if (m_currentStroke) {
            m_currentStroke->addPoint(QPointF(pos));
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

    ctx->repaint();
}

void PencilToolHandler::drawPreview(QPainter& painter) const {
    if (m_isDrawing && m_currentStroke) {
        m_currentStroke->draw(painter);
    }
}

void PencilToolHandler::cancelDrawing() {
    m_isDrawing = false;
    m_currentPath.clear();
    m_currentStroke.reset();
}
