#ifndef PENCILTOOLHANDLER_H
#define PENCILTOOLHANDLER_H

#include "../IToolHandler.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/PencilStroke.h"

#include <QVector>
#include <QPointF>
#include <memory>

/**
 * @brief Tool handler for freehand pencil drawing with real-time smoothing.
 *
 * Uses exponential moving average (EMA) for input smoothing while maintaining
 * zero visual latency by drawing directly to the current cursor position.
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

    // Smoothing state
    QPointF m_smoothedPoint;      // EMA-smoothed position
    QPointF m_smoothedVelocity;   // EMA-smoothed velocity for direction stability
    QPointF m_lastRawPoint;       // Previous raw input for velocity calculation
    bool m_hasSmoothedPoint = false;

    // Minimum distance before committing another point. The spline interpolates
    // every committed point, so dense samples turn the +/-0.5 physical-pixel
    // input quantization into visible wobble; spacing is set in physical pixels
    // and capped in logical pixels so small shapes keep their detail at 100%.
    static constexpr qreal kMinPointDistancePhysical = 6.0;
    static constexpr qreal kMaxMinPointDistanceLogical = 4.0;

    // Smoothing parameters
    // Base smoothing factor (0 = max smooth, 1 = no smooth)
    static constexpr qreal kBaseSmoothing = 0.3;
    // Velocity smoothing factor
    static constexpr qreal kVelocitySmoothing = 0.4;
    // Speed threshold for adaptive smoothing (pixels per event)
    static constexpr qreal kSpeedThresholdLow = 3.0;
    static constexpr qreal kSpeedThresholdHigh = 15.0;
};

#endif // PENCILTOOLHANDLER_H
