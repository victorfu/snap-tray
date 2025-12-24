#include "TransformationGizmo.h"
#include "AnnotationLayer.h"
#include <QtMath>
#include <QLineF>

void TransformationGizmo::draw(QPainter &painter, const TextAnnotation *annotation)
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

    // 3. Draw rotation handle (lollipop from top-center)
    QPointF topCenter = (poly.at(0) + poly.at(1)) / 2.0;
    QPointF handlePos = rotationHandlePosition(annotation);
    drawRotationHandle(painter, topCenter, handlePos);

    painter.restore();
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

QPointF TransformationGizmo::rotationHandlePosition(const TextAnnotation *annotation)
{
    if (!annotation) return QPointF();

    QPolygonF poly = annotation->transformedBoundingPolygon();
    // poly: TopLeft[0], TopRight[1], BottomRight[2], BottomLeft[3]
    QPointF topCenter = (poly.at(0) + poly.at(1)) / 2.0;
    QPointF center = annotation->center();

    // Direction from center to top-center (perpendicular to top edge, pointing outward)
    QPointF direction = topCenter - center;
    qreal length = qSqrt(direction.x() * direction.x() + direction.y() * direction.y());

    if (length > 0) {
        direction /= length;  // Normalize
    } else {
        direction = QPointF(0, -1);  // Default: up
    }

    // Extend beyond top center by the rotation handle distance
    return topCenter + direction * kRotationHandleDistance;
}

QVector<QPointF> TransformationGizmo::cornerHandlePositions(const TextAnnotation *annotation)
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

GizmoHandle TransformationGizmo::hitTest(const TextAnnotation *annotation, const QPoint &point)
{
    if (!annotation) return GizmoHandle::None;

    QPointF p(point);

    // 1. Check rotation handle first (highest priority - it's outside the text box)
    QPointF rotHandle = rotationHandlePosition(annotation);
    qreal distToRot = QLineF(p, rotHandle).length();
    if (distToRot <= kRotationHandleRadius + kHitTolerance) {
        return GizmoHandle::Rotation;
    }

    // 2. Check corner handles (they're on the corners, so check before body)
    QVector<QPointF> corners = cornerHandlePositions(annotation);
    GizmoHandle cornerHandles[] = {
        GizmoHandle::TopLeft,
        GizmoHandle::TopRight,
        GizmoHandle::BottomRight,
        GizmoHandle::BottomLeft
    };

    for (int i = 0; i < 4; ++i) {
        qreal dist = QLineF(p, corners[i]).length();
        if (dist <= kHandleRadius + kHitTolerance) {
            return cornerHandles[i];
        }
    }

    // 3. Check if inside the text body (for moving)
    if (annotation->containsPoint(point)) {
        return GizmoHandle::Body;
    }

    return GizmoHandle::None;
}
