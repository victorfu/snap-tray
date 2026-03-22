#include "tools/handlers/ShapeToolHandler.h"
#include "tools/ToolContext.h"
#include "annotations/AnnotationLayer.h"
#include "region/ShapeAnnotationEditor.h"
#include "TransformationGizmo.h"

#include <QPainter>

// Shape types: 0 = Rectangle, 1 = Ellipse
// Fill modes:  0 = Outline, 1 = Filled
static constexpr int kShapeRectangle = 0;
static constexpr int kFillFilled = 1;

void ShapeToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos) {
    m_isDrawing = true;
    m_startPoint = pos;
    m_shapeType = (ctx->shapeType == kShapeRectangle) ? ShapeType::Rectangle : ShapeType::Ellipse;
    m_fillMode = ctx->shapeFillMode;

    bool filled = (m_fillMode == kFillFilled);
    QRect rect(pos, pos);

    m_currentShape = std::make_unique<ShapeAnnotation>(
        rect, m_shapeType, ctx->color, ctx->width, filled
    );

}

void ShapeToolHandler::onMouseMove(ToolContext* ctx, const QPoint& pos) {
    if (!m_isDrawing) {
        return;
    }

    updateCurrentShape(pos);
}

void ShapeToolHandler::onMouseRelease(ToolContext* ctx, const QPoint& pos) {
    if (!m_isDrawing) {
        return;
    }

    updateCurrentShape(pos);

    QRect rect = makeRect(m_startPoint, pos);

    // Only add if the shape has some size
    if (rect.width() > 5 || rect.height() > 5) {
        if (m_currentShape) {
            ctx->addItem(std::move(m_currentShape));
            if (ctx && ctx->annotationLayer && ctx->annotationLayer->itemCount() > 0) {
                int newIndex = static_cast<int>(ctx->annotationLayer->itemCount() - 1);
                ctx->annotationLayer->setSelectedIndex(newIndex);
            }
        }
    }

    // Reset state
    m_isDrawing = false;
    m_currentShape.reset();

}

bool ShapeToolHandler::handleEscape(ToolContext* ctx)
{
    if (!ctx || !ctx->shapeAnnotationEditor) {
        return false;
    }

    if (ctx->shapeAnnotationEditor->isTransforming()) {
        ctx->shapeAnnotationEditor->finishTransformation();
        return true;
    }

    if (ctx->shapeAnnotationEditor->isDragging()) {
        ctx->shapeAnnotationEditor->finishDragging();
        return true;
    }

    return false;
}

void ShapeToolHandler::drawPreview(QPainter& painter) const {
    if (!m_isDrawing || !m_currentShape) {
        return;
    }

    m_currentShape->draw(painter);
}

QRect ShapeToolHandler::previewBounds(const ToolContext* ctx) const
{
    Q_UNUSED(ctx);
    return m_currentShape ? m_currentShape->boundingRect() : QRect();
}

void ShapeToolHandler::cancelDrawing() {
    m_isDrawing = false;
    m_currentShape.reset();
}

bool ShapeToolHandler::handleInteractionPress(ToolContext* ctx,
                                             const QPoint& pos,
                                             Qt::KeyboardModifiers modifiers)
{
    Q_UNUSED(modifiers);

    if (!canHandle(ctx)) {
        return false;
    }

    auto* layer = ctx->annotationLayer;
    auto* shapeEditor = ctx->shapeAnnotationEditor;

    auto* selectedShape = dynamic_cast<ShapeAnnotation*>(layer->selectedItem());
    if (selectedShape) {
        GizmoHandle handle = TransformationGizmo::hitTest(selectedShape, pos);
        if (handle != GizmoHandle::None) {
            if (handle == GizmoHandle::Body) {
                shapeEditor->startDragging(pos);
            } else {
                shapeEditor->startTransformation(pos, handle);
            }

            if (ctx->requestHostFocus) {
                ctx->requestHostFocus();
            }
            return true;
        }
    }

    const int hitIndex = layer->hitTestShape(pos);
    if (hitIndex < 0) {
        return false;
    }

    layer->setSelectedIndex(hitIndex);
    auto* hitShape = dynamic_cast<ShapeAnnotation*>(layer->selectedItem());
    if (!hitShape) {
        return false;
    }

    const GizmoHandle handle = TransformationGizmo::hitTest(hitShape, pos);
    if (handle == GizmoHandle::Body || handle == GizmoHandle::None) {
        shapeEditor->startDragging(pos);
    } else {
        shapeEditor->startTransformation(pos, handle);
    }

    if (ctx->requestHostFocus) {
        ctx->requestHostFocus();
    }
    return true;
}

bool ShapeToolHandler::handleInteractionMove(ToolContext* ctx,
                                            const QPoint& pos,
                                            Qt::KeyboardModifiers modifiers)
{
    Q_UNUSED(modifiers);

    if (!canHandle(ctx)) {
        return false;
    }

    auto* layer = ctx->annotationLayer;
    auto* shapeEditor = ctx->shapeAnnotationEditor;

    if (shapeEditor->isTransforming() && layer->selectedIndex() >= 0) {
        shapeEditor->updateTransformation(pos);
        return true;
    }

    if (shapeEditor->isDragging() && layer->selectedIndex() >= 0) {
        shapeEditor->updateDragging(pos);
        return true;
    }

    return false;
}

bool ShapeToolHandler::handleInteractionRelease(ToolContext* ctx,
                                               const QPoint& pos,
                                               Qt::KeyboardModifiers modifiers)
{
    Q_UNUSED(pos);
    Q_UNUSED(modifiers);

    if (!canHandle(ctx)) {
        return false;
    }

    auto* shapeEditor = ctx->shapeAnnotationEditor;
    if (shapeEditor->isTransforming()) {
        shapeEditor->finishTransformation();
        return true;
    }

    if (shapeEditor->isDragging()) {
        shapeEditor->finishDragging();
        return true;
    }

    return false;
}

QRect ShapeToolHandler::interactionBounds(const ToolContext* ctx) const
{
    if (!ctx || !ctx->annotationLayer) {
        return QRect();
    }

    auto* shapeItem = dynamic_cast<ShapeAnnotation*>(ctx->annotationLayer->selectedItem());
    if (!shapeItem) {
        return QRect();
    }

    return TransformationGizmo::visualBounds(shapeItem);
}

AnnotationInteractionKind ShapeToolHandler::activeInteractionKind(const ToolContext* ctx) const
{
    if (!ctx || !ctx->shapeAnnotationEditor) {
        return AnnotationInteractionKind::None;
    }

    if (ctx->shapeAnnotationEditor->isDragging()) {
        return AnnotationInteractionKind::SelectedDrag;
    }

    if (ctx->shapeAnnotationEditor->isTransforming()) {
        return AnnotationInteractionKind::SelectedTransform;
    }

    return AnnotationInteractionKind::None;
}

QRect ShapeToolHandler::makeRect(const QPoint& start, const QPoint& end) const {
    return QRect(
        qMin(start.x(), end.x()),
        qMin(start.y(), end.y()),
        qAbs(end.x() - start.x()),
        qAbs(end.y() - start.y())
    );
}

void ShapeToolHandler::updateCurrentShape(const QPoint& endPos) {
    if (m_currentShape) {
        QRect rect = makeRect(m_startPoint, endPos);
        m_currentShape->setRect(rect);
    }
}

bool ShapeToolHandler::canHandle(ToolContext* ctx) const
{
    return ctx && ctx->annotationLayer && ctx->shapeAnnotationEditor;
}
