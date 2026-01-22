#ifndef TRANSFORMATIONGIZMO_H
#define TRANSFORMATIONGIZMO_H

#include <QPainter>
#include <QPoint>
#include <QPointF>
#include <QPolygonF>
#include <QVector>

class TextBoxAnnotation;
class EmojiStickerAnnotation;
class ArrowAnnotation;
class PolylineAnnotation;

// Handle types for hit-testing transformation gizmo
enum class GizmoHandle {
    None = 0,
    Body,        // Inside text - for moving
    TopLeft,     // Corner resize handles
    TopRight,
    BottomLeft,
    BottomRight,
    Rotation,    // Rotation handle (lollipop)

    // Arrow-specific handles
    ArrowStart,   // Start point of arrow
    ArrowEnd,     // End point of arrow
    ArrowControl  // Control point for Bézier curve
};

/**
 * @brief Helper class for drawing and hit-testing the transformation gizmo.
 *
 * The gizmo consists of:
 * - A dashed border around the selected text
 * - Four corner handles for scaling
 * - A rotation handle (lollipop style) extending from top-center
 *
 * For arrows:
 * - Start/End handles (circles) for endpoint manipulation
 * - Control handle (diamond) for curve adjustment
 * - Dashed lines showing the Bézier control hull
 */
class TransformationGizmo
{
public:
    // Gizmo configuration constants
    static constexpr int kHandleRadius = 5;
    static constexpr int kRotationHandleDistance = 30;
    static constexpr int kRotationHandleRadius = 6;
    static constexpr int kHitTolerance = 8;

    // Arrow-specific constants
    static constexpr int kArrowHandleRadius = 6;
    static constexpr int kControlHandleRadius = 5;

    /**
     * @brief Draw the transformation gizmo for a text annotation.
     * @param painter The painter to draw with
     * @param annotation The text annotation to draw the gizmo for
     */
    static void draw(QPainter &painter, const TextBoxAnnotation *annotation);

    /**
     * @brief Hit-test the gizmo to determine which handle (if any) is at the given point.
     * @param annotation The text annotation
     * @param point The point to test (in global/widget coordinates)
     * @return The handle at the point, or GizmoHandle::None if no handle is hit
     */
    static GizmoHandle hitTest(const TextBoxAnnotation *annotation, const QPoint &point);

    /**
     * @brief Get the position of the rotation handle.
     * @param annotation The text annotation
     * @return The position of the rotation handle circle center
     */
    static QPointF rotationHandlePosition(const TextBoxAnnotation *annotation);

    /**
     * @brief Get the positions of the four corner handles.
     * @param annotation The text annotation
     * @return Vector of 4 corner positions (TopLeft, TopRight, BottomRight, BottomLeft)
     */
    static QVector<QPointF> cornerHandlePositions(const TextBoxAnnotation *annotation);

    // EmojiStickerAnnotation overloads (no rotation handle)
    static void draw(QPainter &painter, const EmojiStickerAnnotation *annotation);
    static GizmoHandle hitTest(const EmojiStickerAnnotation *annotation, const QPoint &point);
    static QVector<QPointF> cornerHandlePositions(const EmojiStickerAnnotation *annotation);

    // ArrowAnnotation overloads
    /**
     * @brief Draw the transformation gizmo for an arrow annotation.
     *
     * Draws:
     * - Circles at start and end points
     * - A distinct diamond/circle at the control point
     * - Dashed lines from start->control and control->end (Bézier hull)
     */
    static void draw(QPainter &painter, const ArrowAnnotation *annotation);

    /**
     * @brief Hit-test the arrow gizmo handles.
     * @return ArrowStart, ArrowEnd, ArrowControl, Body, or None
     */
    static GizmoHandle hitTest(const ArrowAnnotation *annotation, const QPoint &point);

    // PolylineAnnotation overloads
    static void draw(QPainter &painter, const PolylineAnnotation *annotation);
    
    /**
     * @brief Hit-test polyline vertices.
     * @return Index of the vertex (0..N) or -1 if body is hit, -2 if nothing hit.
     * Note: This returns an int index, not GizmoHandle, because polylines have dynamic vertices.
     */
    static int hitTestVertex(const PolylineAnnotation *annotation, const QPoint &point);

private:
    static void drawDashedBorder(QPainter &painter, const QPolygonF &polygon);
    static void drawCornerHandles(QPainter &painter, const QVector<QPointF> &corners);
    static void drawRotationHandle(QPainter &painter, const QPointF &topCenter, const QPointF &handlePos);

    // Arrow-specific drawing helpers
    static void drawArrowHandle(QPainter &painter, const QPointF &pos, bool isControlPoint = false);
};

#endif // TRANSFORMATIONGIZMO_H
