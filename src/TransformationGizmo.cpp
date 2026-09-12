#include "TransformationGizmo.h"
#include "annotations/TextBoxAnnotation.h"
#include "annotations/EmojiStickerAnnotation.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/PolylineAnnotation.h"
#include <QtMath>
#include <QLineF>
#include <limits>

namespace {
template<typename Handle>
class NearestHandle
{
public:
    NearestHandle(const QPoint& point, Handle none) : m_point(point), m_handle(none) {}

    void consider(const QPointF& center, qreal radius, Handle handle)
    {
        const QPointF delta = center - m_point;
        const qreal distance = QPointF::dotProduct(delta, delta);
        // Strict comparison keeps the first candidate on an exact tie.
        if (distance <= radius * radius && distance < m_distance) {
            m_distance = distance;
            m_handle = handle;
        }
    }
    Handle result() const { return m_handle; }

private:
    QPointF m_point;
    qreal m_distance = std::numeric_limits<qreal>::infinity();
    Handle m_handle;
};

template<typename Annotation>
GizmoHandle hitBoxHandles(const Annotation* annotation, const QPoint& point)
{
    NearestHandle<GizmoHandle> nearest(point, GizmoHandle::None);
    nearest.consider(TransformationGizmo::rotationHandlePosition(annotation),
                     TransformationGizmo::kRotationHandleRadius + TransformationGizmo::kHitTolerance,
                     GizmoHandle::Rotation);
    const auto corners = TransformationGizmo::cornerHandlePositions(annotation);
    static constexpr GizmoHandle handles[] = {
        GizmoHandle::TopLeft, GizmoHandle::TopRight,
        GizmoHandle::BottomRight, GizmoHandle::BottomLeft
    };
    for (int i = 0; i < corners.size(); ++i) {
        nearest.consider(corners[i],
                         TransformationGizmo::kHandleRadius + TransformationGizmo::kHitTolerance,
                         handles[i]);
    }
    return nearest.result();
}
} // namespace

void TransformationGizmo::draw(QPainter &painter, const TextBoxAnnotation *annotation)
{
    if (!annotation) return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPolygonF poly = annotation->transformedBoundingPolygon();

    // 1. Draw text-focused border (solid blue outline + soft halo)
    drawTextFocusBorder(painter, poly);

    // 2. Draw corner resize handles
    QVector<QPointF> corners = cornerHandlePositions(annotation);
    drawCornerHandles(painter, corners);

    // 3. Draw rotation handle (lollipop from top-center)
    QPointF topCenter = (poly.at(0) + poly.at(1)) / 2.0;
    QPointF handlePos = rotationHandlePosition(annotation);
    drawRotationHandle(painter, topCenter, handlePos);

    painter.restore();
}

