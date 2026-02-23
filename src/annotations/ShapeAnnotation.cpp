#include "annotations/ShapeAnnotation.h"
#include <QPainter>
#include <QtMath>
#include <cmath>

ShapeAnnotation::ShapeAnnotation(const QRect &rect, ShapeType type,
                                 const QColor &color, int width, bool filled)
    : m_rect(rect)
    , m_type(type)
    , m_color(color)
    , m_width(width)
    , m_filled(filled)
{
}

QRectF ShapeAnnotation::normalizedRect() const
{
    return m_rect.normalized();
}

QPointF ShapeAnnotation::center() const
{
    return normalizedRect().center();
}

QTransform ShapeAnnotation::localLinearTransform() const
{
    QTransform transform;
    if (!qFuzzyIsNull(m_rotation)) {
        transform.rotate(m_rotation);
    }
    if (!qFuzzyCompare(m_scaleX, 1.0) || !qFuzzyCompare(m_scaleY, 1.0)) {
        transform.scale(m_scaleX, m_scaleY);
    }
    return transform;
}

QPolygonF ShapeAnnotation::transformedBoundingPolygon() const
{
    const QTransform linear = localLinearTransform();
    const QPointF mappedUnitX = linear.map(QPointF(1.0, 0.0));
    const QPointF mappedUnitY = linear.map(QPointF(0.0, 1.0));
    const qreal scaleX = qMax(qHypot(mappedUnitX.x(), mappedUnitX.y()), kMinScale);
    const qreal scaleY = qMax(qHypot(mappedUnitY.x(), mappedUnitY.y()), kMinScale);
    const qreal hitMarginX = static_cast<qreal>(kHitMargin) / scaleX;
    const qreal hitMarginY = static_cast<qreal>(kHitMargin) / scaleY;

    QRectF rect = normalizedRect();
    rect.adjust(-hitMarginX, -hitMarginY, hitMarginX, hitMarginY);

    QPolygonF poly;
    poly << rect.topLeft()
         << rect.topRight()
         << rect.bottomRight()
         << rect.bottomLeft();

    const QPointF c = center();
    for (int i = 0; i < poly.size(); ++i) {
        poly[i] = c + linear.map(poly[i] - c);
    }

    return poly;
}

bool ShapeAnnotation::containsTransformedPoint(const QPointF& transformedPoint) const
{
    QRectF rect = normalizedRect();
    // Keep one-dimensional shapes (horizontal/vertical drags) selectable.
    if (rect.width() <= 0.0 && rect.height() <= 0.0) {
        return false;
    }

    const QTransform linear = localLinearTransform();
    const QPointF mappedUnitX = linear.map(QPointF(1.0, 0.0));
    const QPointF mappedUnitY = linear.map(QPointF(0.0, 1.0));
    const qreal scaleX = qMax(qHypot(mappedUnitX.x(), mappedUnitX.y()), kMinScale);
    const qreal scaleY = qMax(qHypot(mappedUnitY.x(), mappedUnitY.y()), kMinScale);

    // Keep the extra hit margin stable in screen space even when the shape is scaled.
    const qreal hitMarginX = static_cast<qreal>(kHitMargin) / scaleX;
    const qreal hitMarginY = static_cast<qreal>(kHitMargin) / scaleY;
    const qreal expandedHalfStrokeX = static_cast<qreal>(m_width) / 2.0 + hitMarginX;
    const qreal expandedHalfStrokeY = static_cast<qreal>(m_width) / 2.0 + hitMarginY;

    if (m_type == ShapeType::Rectangle) {
        return rect.adjusted(-expandedHalfStrokeX, -expandedHalfStrokeY,
                             expandedHalfStrokeX, expandedHalfStrokeY)
            .contains(transformedPoint);
    }

    const QPointF c = rect.center();
    const qreal rx = rect.width() / 2.0 + expandedHalfStrokeX;
    const qreal ry = rect.height() / 2.0 + expandedHalfStrokeY;
    if (rx <= 0.0 || ry <= 0.0) {
        return false;
    }

    const qreal dx = (transformedPoint.x() - c.x()) / rx;
    const qreal dy = (transformedPoint.y() - c.y()) / ry;
    return (dx * dx + dy * dy) <= 1.0;
}

