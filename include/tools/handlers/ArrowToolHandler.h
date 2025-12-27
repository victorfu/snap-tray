#ifndef ARROWTOOLHANDLER_H
#define ARROWTOOLHANDLER_H

#include "../IToolHandler.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/ArrowAnnotation.h"

#include <QPoint>
#include <memory>

/**
 * @brief Tool handler for arrow/line drawing.
 */
class ArrowToolHandler : public IToolHandler {
public:
    ArrowToolHandler() = default;
    ~ArrowToolHandler() override = default;

    ToolId toolId() const override { return ToolId::Arrow; }

    void onMousePress(ToolContext* ctx, const QPoint& pos) override;
    void onMouseMove(ToolContext* ctx, const QPoint& pos) override;
    void onMouseRelease(ToolContext* ctx, const QPoint& pos) override;

    void drawPreview(QPainter& painter) const override;
    bool isDrawing() const override { return m_isDrawing; }
    void cancelDrawing() override;

    bool supportsColor() const override { return true; }
    bool supportsWidth() const override { return true; }
    bool supportsArrowStyle() const override { return true; }
    bool supportsLineStyle() const override { return true; }

private:
    bool m_isDrawing = false;
    QPoint m_startPoint;
    std::unique_ptr<ArrowAnnotation> m_currentArrow;
};

#endif // ARROWTOOLHANDLER_H
