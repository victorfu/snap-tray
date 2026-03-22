#include "tools/handlers/PolylineToolHandler.h"
#include "tools/ToolContext.h"
#include "TransformationGizmo.h"
#include "utils/AngleSnap.h"

#include <QPainter>

void PolylineToolHandler::onActivate(ToolContext* ctx)
{
    Q_UNUSED(ctx);
    // Reset state when tool is activated
    m_isDrawing = false;
    m_currentPolyline.reset();
}

void PolylineToolHandler::onDeactivate(ToolContext* ctx)
{
    // Cancel any in-progress drawing when switching tools
    if (m_isDrawing) {
        cancelDrawing();
    }
}

void PolylineToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos)
{
    if (!m_isDrawing) {
        // Start a new polyline
        m_isDrawing = true;
        m_currentPolyline = std::make_unique<PolylineAnnotation>(
            ctx->color, ctx->width, LineEndStyle::None, ctx->lineStyle
        );
        m_currentPolyline->addPoint(pos);
        // Add a second point for the preview line
        m_currentPolyline->addPoint(pos);
        m_clickTimer.start();
    } else {
        if (!m_currentPolyline)
            return;
        // Check for double-click to finish
        if (m_clickTimer.isValid() && m_clickTimer.elapsed() < DOUBLE_CLICK_INTERVAL) {
            // Double-click detected - finish the polyline
            // Apply angle snapping if Shift is held
            QPoint snappedPos = pos;
            if (ctx->shiftPressed && m_currentPolyline->pointCount() >= 2) {
                QPoint lastConfirmed = m_currentPolyline->points().at(m_currentPolyline->pointCount() - 2);
                snappedPos = AngleSnap::snapTo45Degrees(lastConfirmed, pos);
            }
            m_currentPolyline->updateLastPoint(snappedPos);
            finishPolyline(ctx);
            return;
        }

        // Add a new point
        // Apply angle snapping if Shift is held
        QPoint snappedPos = pos;
        if (ctx->shiftPressed && m_currentPolyline->pointCount() >= 2) {
            QPoint lastConfirmed = m_currentPolyline->points().at(m_currentPolyline->pointCount() - 2);
            snappedPos = AngleSnap::snapTo45Degrees(lastConfirmed, pos);
        }
        m_currentPolyline->updateLastPoint(snappedPos);  // Confirm the preview point
        m_currentPolyline->addPoint(snappedPos);  // Add new preview point
        m_clickTimer.restart();
    }

}

void PolylineToolHandler::onMouseMove(ToolContext* ctx, const QPoint& pos)
{
    if (!m_isDrawing || !m_currentPolyline) {
        return;
    }

    // Update the last point (preview) to follow the cursor
    // Apply angle snapping if Shift is held
    QPoint snappedPos = pos;
    if (ctx->shiftPressed && m_currentPolyline->pointCount() >= 2) {
        QPoint lastConfirmed = m_currentPolyline->points().at(m_currentPolyline->pointCount() - 2);
        snappedPos = AngleSnap::snapTo45Degrees(lastConfirmed, pos);
    }
    m_currentPolyline->updateLastPoint(snappedPos);
}

void PolylineToolHandler::onMouseRelease(ToolContext* ctx, const QPoint& pos)
{
    Q_UNUSED(ctx);
    Q_UNUSED(pos);
    // Points are added on press, not release
}

void PolylineToolHandler::onDoubleClick(ToolContext* ctx, const QPoint& pos)
{
    if (!m_isDrawing || !m_currentPolyline) {
        return;
    }

    // Update the last point to the double-click position
    // Apply angle snapping if Shift is held
    QPoint snappedPos = pos;
    if (ctx->shiftPressed && m_currentPolyline->pointCount() >= 2) {
        QPoint lastConfirmed = m_currentPolyline->points().at(m_currentPolyline->pointCount() - 2);
        snappedPos = AngleSnap::snapTo45Degrees(lastConfirmed, pos);
    }
    m_currentPolyline->updateLastPoint(snappedPos);

    finishPolyline(ctx);
}

void PolylineToolHandler::finishPolyline(ToolContext* ctx)
{
    if (!m_currentPolyline) {
        return;
    }

    // Remove the preview point (the last point that follows the cursor)
    m_currentPolyline->removeLastPoint();

    // Only add if we have at least 2 points
    if (m_currentPolyline->pointCount() >= 2) {
        ctx->addItem(std::move(m_currentPolyline));
    }

    // Reset state
    m_isDrawing = false;
    m_currentPolyline.reset();
}