void TransformationGizmo::drawTextFocusBorder(QPainter &painter, const QPolygonF &polygon)
{
    painter.setBrush(Qt::NoBrush);

    // Soft outer halo to match the reference focus treatment.
    QPen haloPen(QColor(0, 174, 255, 72), 4.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(haloPen);
    painter.drawPolygon(polygon);

    // Crisp inner border for edge definition.
    QPen mainPen(QColor(150, 220, 255, 235), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(mainPen);
    painter.drawPolygon(polygon);
}

void TransformationGizmo::drawDashedBorder(QPainter &painter, const QPolygonF &polygon)
{
    // Draw white dashed border
    QPen pen(Qt::white, 1.5, Qt::DashLine);
    pen.setDashPattern({4, 4});
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(polygon);

    // Draw dark shadow for visibility on light backgrounds
    QPen shadowPen(QColor(0, 0, 0, 100), 1.5, Qt::DashLine);
    shadowPen.setDashOffset(4);
    shadowPen.setDashPattern({4, 4});
    painter.setPen(shadowPen);
    painter.drawPolygon(polygon);
}

void TransformationGizmo::drawCornerHandles(QPainter &painter, const QVector<QPointF> &corners)
{
    // Draw corner handles as blue circles with white border
    painter.setPen(QPen(Qt::white, 1.5));
    painter.setBrush(QColor(0, 174, 255));  // Blue accent color

    for (const QPointF &corner : corners) {
        painter.drawEllipse(corner, kHandleRadius, kHandleRadius);
    }
}

void TransformationGizmo::drawRotationHandle(QPainter &painter, const QPointF &topCenter, const QPointF &handlePos)
{
    // Draw stem line from top-center to handle
    painter.setPen(QPen(Qt::white, 1.5));
    painter.drawLine(topCenter, handlePos);

    // Draw dark line for visibility
    painter.setPen(QPen(QColor(0, 0, 0, 80), 1.5));
    painter.drawLine(topCenter + QPointF(1, 1), handlePos + QPointF(1, 1));

    // Draw circular handle at end
    painter.setPen(QPen(Qt::white, 1.5));
    painter.setBrush(QColor(0, 174, 255));
    painter.drawEllipse(handlePos, kRotationHandleRadius, kRotationHandleRadius);

    // Draw rotation icon inside circle (curved arrow indicator)
    painter.setPen(QPen(Qt::white, 1.2));
    QRectF arcRect(handlePos.x() - 3, handlePos.y() - 3, 6, 6);
    painter.drawArc(arcRect, 45 * 16, 270 * 16);
}

QPointF TransformationGizmo::rotationHandlePosition(const TextBoxAnnotation *annotation)
{
    if (!annotation) return QPointF();

    QPolygonF poly = annotation->transformedBoundingPolygon();
    return rotationHandlePosition(poly, annotation->center());
}

QVector<QPointF> TransformationGizmo::cornerHandlePositions(const TextBoxAnnotation *annotation)
{
    QVector<QPointF> corners;
    if (!annotation) return corners;

    QPolygonF poly = annotation->transformedBoundingPolygon();
    // poly: TopLeft[0], TopRight[1], BottomRight[2], BottomLeft[3]
    for (int i = 0; i < 4; ++i) {
        corners.append(poly.at(i));
    }
    return corners;
}

GizmoHandle TransformationGizmo::hitTest(const TextBoxAnnotation *annotation, const QPoint &point)
{
    if (!annotation) return GizmoHandle::None;
    const GizmoHandle handle = hitBoxHandles(annotation, point);
    if (handle != GizmoHandle::None) return handle;
    return annotation->containsPoint(point) ? GizmoHandle::Body : GizmoHandle::None;
}

QPointF TransformationGizmo::rotationHandlePosition(const QPolygonF &poly, const QPointF &center)
{
    if (poly.size() < 2) {
        return QPointF();
    }

    const QPointF topCenter = (poly.at(0) + poly.at(1)) / 2.0;
    QPointF direction = topCenter - center;
    const qreal length = qSqrt(direction.x() * direction.x() + direction.y() * direction.y());

    if (length > 0) {
        direction /= length;
    } else {
        direction = QPointF(0, -1);
    }

    return topCenter + direction * kRotationHandleDistance;
}

// ============================================================================
// EmojiStickerAnnotation overloads
// ============================================================================

void TransformationGizmo::draw(QPainter &painter, const EmojiStickerAnnotation *annotation)
{
    if (!annotation) return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPolygonF poly = annotation->transformedBoundingPolygon();

    // 1. Draw dashed border
    drawDashedBorder(painter, poly);

    // 2. Draw corner resize handles
    QVector<QPointF> corners = cornerHandlePositions(annotation);
    drawCornerHandles(painter, corners);

    // 3. Draw rotation handle (same behavior as text tool)
    QPointF topCenter = (poly.at(0) + poly.at(1)) / 2.0;
    QPointF handlePos = rotationHandlePosition(annotation);
    drawRotationHandle(painter, topCenter, handlePos);

    painter.restore();
}

QPointF TransformationGizmo::rotationHandlePosition(const EmojiStickerAnnotation *annotation)
{
    if (!annotation) return QPointF();

    QPolygonF poly = annotation->transformedBoundingPolygon();
    return rotationHandlePosition(poly, annotation->center());
}

QVector<QPointF> TransformationGizmo::cornerHandlePositions(const EmojiStickerAnnotation *annotation)
{
    QVector<QPointF> corners;
    if (!annotation) return corners;

    QPolygonF poly = annotation->transformedBoundingPolygon();
    // poly: TopLeft[0], TopRight[1], BottomRight[2], BottomLeft[3]
    for (int i = 0; i < 4; ++i) {
        corners.append(poly.at(i));
    }
    return corners;
}

GizmoHandle TransformationGizmo::hitTest(const EmojiStickerAnnotation *annotation, const QPoint &point)
{
    if (!annotation) return GizmoHandle::None;
    const GizmoHandle handle = hitBoxHandles(annotation, point);
    if (handle != GizmoHandle::None) return handle;
    return annotation->containsPoint(point) ? GizmoHandle::Body : GizmoHandle::None;
}

// ============================================================================
// ShapeAnnotation overloads
// ============================================================================

void TransformationGizmo::draw(QPainter &painter, const ShapeAnnotation *annotation)
{
    if (!annotation) return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPolygonF poly = annotation->transformedBoundingPolygon();

    drawDashedBorder(painter, poly);

    QVector<QPointF> corners = cornerHandlePositions(annotation);
    drawCornerHandles(painter, corners);

    QPointF topCenter = (poly.at(0) + poly.at(1)) / 2.0;
    QPointF handlePos = rotationHandlePosition(annotation);
    drawRotationHandle(painter, topCenter, handlePos);

    painter.restore();
}

QVector<QPointF> TransformationGizmo::cornerHandlePositions(const ShapeAnnotation *annotation)
{
    QVector<QPointF> corners;
    if (!annotation) return corners;

    QPolygonF poly = annotation->transformedBoundingPolygon();
    for (int i = 0; i < 4; ++i) {
        corners.append(poly.at(i));
    }
    return corners;
}

QPointF TransformationGizmo::rotationHandlePosition(const ShapeAnnotation *annotation)
{
    if (!annotation) return QPointF();

    QPolygonF poly = annotation->transformedBoundingPolygon();
    return rotationHandlePosition(poly, annotation->center());
}

GizmoHandle TransformationGizmo::hitTest(const ShapeAnnotation *annotation, const QPoint &point)
{
    if (!annotation) return GizmoHandle::None;
    const GizmoHandle handle = hitBoxHandles(annotation, point);
    if (handle != GizmoHandle::None) return handle;
    return annotation->containsPoint(point) ? GizmoHandle::Body : GizmoHandle::None;
}

// ============================================================================
// ArrowAnnotation overloads
// ============================================================================

void TransformationGizmo::drawArrowHandle(QPainter &painter, const QPointF &pos, bool isControlPoint)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (isControlPoint) {
        // Control point: yellow diamond shape
        painter.setPen(QPen(Qt::white, 1.5));
        painter.setBrush(QColor(255, 200, 0));  // Yellow/gold for control point

        // Draw diamond (rotated square)
        QPolygonF diamond;
        diamond << QPointF(pos.x(), pos.y() - kControlHandleRadius)
                << QPointF(pos.x() + kControlHandleRadius, pos.y())
                << QPointF(pos.x(), pos.y() + kControlHandleRadius)
                << QPointF(pos.x() - kControlHandleRadius, pos.y());
        painter.drawPolygon(diamond);
    } else {
        // Start/End point: blue circle (same style as corner handles)
        painter.setPen(QPen(Qt::white, 1.5));
        painter.setBrush(QColor(0, 174, 255));  // Blue accent color
        painter.drawEllipse(pos, kArrowHandleRadius, kArrowHandleRadius);
    }

    painter.restore();
}

void TransformationGizmo::draw(QPainter &painter, const ArrowAnnotation *annotation)
{
    if (!annotation) return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPointF start = annotation->start();
    QPointF end = annotation->end();
    QPointF control = annotation->controlPoint();

    // Calculate the point ON the curve at t=0.5 (midpoint)
    // B(0.5) = 0.25*P0 + 0.5*P1 + 0.25*P2
    QPointF curveMidpoint = 0.25 * start + 0.5 * control + 0.25 * end;

    // Draw handles at start, end, and curve midpoint (not Bézier control point)
    drawArrowHandle(painter, start, false);         // Start: blue circle
    drawArrowHandle(painter, end, false);           // End: blue circle
    drawArrowHandle(painter, curveMidpoint, true);  // Curve midpoint: yellow diamond

    painter.restore();
}

GizmoHandle TransformationGizmo::hitTest(const ArrowAnnotation *annotation, const QPoint &point)
{
    if (!annotation) return GizmoHandle::None;

    QPointF start = annotation->start();
    QPointF end = annotation->end();
    QPointF control = annotation->controlPoint();

    // Calculate the point ON the curve at t=0.5 (midpoint)
    // This is where the control handle is displayed
    QPointF curveMidpoint = 0.25 * start + 0.5 * control + 0.25 * end;

    NearestHandle<GizmoHandle> nearest(point, GizmoHandle::None);
    // Preserve control/start/end priority only when centers are equally close.
    nearest.consider(curveMidpoint, kControlHandleRadius + kHitTolerance, GizmoHandle::ArrowControl);
    nearest.consider(start, kArrowHandleRadius + kHitTolerance, GizmoHandle::ArrowStart);
    nearest.consider(end, kArrowHandleRadius + kHitTolerance, GizmoHandle::ArrowEnd);
    if (nearest.result() != GizmoHandle::None) return nearest.result();

    // 4. Check if on the arrow body (for moving entire arrow)
    if (annotation->containsPoint(point)) {
        return GizmoHandle::Body;
    }

    return GizmoHandle::None;
}

// ============================================================================
// PolylineAnnotation overloads
// ============================================================================

void TransformationGizmo::draw(QPainter &painter, const PolylineAnnotation *annotation)
{
    if (!annotation) return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QVector<QPoint> points = annotation->points();
    for (const QPoint& pt : points) {
        drawArrowHandle(painter, pt, false); // Reuse arrow handle style (blue circle)
    }

    painter.restore();
}

int TransformationGizmo::hitTestVertex(const PolylineAnnotation *annotation, const QPoint &point)
{
    if (!annotation) return -2; // Nothing hit

    QVector<QPoint> points = annotation->points();

    NearestHandle<int> nearest(point, -2);
    for (int i = 0; i < points.size(); ++i) {
        nearest.consider(points[i], kArrowHandleRadius + kHitTolerance, i);
    }
    if (nearest.result() >= 0) return nearest.result();

    // 2. Check body
    if (annotation->containsPoint(point)) {
        return -1; // Body hit
    }

    return -2; // Nothing hit
}
