#include "tools/handlers/EraserToolHandler.h"
#include "tools/ToolContext.h"

#include <QPainter>
#include <QCursor>

void EraserToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos) {
    m_isDrawing = true;
    m_eraserPath.clear();
    m_erasedItems.clear();
    m_lastPoint = pos;
    m_eraserPath.append(pos);

    eraseAtPoint(ctx, pos);
    ctx->repaint();
}

void EraserToolHandler::onMouseMove(ToolContext* ctx, const QPoint& pos) {
    if (!m_isDrawing) {
        return;
    }

    m_eraserPath.append(pos);
    eraseAtPoint(ctx, pos);
    m_lastPoint = pos;
    ctx->repaint();
}

void EraserToolHandler::onMouseRelease(ToolContext* ctx, const QPoint& pos) {
    if (!m_isDrawing) {
        return;
    }

    eraseAtPoint(ctx, pos);

    // If any items were erased, add an ErasedItemsGroup for undo support
    if (!m_erasedItems.empty() && ctx->annotationLayer) {
        auto group = std::make_unique<ErasedItemsGroup>(std::move(m_erasedItems));
        ctx->addItem(std::move(group));
    }

    // Reset state
    m_isDrawing = false;
    m_eraserPath.clear();
    m_erasedItems.clear();

    ctx->repaint();
}

void EraserToolHandler::drawPreview(QPainter& painter) const {
    if (!m_isDrawing) {
        return;
    }

    // Draw eraser cursor indicator
    painter.save();
    painter.setPen(QPen(Qt::gray, 1, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(m_lastPoint, ERASER_WIDTH / 2, ERASER_WIDTH / 2);
    painter.restore();
}

void EraserToolHandler::cancelDrawing() {
    m_isDrawing = false;
    m_eraserPath.clear();
    m_erasedItems.clear();
}

QCursor EraserToolHandler::cursor() const {
    return Qt::CrossCursor;
}

void EraserToolHandler::eraseAtPoint(ToolContext* ctx, const QPoint& pos) {
    if (!ctx->annotationLayer) {
        return;
    }

    // Remove items that intersect with the eraser
    auto removed = ctx->annotationLayer->removeItemsIntersecting(pos, ERASER_WIDTH);

    // Collect all erased items for undo support
    for (auto& item : removed) {
        m_erasedItems.push_back(std::move(item));
    }
}
