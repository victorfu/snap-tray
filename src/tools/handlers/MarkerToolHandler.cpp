#include "tools/handlers/MarkerToolHandler.h"
#include "tools/ToolContext.h"

#include <QPainter>

// Marker uses fixed width
static constexpr int kMarkerWidth = 20;

void MarkerToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos) {
    m_isDrawing = true;
    m_currentPath.clear();
    m_currentPath.append(QPointF(pos));

    m_currentStroke = std::make_unique<MarkerStroke>(
        m_currentPath, ctx->color, kMarkerWidth
    );

    ctx->repaint();
}

void MarkerToolHandler::onMouseMove(ToolContext* ctx, const QPoint& pos) {
    if (!m_isDrawing || !m_currentStroke) {
        return;
    }

    m_currentPath.append(QPointF(pos));
    m_currentStroke->addPoint(QPointF(pos));

    ctx->repaint();
}

void MarkerToolHandler::onMouseRelease(ToolContext* ctx, const QPoint& pos) {
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
        ctx->addItem(std::move(m_currentStroke));
    }

    // Reset state
    m_isDrawing = false;
    m_currentPath.clear();
    m_currentStroke.reset();

    ctx->repaint();
}

void MarkerToolHandler::drawPreview(QPainter& painter) const {
    if (m_isDrawing && m_currentStroke) {
        m_currentStroke->draw(painter);
    }
}

void MarkerToolHandler::cancelDrawing() {
    m_isDrawing = false;
    m_currentPath.clear();
    m_currentStroke.reset();
}
