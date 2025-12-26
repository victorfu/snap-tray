#ifndef PENCILTOOLHANDLER_H
#define PENCILTOOLHANDLER_H

#include "../IToolHandler.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/PencilStroke.h"

#include <QVector>
#include <QPointF>
#include <memory>

/**
 * @brief Tool handler for freehand pencil drawing.
 */
class PencilToolHandler : public IToolHandler {
public:
    PencilToolHandler() = default;
    ~PencilToolHandler() override = default;

    ToolId toolId() const override { return ToolId::Pencil; }

    void onMousePress(ToolContext* ctx, const QPoint& pos) override;
    void onMouseMove(ToolContext* ctx, const QPoint& pos) override;
    void onMouseRelease(ToolContext* ctx, const QPoint& pos) override;

    void drawPreview(QPainter& painter) const override;
    bool isDrawing() const override { return m_isDrawing; }
    void cancelDrawing() override;

    bool supportsColor() const override { return true; }
    bool supportsWidth() const override { return true; }

private:
    bool m_isDrawing = false;
    QVector<QPointF> m_currentPath;
    std::unique_ptr<PencilStroke> m_currentStroke;
};

#endif // PENCILTOOLHANDLER_H
