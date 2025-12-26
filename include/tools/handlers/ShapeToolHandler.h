#ifndef SHAPETOOLHANDLER_H
#define SHAPETOOLHANDLER_H

#include "../IToolHandler.h"
#include "../../annotations/ShapeAnnotation.h"

#include <QPoint>
#include <QRect>
#include <memory>

/**
 * @brief Tool handler for shape drawing (rectangle/ellipse).
 *
 * Unified handler that supports both Rectangle and Ellipse shapes
 * with optional fill mode.
 */
class ShapeToolHandler : public IToolHandler {
public:
    ShapeToolHandler() = default;
    ~ShapeToolHandler() override = default;

    ToolId toolId() const override { return ToolId::Shape; }

    void onMousePress(ToolContext* ctx, const QPoint& pos) override;
    void onMouseMove(ToolContext* ctx, const QPoint& pos) override;
    void onMouseRelease(ToolContext* ctx, const QPoint& pos) override;

    void drawPreview(QPainter& painter) const override;
    bool isDrawing() const override { return m_isDrawing; }
    void cancelDrawing() override;

    bool supportsColor() const override { return true; }
    bool supportsWidth() const override { return true; }
    bool supportsShapeType() const override { return true; }
    bool supportsFillMode() const override { return true; }

private:
    QRect makeRect(const QPoint& start, const QPoint& end) const;
    void updateCurrentShape(const QPoint& endPos);

    bool m_isDrawing = false;
    QPoint m_startPoint;
    ShapeType m_shapeType = ShapeType::Rectangle;
    int m_fillMode = 0;    // 0 = Outline, 1 = Filled

    std::unique_ptr<ShapeAnnotation> m_currentShape;
};

#endif // SHAPETOOLHANDLER_H
