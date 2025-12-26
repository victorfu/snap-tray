#ifndef MARKERTOOLHANDLER_H
#define MARKERTOOLHANDLER_H

#include "../IToolHandler.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/MarkerStroke.h"

#include <QVector>
#include <QPointF>
#include <memory>

/**
 * @brief Tool handler for semi-transparent marker/highlighter.
 */
class MarkerToolHandler : public IToolHandler {
public:
    MarkerToolHandler() = default;
    ~MarkerToolHandler() override = default;

    ToolId toolId() const override { return ToolId::Marker; }

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
    std::unique_ptr<MarkerStroke> m_currentStroke;
};

#endif // MARKERTOOLHANDLER_H
