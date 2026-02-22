#include "region/ShapeAnnotationEditor.h"

#include "annotations/AnnotationLayer.h"
#include "annotations/ShapeAnnotation.h"

#include <QtMath>

namespace {
qreal normalizeAngleDelta(qreal deltaDegrees)
{
    while (deltaDegrees > 180.0) {
        deltaDegrees -= 360.0;
    }
    while (deltaDegrees < -180.0) {
        deltaDegrees += 360.0;
    }
    return deltaDegrees;
}
} // namespace

void ShapeAnnotationEditor::setAnnotationLayer(AnnotationLayer* layer)
{
    m_annotationLayer = layer;
}

ShapeAnnotation* ShapeAnnotationEditor::selectedShape() const
{
    if (!m_annotationLayer) {
        return nullptr;
    }
    return dynamic_cast<ShapeAnnotation*>(m_annotationLayer->selectedItem());
}

void ShapeAnnotationEditor::startTransformation(const QPoint& pos, GizmoHandle handle)
{
    ShapeAnnotation* shapeItem = selectedShape();
    if (!shapeItem) {
        return;
    }

    m_isTransforming = true;
    m_activeGizmoHandle = handle;
    m_transformStartCenter = shapeItem->center();
    m_transformStartRotation = shapeItem->rotation();
    m_transformStartScaleX = shapeItem->scaleX();
    m_transformStartScaleY = shapeItem->scaleY();

    QPointF delta = QPointF(pos) - m_transformStartCenter;
    m_transformStartAngle = qRadiansToDegrees(qAtan2(delta.y(), delta.x()));
    m_transformStartDistance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
    m_dragStart = pos;
}

void ShapeAnnotationEditor::updateTransformation(const QPoint& pos)
{
    ShapeAnnotation* shapeItem = selectedShape();
    if (!shapeItem || !m_isTransforming) {
        return;
    }

    QPointF delta = QPointF(pos) - m_transformStartCenter;

    switch (m_activeGizmoHandle) {
    case GizmoHandle::Rotation: {
        const qreal currentAngle = qRadiansToDegrees(qAtan2(delta.y(), delta.x()));
        const qreal angleDelta = normalizeAngleDelta(currentAngle - m_transformStartAngle);
        shapeItem->setRotation(m_transformStartRotation + angleDelta);
        break;
    }
    case GizmoHandle::TopLeft:
    case GizmoHandle::TopRight:
    case GizmoHandle::BottomLeft:
    case GizmoHandle::BottomRight: {
        const qreal currentDistance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
        if (m_transformStartDistance > 0.0) {
            const qreal scaleFactor = currentDistance / m_transformStartDistance;
            const qreal nextScaleX = qBound(
                kMinScale, m_transformStartScaleX * scaleFactor, kMaxScale);
            const qreal nextScaleY = qBound(
                kMinScale, m_transformStartScaleY * scaleFactor, kMaxScale);
            shapeItem->setScale(nextScaleX, nextScaleY);
        }
        break;
    }
    case GizmoHandle::Body: {
        const QPoint moveDelta = pos - m_dragStart;
        shapeItem->moveBy(QPointF(moveDelta));
        m_dragStart = pos;
        break;
    }
    default:
        break;
    }
}

void ShapeAnnotationEditor::finishTransformation()
{
    m_isTransforming = false;
    m_activeGizmoHandle = GizmoHandle::None;
}

void ShapeAnnotationEditor::startDragging(const QPoint& pos)
{
    m_isDragging = true;
    m_dragStart = pos;
}

void ShapeAnnotationEditor::updateDragging(const QPoint& pos)
{
    ShapeAnnotation* shapeItem = selectedShape();
    if (!shapeItem || !m_isDragging) {
        return;
    }

    const QPoint delta = pos - m_dragStart;
    shapeItem->moveBy(QPointF(delta));
    m_dragStart = pos;
}

void ShapeAnnotationEditor::finishDragging()
{
    m_isDragging = false;
}
