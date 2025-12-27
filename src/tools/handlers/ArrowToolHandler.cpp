#include "tools/handlers/ArrowToolHandler.h"
#include "tools/ToolContext.h"

#include <QPainter>

void ArrowToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos) {
    Q_UNUSED(ctx);
    m_isDrawing = true;
    m_startPoint = pos;
    // Don't create arrow yet - wait until mouse moves
    m_currentArrow.reset();
}

void ArrowToolHandler::onMouseMove(ToolContext* ctx, const QPoint& pos) {
    if (!m_isDrawing) {
        return;
    }

    // Create arrow only when mouse has moved enough
    if (!m_currentArrow) {
        QPoint diff = pos - m_startPoint;
        if (diff.manhattanLength() > 3) {
            m_currentArrow = std::make_unique<ArrowAnnotation>(
                m_startPoint, pos, ctx->color, ctx->width, ctx->arrowStyle, ctx->lineStyle
            );
            ctx->repaint();
        }
    } else {
        m_currentArrow->setEnd(pos);
        ctx->repaint();
    }
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
