#ifndef POLYLINETOOLHANDLER_H
#define POLYLINETOOLHANDLER_H

#include "../IToolHandler.h"
#include "annotations/PolylineAnnotation.h"

#include <QPoint>
#include <QElapsedTimer>
#include <memory>

/**
 * @brief Tool handler for polyline (broken line) drawing.
 *
 * Click to add points, double-click to finish.
 * Draws line segments only (no arrowhead).
 */
class PolylineToolHandler : public IToolHandler
{
public:
    PolylineToolHandler() = default;
    ~PolylineToolHandler() override = default;

    ToolId toolId() const override { return ToolId::Polyline; }

    void onActivate(ToolContext* ctx) override;
    void onDeactivate(ToolContext* ctx) override;
    void onMousePress(ToolContext* ctx, const QPoint& pos) override;
    void onMouseMove(ToolContext* ctx, const QPoint& pos) override;
    void onMouseRelease(ToolContext* ctx, const QPoint& pos) override;
    void onDoubleClick(ToolContext* ctx, const QPoint& pos) override;

    void drawPreview(QPainter& painter) const override;
    QRect previewBounds(const ToolContext* ctx) const override;
    bool isDrawing() const override { return m_isDrawing; }
    void cancelDrawing() override;
    bool handleEscape(ToolContext* ctx) override;
    bool handleInteractionPress(ToolContext* ctx, const QPoint& pos, Qt::KeyboardModifiers modifiers) override;
    bool handleInteractionMove(ToolContext* ctx, const QPoint& pos, Qt::KeyboardModifiers modifiers) override;
    bool handleInteractionRelease(ToolContext* ctx, const QPoint& pos, Qt::KeyboardModifiers modifiers) override;
    QRect interactionBounds(const ToolContext* ctx) const override;
    AnnotationInteractionKind activeInteractionKind(const ToolContext* ctx) const override;

    bool supportsColor() const override { return true; }
    bool supportsWidth() const override { return true; }
    bool supportsArrowStyle() const override { return false; }
    bool supportsLineStyle() const override { return true; }

private:
    void finishPolyline(ToolContext* ctx);
    bool canHandleInteraction(ToolContext* ctx) const;
    PolylineAnnotation* selectedPolyline(ToolContext* ctx) const;

    bool m_isDrawing = false;
    std::unique_ptr<PolylineAnnotation> m_currentPolyline;
    QElapsedTimer m_clickTimer;
    static constexpr int DOUBLE_CLICK_INTERVAL = 300;  // ms

    bool m_isPolylineDragging = false;
    int m_activePolylineVertexIndex = -1;
    QPoint m_polylineDragStart;
};

#endif // POLYLINETOOLHANDLER_H
