#include "annotations/LineAnnotationGeometry.h"

#include <QPainterPathStroker>
#include <QtMath>
#include <array>

namespace {
constexpr qreal kHeadHalfAngle = M_PI / 6.0;
constexpr qreal kMinHeadLength = 10.0;
constexpr qreal kHeadWidthRatio = 3.0;
constexpr qreal kShaftHeadOverlap = 1.0;
constexpr qreal kAntialiasMargin = 1.0;
constexpr qreal kHitPadding = 6.0;
constexpr qreal kMinHitWidth = 10.0;

QPainterPath stroke(const QPainterPath& path, qreal width, Qt::PenCapStyle cap)
{
    QPainterPathStroker stroker;
    stroker.setWidth(width);
    stroker.setCapStyle(cap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return stroker.createStroke(path);
}
} // namespace

qreal LineAnnotationGeometry::headLength(int width)
{
    return qMax(kMinHeadLength, width * kHeadWidthRatio);
}

qreal LineAnnotationGeometry::headBaseDistance(int width)
{
    return headLength(width) * qCos(kHeadHalfAngle) - kShaftHeadOverlap;
}

void LineAnnotationGeometry::addHead(const QPointF& tip, qreal angle, int width,
                                     LineEndStyle style)
{
    if (style == LineEndStyle::None) {
        return;
    }
    const qreal length = headLength(width);
    const QPointF wing1 = tip - length * QPointF(qCos(angle - kHeadHalfAngle),
                                                qSin(angle - kHeadHalfAngle));
    const QPointF wing2 = tip - length * QPointF(qCos(angle + kHeadHalfAngle),
                                                qSin(angle + kHeadHalfAngle));
    Head head;
    head.filled = style == LineEndStyle::EndArrow || style == LineEndStyle::BothArrow;
    if (style == LineEndStyle::EndArrowLine) {
        // Separate subpaths preserve the round caps of the two original lines.
        head.path.moveTo(wing1);
        head.path.lineTo(tip);
        head.path.moveTo(tip);
        head.path.lineTo(wing2);
    } else {
        head.path.moveTo(tip);
        head.path.lineTo(wing1);
        head.path.lineTo(wing2);
        head.path.closeSubpath();
    }
    heads.append(head);
}

void LineAnnotationGeometry::draw(QPainter& painter, const QColor& color, int width,
                                  LineStyle style) const
{
    static constexpr std::array penStyles{Qt::SolidLine, Qt::DashLine, Qt::DotLine};
    const int index = static_cast<int>(style);
    const Qt::PenStyle penStyle = index >= 0 && index < int(penStyles.size())
        ? penStyles[index] : Qt::SolidLine;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(color, width, penStyle, capStyle, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(shaft);
    for (const auto& head : heads) {
        painter.setPen(head.filled ? QPen(Qt::NoPen)
                                  : QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(head.filled ? QBrush(color) : QBrush(Qt::NoBrush));
        painter.drawPath(head.path);
    }
    painter.restore();
}

QRect LineAnnotationGeometry::boundingRect(int width) const
{
    // A solid shaft conservatively bounds every dash pattern as well.
    QRectF bounds = stroke(shaft, width, capStyle).boundingRect();
    for (const auto& head : heads) {
        const QPainterPath painted = head.filled ? head.path
            : stroke(head.path, width, Qt::RoundCap);
        bounds = bounds.united(painted.boundingRect());
    }
    return bounds.isEmpty() ? QRect()
        : bounds.adjusted(-kAntialiasMargin, -kAntialiasMargin,
                          kAntialiasMargin, kAntialiasMargin).toAlignedRect();
}

bool LineAnnotationGeometry::containsPoint(const QPoint& point, int width) const
{
    const qreal hitWidth = qMax(kMinHitWidth, width + kHitPadding);
    if (stroke(shaft, hitWidth, Qt::RoundCap).contains(point)) {
        return true;
    }
    for (const auto& head : heads) {
        if ((head.filled && head.path.contains(point))
            || stroke(head.path, head.filled ? kHitPadding : hitWidth,
                      Qt::RoundCap).contains(point)) {
            return true;
        }
    }
    return false;
}
