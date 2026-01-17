#include "tools/handlers/SelectionToolHandler.h"
#include "tools/ToolContext.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/AnnotationLayer.h"

#include <QCursor>

ArrowAnnotation* SelectionToolHandler::findArrowAt(ToolContext* ctx, const QPoint& pos)
{
    if (!ctx || !ctx->annotationLayer) {
        return nullptr;
    }

    // Iterate through annotations to find an arrow at this position
    // Check in reverse order (top-most first)
    AnnotationLayer* layer = ctx->annotationLayer;
    for (int i = static_cast<int>(layer->itemCount()) - 1; i >= 0; --i) {
        AnnotationItem* item = layer->itemAt(i);
        if (ArrowAnnotation* arrow = dynamic_cast<ArrowAnnotation*>(item)) {
            if (arrow->containsPoint(pos)) {
                return arrow;
            }
        }
    }

    return nullptr;
}

void SelectionToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos)
{
    if (!ctx) return;

    m_lastMousePos = pos;

    // 1. If we have a selected arrow, check if clicking on one of its handles
    if (m_selectedArrow) {
        GizmoHandle handle = TransformationGizmo::hitTest(m_selectedArrow, pos);

        if (handle != GizmoHandle::None) {
            // Start dragging this handle
            m_activeHandle = handle;
            m_isDragging = true;
            return;
        }
    }

    // 2. Check if clicking on a different arrow
    ArrowAnnotation* clickedArrow = findArrowAt(ctx, pos);

    if (clickedArrow) {
        // Select this arrow
        m_selectedArrow = clickedArrow;
        m_activeHandle = GizmoHandle::Body;  // Default to body drag
        m_isDragging = true;
        ctx->repaint();
        return;
    }

    // 3. Clicked on empty space - deselect
    if (m_selectedArrow) {
        m_selectedArrow = nullptr;
        m_activeHandle = GizmoHandle::None;
        ctx->repaint();
    }
}

void SelectionToolHandler::onMouseMove(ToolContext* ctx, const QPoint& pos)
{
    if (!ctx || !m_isDragging || !m_selectedArrow) {
        return;
    }

    QPoint delta = pos - m_lastMousePos;
    m_lastMousePos = pos;

    switch (m_activeHandle) {
    case GizmoHandle::ArrowStart:
        // Move start point (control point adjusts automatically in setStart)
        m_selectedArrow->setStart(m_selectedArrow->start() + delta);
        break;

    case GizmoHandle::ArrowEnd:
        // Move end point (control point adjusts automatically in setEnd)
        m_selectedArrow->setEnd(m_selectedArrow->end() + delta);
        break;

    case GizmoHandle::ArrowControl:
        // Move control point directly for curve adjustment
        m_selectedArrow->setControlPoint(pos);
        break;

    case GizmoHandle::Body:
        // Move entire arrow
        m_selectedArrow->moveBy(delta);
        break;

    default:
        // Other handles not applicable to arrows
        break;
    }

    // Invalidate cache and request repaint
    if (ctx->annotationLayer) {
        ctx->annotationLayer->invalidateCache();
    }
    ctx->repaint();
}

void SelectionToolHandler::onMouseRelease(ToolContext* ctx, const QPoint& pos)
{
    Q_UNUSED(ctx);
    Q_UNUSED(pos);

    m_isDragging = false;
    // Keep selection and handle state for continued editing
}

QCursor SelectionToolHandler::cursor() const
{
    if (m_isDragging) {
        return Qt::ClosedHandCursor;
    }

    // Could extend to show different cursors based on hover state
    return Qt::ArrowCursor;
}
