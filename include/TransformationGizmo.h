#ifndef TRANSFORMATIONGIZMO_H
#define TRANSFORMATIONGIZMO_H

#include <QPainter>
#include <QPoint>
#include <QPointF>
#include <QPolygonF>
#include <QVector>

class TextAnnotation;

// Handle types for hit-testing transformation gizmo
enum class GizmoHandle {
    None = 0,
    Body,        // Inside text - for moving
    TopLeft,     // Corner resize handles
    TopRight,
    BottomLeft,
    BottomRight,
    Rotation     // Rotation handle (lollipop)
};

/**
 * @brief Helper class for drawing and hit-testing the transformation gizmo.
 *
 * The gizmo consists of:
 * - A dashed border around the selected text
 * - Four corner handles for scaling
 * - A rotation handle (lollipop style) extending from top-center
 */
class TransformationGizmo
{
public:
    // Gizmo configuration constants
    static constexpr int kHandleRadius = 5;
    static constexpr int kRotationHandleDistance = 30;
    static constexpr int kRotationHandleRadius = 6;
    static constexpr int kHitTolerance = 8;

    /**
     * @brief Draw the transformation gizmo for a text annotation.
     * @param painter The painter to draw with
     * @param annotation The text annotation to draw the gizmo for
     */
    static void draw(QPainter &painter, const TextAnnotation *annotation);

    /**
     * @brief Hit-test the gizmo to determine which handle (if any) is at the given point.
     * @param annotation The text annotation
     * @param point The point to test (in global/widget coordinates)
     * @return The handle at the point, or GizmoHandle::None if no handle is hit
     */
    static GizmoHandle hitTest(const TextAnnotation *annotation, const QPoint &point);

    /**
     * @brief Get the position of the rotation handle.
     * @param annotation The text annotation
     * @return The position of the rotation handle circle center
     */
    static QPointF rotationHandlePosition(const TextAnnotation *annotation);

    /**
     * @brief Get the positions of the four corner handles.
     * @param annotation The text annotation
     * @return Vector of 4 corner positions (TopLeft, TopRight, BottomRight, BottomLeft)
     */
    static QVector<QPointF> cornerHandlePositions(const TextAnnotation *annotation);

private:
    static void drawDashedBorder(QPainter &painter, const QPolygonF &polygon);
    static void drawCornerHandles(QPainter &painter, const QVector<QPointF> &corners);
    static void drawRotationHandle(QPainter &painter, const QPointF &topCenter, const QPointF &handlePos);
};

#endif // TRANSFORMATIONGIZMO_H
