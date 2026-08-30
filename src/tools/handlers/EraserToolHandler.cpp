#include "tools/handlers/EraserToolHandler.h"
#include "tools/ToolContext.h"
#include "cursor/CursorStyleCatalog.h"

#include <algorithm>

EraserToolHandler::~EraserToolHandler()
{
    cancelDrawing();
}

void EraserToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos)
{
    if (!ctx || !ctx->annotationLayer) {
        return;
    }

    if (m_isErasing) {
        cancelDrawing();
    }

    m_isErasing = true;
    m_activeLayer = ctx->annotationLayer;
    m_activeLayer->beginEraseTransaction();
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

    QPointer<AnnotationLayer> activeLayer = m_activeLayer;
    const bool canCommit = activeLayer && ctx && ctx->annotationLayer == activeLayer;
    const bool committed = canCommit
        && activeLayer->commitEraseTransaction(std::move(m_currentStrokeErasedItems));
    if (!committed && activeLayer) {
        activeLayer->cancelEraseTransaction(std::move(m_currentStrokeErasedItems));
    }

    m_isErasing = false;
    m_activeLayer.clear();
    m_currentStrokeErasedItems.clear();
    m_removedOriginalIndices.clear();
    m_lastPoint = pos;

    if (ctx) {
        ctx->repaint();
    }
}

void EraserToolHandler::onDeactivate(ToolContext* ctx)
{
    Q_UNUSED(ctx);
    cancelDrawing();
}

void EraserToolHandler::drawPreview(QPainter& painter) const
{
    Q_UNUSED(painter);
}

void EraserToolHandler::cancelDrawing()
{
    QPointer<AnnotationLayer> activeLayer = m_activeLayer;
    if (activeLayer) {
        activeLayer->cancelEraseTransaction(std::move(m_currentStrokeErasedItems));
    }

    m_isErasing = false;
    m_activeLayer.clear();
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
    if (!ctx || !ctx->annotationLayer || !m_activeLayer ||
        ctx->annotationLayer != m_activeLayer ||
        !m_activeLayer->isHistoryLocked()) {
        return;
    }

    int diameter = std::clamp(m_eraserWidth, kMinWidth, kMaxWidth);
    m_eraserWidth = diameter;
    auto removedItems = ctx->annotationLayer->removeItemsIntersecting(pos, diameter);
    if (removedItems.empty()) {
        return;
    }

    if (removedItems.size()
            > m_currentStrokeErasedItems.max_size() - m_currentStrokeErasedItems.size()
        || removedItems.size()
            > m_removedOriginalIndices.max_size() - m_removedOriginalIndices.size()) {
        ctx->annotationLayer->restoreRemovedItems(std::move(removedItems));
        return;
    }

    const size_t erasedRequired =
        m_currentStrokeErasedItems.size() + removedItems.size();
    const size_t indicesRequired =
        m_removedOriginalIndices.size() + removedItems.size();
    const auto geometricCapacity = [](size_t required,
                                      size_t capacity,
                                      size_t maxSize) {
        if (required <= capacity) {
            return capacity;
        }
        const size_t doubled = capacity > maxSize / 2
            ? maxSize
            : capacity * 2;
        return (std::max)(required, doubled);
    };

    try {
        // Guarantee all aggregation storage before moving ownership out of the
        // layer-returned batch. Geometric growth preserves amortized O(N)
        // behavior when a gesture erases one item per mouse sample.
        if (erasedRequired > m_currentStrokeErasedItems.capacity()) {
            m_currentStrokeErasedItems.reserve(geometricCapacity(
                erasedRequired,
                m_currentStrokeErasedItems.capacity(),
                m_currentStrokeErasedItems.max_size()));
        }
        if (indicesRequired > m_removedOriginalIndices.capacity()) {
            m_removedOriginalIndices.reserve(geometricCapacity(
                indicesRequired,
                m_removedOriginalIndices.capacity(),
                m_removedOriginalIndices.max_size()));
        }
    } catch (...) {
        ctx->annotationLayer->restoreRemovedItems(std::move(removedItems));
        return;
    }

    // All indices returned by one layer query refer to the same pre-query
    // ordering. Map the whole batch before recording any of its removals.
    for (auto& indexed : removedItems) {
        indexed.originalIndex = mapToOriginalIndex(indexed.originalIndex);
    }
    for (auto& indexed : removedItems) {
        m_removedOriginalIndices.push_back(indexed.originalIndex);
        m_currentStrokeErasedItems.push_back(std::move(indexed));
    }

    std::sort(m_removedOriginalIndices.begin(), m_removedOriginalIndices.end());
}

size_t EraserToolHandler::mapToOriginalIndex(size_t currentIndex) const
{
    size_t originalIndex = currentIndex;
    for (size_t removedIndex : m_removedOriginalIndices) {
        if (removedIndex <= originalIndex) {
            ++originalIndex;
        } else {
            break;
        }
    }

    return originalIndex;
}
