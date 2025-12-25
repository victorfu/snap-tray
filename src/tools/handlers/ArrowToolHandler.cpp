#include "tools/handlers/ArrowToolHandler.h"
#include "tools/ToolContext.h"

#include <QPainter>

void ArrowToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos) {
    m_isDrawing = true;
    m_startPoint = pos;

    m_currentArrow = std::make_unique<ArrowAnnotation>(
        pos, pos, ctx->color, ctx->width, ctx->arrowStyle
    );

    ctx->repaint();
}

void ArrowToolHandler::onMouseMove(ToolContext* ctx, const QPoint& pos) {
    if (!m_isDrawing || !m_currentArrow) {
        return;
    }

    m_currentArrow->setEnd(pos);
    ctx->repaint();
}

void ArrowToolHandler::onMouseRelease(ToolContext* ctx, const QPoint& pos) {
    if (!m_isDrawing) {
        return;
    }

    if (m_currentArrow) {
        m_currentArrow->setEnd(pos);

        // Only add if the arrow has some length
        QPoint diff = pos - m_startPoint;
        if (diff.manhattanLength() > 5) {
            ctx->addItem(std::move(m_currentArrow));
        }
    }

    // Reset state
    m_isDrawing = false;
    m_currentArrow.reset();

    ctx->repaint();
}

void ArrowToolHandler::drawPreview(QPainter& painter) const {
    if (m_isDrawing && m_currentArrow) {
        m_currentArrow->draw(painter);
    }
}

void ArrowToolHandler::cancelDrawing() {
    m_isDrawing = false;
    m_currentArrow.reset();
}
