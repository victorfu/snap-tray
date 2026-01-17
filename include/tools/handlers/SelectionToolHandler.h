#ifndef SELECTIONTOOLHANDLER_H
#define SELECTIONTOOLHANDLER_H

#include "../IToolHandler.h"
#include "TransformationGizmo.h"

class ArrowAnnotation;

/**
 * @brief Tool handler for selection mode.
 *
 * This tool allows selecting and manipulating existing annotations:
 * - For arrows: drag start/end/control handles to reshape, or drag body to move
 * - For text/emoji: handled by parent widget (existing behavior)
 */
class SelectionToolHandler : public IToolHandler {
public:
    SelectionToolHandler() = default;
    ~SelectionToolHandler() override = default;

    ToolId toolId() const override { return ToolId::Selection; }

    void onMousePress(ToolContext* ctx, const QPoint& pos) override;
    void onMouseMove(ToolContext* ctx, const QPoint& pos) override;
    void onMouseRelease(ToolContext* ctx, const QPoint& pos) override;

    bool isDrawing() const override { return m_isDragging; }

    bool supportsColor() const override { return false; }
    bool supportsWidth() const override { return false; }

    QCursor cursor() const override;

    // Arrow selection state (accessible for gizmo drawing)
    ArrowAnnotation* selectedArrow() const { return m_selectedArrow; }
    void clearArrowSelection() { m_selectedArrow = nullptr; m_activeHandle = GizmoHandle::None; }

private:
    // Find an ArrowAnnotation at the given position
    ArrowAnnotation* findArrowAt(ToolContext* ctx, const QPoint& pos);

    // Selection state
    ArrowAnnotation* m_selectedArrow = nullptr;
    GizmoHandle m_activeHandle = GizmoHandle::None;

    // Drag state
    bool m_isDragging = false;
    QPoint m_lastMousePos;
};

#endif // SELECTIONTOOLHANDLER_H
