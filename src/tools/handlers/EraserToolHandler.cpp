#include "tools/handlers/EraserToolHandler.h"
#include "tools/ToolContext.h"
#include "cursor/CursorStyleCatalog.h"

#include <algorithm>

void EraserToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos)
{
    if (!ctx || !ctx->annotationLayer) {
        return;
    }

    m_isErasing = true;
    m_lastPoint = pos;
    m_currentStrokeErasedItems.clear();
    m_removedOriginalIndices.clear();

    eraseAt(ctx, pos);
    ctx->repaint();
}

void EraserToolHandler::onMouseMove(ToolContext* ctx, const QPoint& pos)
{
    if (m_isErasing) {
        if (pos == m_lastPoint) {
            return;
        }

        eraseAt(ctx, pos);
        m_lastPoint = pos;
        ctx->repaint();
        return;
    }
}

void EraserToolHandler::onMouseRelease(ToolContext* ctx, const QPoint& pos)
{
    if (!m_isErasing) {
        return;
    }

    if (pos != m_lastPoint) {
        eraseAt(ctx, pos);
    }

    if (!m_currentStrokeErasedItems.empty() && ctx) {
        ctx->addItem(std::make_unique<ErasedItemsGroup>(std::move(m_currentStrokeErasedItems)));
        m_currentStrokeErasedItems.clear();
    }

    m_isErasing = false;
    m_removedOriginalIndices.clear();
    m_lastPoint = pos;

    if (ctx) {
        ctx->repaint();
    }
}

void EraserToolHandler::drawPreview(QPainter& painter) const
{
    Q_UNUSED(painter);
}

void EraserToolHandler::cancelDrawing()
{
    m_isErasing = false;
    m_currentStrokeErasedItems.clear();
    m_removedOriginalIndices.clear();
}

QCursor EraserToolHandler::cursor() const
{
    int diameter = std::clamp(m_eraserWidth, kMinWidth, kMaxWidth);
    if (m_cachedCursorWidth != diameter || m_cachedCursor.pixmap().isNull()) {
        m_cachedCursor = CursorStyleCatalog::instance().eraserCursor(diameter);
        m_cachedCursorWidth = diameter;
    }
    return m_cachedCursor;
}

CursorStyleSpec EraserToolHandler::cursorStyleSpec() const
{
    CursorStyleSpec spec;
    spec.styleId = CursorStyleId::EraserBrush;
    spec.primaryValue = std::clamp(m_eraserWidth, kMinWidth, kMaxWidth);
    spec.legacyCursor = cursor();
    return spec;
}

void EraserToolHandler::eraseAt(ToolContext* ctx, const QPoint& pos)
{
    if (!ctx || !ctx->annotationLayer) {
        return;
    }

    int diameter = std::clamp(m_eraserWidth, kMinWidth, kMaxWidth);
    m_eraserWidth = diameter;
    auto removedItems = ctx->annotationLayer->removeItemsIntersecting(pos, diameter);
    if (removedItems.empty()) {
        return;
    }

    for (auto& indexed : removedItems) {
        indexed.originalIndex = mapToOriginalIndex(indexed.originalIndex);
        m_currentStrokeErasedItems.push_back(std::move(indexed));
    }
}

size_t EraserToolHandler::mapToOriginalIndex(size_t currentIndex)
{
    size_t originalIndex = currentIndex;
    for (size_t removedIndex : m_removedOriginalIndices) {
        if (removedIndex <= originalIndex) {
            ++originalIndex;
        } else {
            break;
        }
    }

    auto it = std::lower_bound(m_removedOriginalIndices.begin(),
        m_removedOriginalIndices.end(), originalIndex);
    m_removedOriginalIndices.insert(it, originalIndex);

    return originalIndex;
}
