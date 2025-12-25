#ifndef ERASERTOOLHANDLER_H
#define ERASERTOOLHANDLER_H

#include "../IToolHandler.h"
#include "../../AnnotationLayer.h"

#include <QVector>
#include <QPoint>
#include <vector>

/**
 * @brief Tool handler for erasing annotations.
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
    bool isDrawing() const override { return m_isDrawing; }
    void cancelDrawing() override;

    bool supportsColor() const override { return false; }
    bool supportsWidth() const override { return false; }

    QCursor cursor() const override;

    static const int ERASER_WIDTH = 20;

private:
    void eraseAtPoint(ToolContext* ctx, const QPoint& pos);

    bool m_isDrawing = false;
    QVector<QPoint> m_eraserPath;
    std::vector<ErasedItemsGroup::IndexedItem> m_erasedItems;
    QPoint m_lastPoint;
};

#endif // ERASERTOOLHANDLER_H
