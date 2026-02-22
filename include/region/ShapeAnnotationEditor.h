#ifndef SHAPEANNOTATIONEDITOR_H
#define SHAPEANNOTATIONEDITOR_H

#include <QPoint>
#include <QPointF>
#include "TransformationGizmo.h"

class AnnotationLayer;
class ShapeAnnotation;

/**
 * @brief Handles shape annotation drag/resize/rotate interactions.
 */
class ShapeAnnotationEditor
{
public:
    static constexpr qreal kMinScale = 0.1;
    static constexpr qreal kMaxScale = 10.0;

    void setAnnotationLayer(AnnotationLayer* layer);

    void startTransformation(const QPoint& pos, GizmoHandle handle);
    void updateTransformation(const QPoint& pos);
    void finishTransformation();
    bool isTransforming() const { return m_isTransforming; }
    GizmoHandle activeHandle() const { return m_activeGizmoHandle; }

    void startDragging(const QPoint& pos);
    void updateDragging(const QPoint& pos);
    void finishDragging();
    bool isDragging() const { return m_isDragging; }

private:
    ShapeAnnotation* selectedShape() const;

    AnnotationLayer* m_annotationLayer = nullptr;

    GizmoHandle m_activeGizmoHandle = GizmoHandle::None;
    bool m_isTransforming = false;
    bool m_isDragging = false;

    QPointF m_transformStartCenter;
    qreal m_transformStartRotation = 0.0;
    qreal m_transformStartScaleX = 1.0;
    qreal m_transformStartScaleY = 1.0;
    qreal m_transformStartAngle = 0.0;
    qreal m_transformStartDistance = 0.0;
    QPoint m_dragStart;
};

#endif // SHAPEANNOTATIONEDITOR_H
