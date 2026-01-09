#ifndef ERASERTOOLHANDLER_H
#define ERASERTOOLHANDLER_H

#include "../IToolHandler.h"
#include "annotations/ErasedItemsGroup.h"

#include <QPoint>
#include <QCursor>
#include <vector>

/**
 * @brief Tool handler for object-based erasing (delete whole annotations).
 */
class EraserToolHandler : public IToolHandler {
public:
    EraserToolHandler() = default;
    ~EraserToolHandler() override = default;

    ToolId toolId() const override { return ToolId::Eraser; }

    void onMousePress(ToolContext* ctx, const QPoint& pos) override;
    void onMouseMove(ToolContext* ctx, const QPoint& pos) override;
    void onMouseRelease(ToolContext* ctx, const QPoint& pos) override;

    void drawPreview(QPainter& painter) const override;
    bool isDrawing() const override { return m_isErasing; }
    void cancelDrawing() override;

    QCursor cursor() const override;

private:
    static constexpr int kMinWidth = 5;
    static constexpr int kMaxWidth = 100;
    static constexpr int kDefaultWidth = 20;

    void eraseAt(ToolContext* ctx, const QPoint& pos);
    size_t mapToOriginalIndex(size_t currentIndex);

    bool m_isErasing = false;
    QPoint m_lastPoint;
    int m_eraserWidth = kDefaultWidth;

    mutable QCursor m_cachedCursor;
    mutable int m_cachedCursorWidth = 0;

    std::vector<ErasedItemsGroup::IndexedItem> m_currentStrokeErasedItems;
    std::vector<size_t> m_removedOriginalIndices;
};

#endif // ERASERTOOLHANDLER_H