bool ShapeAnnotation::containsPoint(const QPoint &point) const
{
    QTransform inverse;
    const QTransform linear = localLinearTransform();
    if (!linear.isInvertible()) {
        return false;
    }
    inverse = linear.inverted();

    const QPointF c = center();
    const QPointF centered = QPointF(point) - c;
    const QPointF transformedPoint = c + inverse.map(centered);
    return containsTransformedPoint(transformedPoint);
}

void ShapeAnnotation::draw(QPainter &painter) const
{
    painter.save();

    const QRectF rect = normalizedRect();
    const QPointF c = center();
    painter.translate(c);
    painter.setTransform(localLinearTransform(), true);
    painter.translate(-c);

    if (m_type == ShapeType::Rectangle) {
        QPen pen(m_color, m_width, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
        painter.setPen(pen);
        painter.setRenderHint(QPainter::Antialiasing, true);

        if (m_filled) {
            painter.setBrush(m_color);
        } else {
            painter.setBrush(Qt::NoBrush);
        }

        painter.drawRect(rect);
    } else {
        // Ellipse
        QPen pen(m_color, m_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.setRenderHint(QPainter::Antialiasing, true);

        if (m_filled) {
            painter.setBrush(m_color);
        } else {
            painter.setBrush(Qt::NoBrush);
        }

        painter.drawEllipse(rect);
    }

    painter.restore();
}

QRect ShapeAnnotation::boundingRect() const
{
    const QRectF rect = normalizedRect();
    QPolygonF visualPoly;
    visualPoly << rect.topLeft()
               << rect.topRight()
               << rect.bottomRight()
               << rect.bottomLeft();

    const QPointF c = center();
    const QTransform linear = localLinearTransform();
    for (int i = 0; i < visualPoly.size(); ++i) {
        visualPoly[i] = c + linear.map(visualPoly[i] - c);
    }

    const QPointF mappedUnitX = linear.map(QPointF(1.0, 0.0));
    const QPointF mappedUnitY = linear.map(QPointF(0.0, 1.0));
    const qreal scaleX = qHypot(mappedUnitX.x(), mappedUnitX.y());
    const qreal scaleY = qHypot(mappedUnitY.x(), mappedUnitY.y());
    const qreal maxScale = qMax(scaleX, scaleY);

    const int baseMargin = m_width / 2 + 2;
    const int margin = qMax(1, static_cast<int>(qCeil(static_cast<qreal>(baseMargin) * maxScale)));
    return visualPoly.boundingRect().toAlignedRect().adjusted(
        -margin, -margin, margin, margin);
}

std::unique_ptr<AnnotationItem> ShapeAnnotation::clone() const
{
    auto cloned = std::make_unique<ShapeAnnotation>(
        m_rect.toAlignedRect(), m_type, m_color, m_width, m_filled);
    cloned->setRotation(m_rotation);
    cloned->setScale(m_scaleX, m_scaleY);
    return cloned;
}

void ShapeAnnotation::translate(const QPointF& delta)
{
    moveBy(delta);
}

void ShapeAnnotation::setRect(const QRect &rect)
{
    m_rect = rect;
}

void ShapeAnnotation::setRotation(qreal degrees)
{
    qreal normalized = std::fmod(degrees, 360.0);
    if (normalized < 0.0) {
        normalized += 360.0;
    }
    if (qFuzzyIsNull(normalized)) {
        normalized = 0.0;
    }
    m_rotation = normalized;
}

void ShapeAnnotation::setScale(qreal scaleX, qreal scaleY)
{
    m_scaleX = qBound(kMinScale, scaleX, kMaxScale);
    m_scaleY = qBound(kMinScale, scaleY, kMaxScale);
}

void ShapeAnnotation::moveBy(const QPointF& delta)
{
    m_rect.translate(delta.x(), delta.y());
}
