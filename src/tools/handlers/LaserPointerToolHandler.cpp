#include "tools/handlers/LaserPointerToolHandler.h"
#include "tools/ToolContext.h"

#include <QCursor>

void LaserPointerToolHandler::onActivate(ToolContext* ctx) {
    m_isActive = true;
    ctx->repaint();
}

void LaserPointerToolHandler::onDeactivate(ToolContext* ctx) {
    m_isActive = false;
    ctx->repaint();
}

void LaserPointerToolHandler::onMouseMove(ToolContext* ctx, const QPoint& pos) {
    m_currentPos = pos;
    ctx->repaint();
}

QCursor LaserPointerToolHandler::cursor() const {
    return Qt::BlankCursor;  // Hide cursor, laser pointer replaces it
}
