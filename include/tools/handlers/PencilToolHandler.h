#ifndef PENCILTOOLHANDLER_H
#define PENCILTOOLHANDLER_H

#include "../IToolHandler.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/PencilStroke.h"
#include "tools/handlers/FreehandSmoother.h"

#include <QVector>
#include <QPointF>
#include <memory>

/**
 * @brief Tool handler for freehand pencil drawing with real-time smoothing.
 *
 * Committed points are smoothed and spaced by FreehandSmoother; the stroke's
 * live tail follows the raw cursor so drawing shows without latency.
 */
class PencilToolHandler : public IToolHandler {
public:
    PencilToolHandler() = default;
    ~PencilToolHandler() override = default;

    ToolId toolId() const override { return ToolId::Pencil; }

    void onMousePress(ToolContext* ctx, const QPoint& pos) override;
    void onMousePressF(ToolContext* ctx, const QPointF& pos) override;
    void onMouseMove(ToolContext* ctx, const QPoint& pos) override;
    void onMouseMoveF(ToolContext* ctx, const QPointF& pos) override;
    void onMouseRelease(ToolContext* ctx, const QPoint& pos) override;
    void onMouseReleaseF(ToolContext* ctx, const QPointF& pos) override;

    void drawPreview(QPainter& painter) const override;
    QRect previewBounds() const override;
    bool isDrawing() const override { return m_isDrawing; }
    void cancelDrawing() override;

    bool supportsColor() const override { return true; }
    bool supportsWidth() const override { return true; }
    bool supportsLineStyle() const override { return true; }

private:
    void updateStraightStroke(const QPointF& pos);

    bool m_isDrawing = false;
    // Latch Shift at press so modifier changes cannot reshape an active stroke.
    bool m_straightStroke = false;
    std::unique_ptr<PencilStroke> m_currentStroke;
    QRect m_previewDirtyRect;

    FreehandSmoother m_smoother;
};

#endif // PENCILTOOLHANDLER_H
