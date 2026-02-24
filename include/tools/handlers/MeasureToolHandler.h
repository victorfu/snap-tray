#ifndef MEASURETOOLHANDLER_H
#define MEASURETOOLHANDLER_H

#include "../IToolHandler.h"
#include <QPoint>

class TestMeasureToolHandler;

class MeasureToolHandler : public IToolHandler {
public:
    MeasureToolHandler() = default;
    ~MeasureToolHandler() override = default;

    ToolId toolId() const override { return ToolId::Measure; }

    void onActivate(ToolContext* ctx) override;
    void onDeactivate(ToolContext* ctx) override;
    void onMousePress(ToolContext* ctx, const QPoint& pos) override;
    void onMouseMove(ToolContext* ctx, const QPoint& pos) override;
    void onMouseRelease(ToolContext* ctx, const QPoint& pos) override;

    void drawPreview(QPainter& painter) const override;
    bool isDrawing() const override { return m_isDrawing; }
    void cancelDrawing() override;
    bool handleEscape(ToolContext* ctx) override;

    QCursor cursor() const override { return Qt::CrossCursor; }

private:
    friend class TestMeasureToolHandler;

    QPoint constrainEndpoint(const QPoint& start, const QPoint& end, bool shiftHeld) const;
    void drawMeasurementOverlay(QPainter& painter, const QPoint& p1, const QPoint& p2) const;

    bool m_isDrawing = false;
    QPoint m_startPoint;
    QPoint m_endPoint;

    bool m_hasCompletedMeasurement = false;
    QPoint m_completedStart;
    QPoint m_completedEnd;

    static constexpr int kEndpointRadius = 4;
    static constexpr int kMinMeasureDistance = 3;
    static constexpr int kLabelFontSize = 12;
    static constexpr int kLabelOffsetY = 20;
    static constexpr int kLabelPaddingH = 6;
    static constexpr int kLabelPaddingV = 2;
    static constexpr int kLabelRadius = 4;
};

#endif // MEASURETOOLHANDLER_H