void PolylineToolHandler::drawPreview(QPainter& painter) const
{
    if (m_isDrawing && m_currentPolyline) {
        m_currentPolyline->draw(painter);
    }
}

QRect PolylineToolHandler::previewBounds(const ToolContext* ctx) const
{
    Q_UNUSED(ctx);
    return m_currentPolyline ? m_currentPolyline->boundingRect() : QRect();
}

void PolylineToolHandler::cancelDrawing()
{
    m_isDrawing = false;
    m_currentPolyline.reset();
}

bool PolylineToolHandler::handleEscape(ToolContext* ctx)
{
    if (m_isDrawing) {
        finishPolyline(ctx);
        return true;
    }
    return false;
}

bool PolylineToolHandler::handleInteractionPress(ToolContext* ctx,
                                                 const QPoint& pos,
                                                 Qt::KeyboardModifiers modifiers)
{
    Q_UNUSED(modifiers);

    if (!canHandleInteraction(ctx)) {
        return false;
    }

    auto* layer = ctx->annotationLayer;
    if (auto* polylineItem = selectedPolyline(ctx)) {
        const int vertexIndex = TransformationGizmo::hitTestVertex(polylineItem, pos);
        if (vertexIndex >= 0 || vertexIndex == -1) {
            m_isPolylineDragging = true;
            m_activePolylineVertexIndex = vertexIndex >= 0 ? vertexIndex : -1;
            m_polylineDragStart = pos;
            if (ctx->requestHostFocus) {
                ctx->requestHostFocus();
            }
            return true;
        }
    }

    const int hitIndex = layer->hitTestPolyline(pos);
    if (hitIndex < 0) {
        return false;
    }

    layer->setSelectedIndex(hitIndex);
    if (auto* polylineItem = selectedPolyline(ctx)) {
        m_isPolylineDragging = true;
        m_activePolylineVertexIndex = -1;
        m_polylineDragStart = pos;
        const int vertexIndex = TransformationGizmo::hitTestVertex(polylineItem, pos);
        if (vertexIndex >= 0) {
            m_activePolylineVertexIndex = vertexIndex;
        }
    }

    if (ctx->requestHostFocus) {
        ctx->requestHostFocus();
    }
    return true;
}

bool PolylineToolHandler::handleInteractionMove(ToolContext* ctx,
                                                const QPoint& pos,
                                                Qt::KeyboardModifiers modifiers)
{
    Q_UNUSED(modifiers);

    if (!m_isPolylineDragging || !canHandleInteraction(ctx)) {
        return false;
    }

    auto* polylineItem = selectedPolyline(ctx);
    if (!polylineItem) {
        return false;
    }

    if (m_activePolylineVertexIndex >= 0) {
        polylineItem->setPoint(m_activePolylineVertexIndex, pos);
    } else {
        const QPoint delta = pos - m_polylineDragStart;
        polylineItem->moveBy(delta);
        m_polylineDragStart = pos;
    }

    ctx->annotationLayer->invalidateCache();
    return true;
}

bool PolylineToolHandler::handleInteractionRelease(ToolContext* ctx,
                                                   const QPoint& pos,
                                                   Qt::KeyboardModifiers modifiers)
{
    Q_UNUSED(ctx);
    Q_UNUSED(pos);
    Q_UNUSED(modifiers);

    if (!m_isPolylineDragging) {
        return false;
    }

    m_isPolylineDragging = false;
    m_activePolylineVertexIndex = -1;
    return true;
}

QRect PolylineToolHandler::interactionBounds(const ToolContext* ctx) const
{
    auto* polylineItem = selectedPolyline(const_cast<ToolContext*>(ctx));
    if (!polylineItem) {
        return QRect();
    }

    return TransformationGizmo::visualBounds(polylineItem);
}

AnnotationInteractionKind PolylineToolHandler::activeInteractionKind(const ToolContext* ctx) const
{
    Q_UNUSED(ctx);

    if (!m_isPolylineDragging) {
        return AnnotationInteractionKind::None;
    }

    return m_activePolylineVertexIndex >= 0
        ? AnnotationInteractionKind::SelectedTransform
        : AnnotationInteractionKind::SelectedDrag;
}

bool PolylineToolHandler::canHandleInteraction(ToolContext* ctx) const
{
    return ctx && ctx->annotationLayer;
}

PolylineAnnotation* PolylineToolHandler::selectedPolyline(ToolContext* ctx) const
{
    if (!ctx || !ctx->annotationLayer) {
        return nullptr;
    }
    return dynamic_cast<PolylineAnnotation*>(ctx->annotationLayer->selectedItem());
}
