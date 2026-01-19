#ifndef ARROWTOOLHANDLER_H
#define ARROWTOOLHANDLER_H

#include "../IToolHandler.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/PolylineAnnotation.h"

#include <QPoint>
#include <QElapsedTimer>
#include <memory>

/**
 * @brief Tool handler for arrow/line drawing.
 *
 * Unified behavior:
 * - Drag (press + move + release) = single-segment arrow
 * - Click (press + release without much movement) = polyline mode
 *   - Subsequent clicks add points
 *   - Double-click finishes the polyline
 *   - Last segment displays arrowhead based on current style
 */
class ArrowToolHandler : public IToolHandler {
public:
    ArrowToolHandler() = default;
    ~ArrowToolHandler() override = default;

    ToolId toolId() const override { return ToolId::Arrow; }

    void onActivate(ToolContext* ctx) override;
    void onDeactivate(ToolContext* ctx) override;
    void onMousePress(ToolContext* ctx, const QPoint& pos) override;
    void onMouseMove(ToolContext* ctx, const QPoint& pos) override;
    void onMouseRelease(ToolContext* ctx, const QPoint& pos) override;
    void onDoubleClick(ToolContext* ctx, const QPoint& pos) override;

    void drawPreview(QPainter& painter) const override;
    bool isDrawing() const override { return m_isDrawing || m_isPolylineMode; }
    void cancelDrawing() override;
    bool handleEscape(ToolContext* ctx) override;

    bool supportsColor() const override { return true; }
    bool supportsWidth() const override { return true; }
    bool supportsArrowStyle() const override { return true; }
    bool supportsLineStyle() const override { return true; }

private:
    void finishPolyline(ToolContext* ctx);

    bool m_isDrawing = false;
    bool m_hasDragged = false;      // True if mouse moved significantly during press
    bool m_isPolylineMode = false;  // True when building a multi-point polyline
    QPoint m_startPoint;

    // Arrow mode state (drag-to-draw)
    std::unique_ptr<ArrowAnnotation> m_currentArrow;

    // Polyline mode state (click-to-add-points)
    std::unique_ptr<PolylineAnnotation> m_currentPolyline;
    QElapsedTimer m_clickTimer;
    static constexpr int DOUBLE_CLICK_INTERVAL = 300;  // ms
};

#endif // ARROWTOOLHANDLER_H
