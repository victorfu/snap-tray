#ifndef SHAPETOOLHANDLER_H
#define SHAPETOOLHANDLER_H

#include "../IToolHandler.h"
#include "../../AnnotationLayer.h"

#include <QPoint>
#include <QRect>
#include <memory>

/**
 * @brief Tool handler for shape drawing (rectangle/ellipse).
 *
 * Unified handler that supports both Rectangle and Ellipse shapes
 * with optional fill mode.
 *
 * Shape types: 0 = Rectangle, 1 = Ellipse
 * Fill modes:  0 = Outline, 1 = Filled
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
    void updateCurrentShape(ToolContext* ctx, const QPoint& endPos);

    bool m_isDrawing = false;
    QPoint m_startPoint;
    int m_shapeType = 0;   // 0 = Rectangle, 1 = Ellipse
    int m_fillMode = 0;    // 0 = Outline, 1 = Filled

    std::unique_ptr<RectangleAnnotation> m_currentRectangle;
    std::unique_ptr<EllipseAnnotation> m_currentEllipse;
};

#endif // SHAPETOOLHANDLER_H
