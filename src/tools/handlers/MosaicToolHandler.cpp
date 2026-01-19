#include "tools/handlers/MosaicToolHandler.h"
#include "tools/ToolContext.h"
#include "cursor/CursorManager.h"

#include <QPainter>

void MosaicToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos) {
    if (!ctx->sourcePixmap) {
        return;
    }

    m_isDrawing = true;
    m_currentPath.clear();
    m_currentPath.append(pos);

    int brushWidth = ctx->width > 0 ? ctx->width : kDefaultBrushWidth;
    m_currentStroke = std::make_unique<MosaicStroke>(
        m_currentPath,
        ctx->sourcePixmap,  // Pass shared pointer directly
        brushWidth,
        kDefaultBlockSize,
        ctx->mosaicBlurType
    );

    ctx->repaint();
}

void MosaicToolHandler::onMouseMove(ToolContext* ctx, const QPoint& pos) {
    if (!m_isDrawing || !m_currentStroke) {
        return;
    }

    m_currentPath.append(pos);
    m_currentStroke->addPoint(pos);

    ctx->repaint();
}

void MosaicToolHandler::onMouseRelease(ToolContext* ctx, const QPoint& pos) {
    if (!m_isDrawing) {
        return;
    }

    // Add final point
    if (m_currentPath.isEmpty() || m_currentPath.last() != pos) {
        if (m_currentStroke) {
            m_currentStroke->addPoint(pos);
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

void MosaicToolHandler::drawPreview(QPainter& painter) const {
    if (m_isDrawing && m_currentStroke) {
        m_currentStroke->draw(painter);
    }
}

void MosaicToolHandler::cancelDrawing() {
    m_isDrawing = false;
    m_currentPath.clear();
    m_currentStroke.reset();
}

QCursor MosaicToolHandler::cursor() const {
    // Use 2x width for cursor (UI shows half the actual drawing size)
    int effectiveWidth = m_brushWidth * 2;
    if (m_cachedCursorWidth != effectiveWidth || m_cachedCursor.pixmap().isNull()) {
        m_cachedCursor = CursorManager::createMosaicCursor(effectiveWidth);
        m_cachedCursorWidth = effectiveWidth;
    }
    return m_cachedCursor;
}

void MosaicToolHandler::setWidth(int width) {
    m_brushWidth = width > 0 ? width : kDefaultBrushWidth;
}
