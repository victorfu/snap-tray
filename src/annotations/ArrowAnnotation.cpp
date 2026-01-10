#include "annotations/ArrowAnnotation.h"
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

ArrowAnnotation::ArrowAnnotation(const QPoint &start, const QPoint &end, const QColor &color, int width,
                                 LineEndStyle style, LineStyle lineStyle)
    : m_start(start)
    , m_end(end)
    , m_color(color)
    , m_width(width)
    , m_lineEndStyle(style)
    , m_lineStyle(lineStyle)
{
}

void ArrowAnnotation::draw(QPainter &painter) const
{
    painter.save();

    // Map LineStyle to Qt::PenStyle
    Qt::PenStyle qtStyle = Qt::SolidLine;
    switch (m_lineStyle) {
    case LineStyle::Solid:
        qtStyle = Qt::SolidLine;
        break;
    case LineStyle::Dashed:
        qtStyle = Qt::DashLine;
        break;
    case LineStyle::Dotted:
        qtStyle = Qt::DotLine;
        break;
    }

    QPen pen(m_color, m_width, qtStyle, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setRenderHint(QPainter::Antialiasing, true);

    double arrowLength = qMax(10.0, m_width * 3.0);

    // Calculate line endpoints (adjust if arrowheads are present)
    QPointF lineStart = m_start;
    QPointF lineEnd = m_end;

    // Check if we have arrowhead at end
    bool hasEndArrow = (m_lineEndStyle != LineEndStyle::None);
    // Check if we have arrowhead at start (both-ends styles)
    bool hasStartArrow = (m_lineEndStyle == LineEndStyle::BothArrow ||
                          m_lineEndStyle == LineEndStyle::BothArrowOutline);

    // Adjust line end for filled arrowheads (so line doesn't protrude)
    if (hasEndArrow && (m_lineEndStyle == LineEndStyle::EndArrow ||
                        m_lineEndStyle == LineEndStyle::EndArrowOutline ||
                        m_lineEndStyle == LineEndStyle::BothArrow ||
                        m_lineEndStyle == LineEndStyle::BothArrowOutline)) {
        double angle = qAtan2(m_end.y() - m_start.y(), m_end.x() - m_start.x());
        lineEnd = QPointF(
            m_end.x() - arrowLength * qCos(angle),
            m_end.y() - arrowLength * qSin(angle)
        );
    }

    // Adjust line start for filled arrowheads at start
    if (hasStartArrow) {
        double angle = qAtan2(m_start.y() - m_end.y(), m_start.x() - m_end.x());
        lineStart = QPointF(
            m_start.x() - arrowLength * qCos(angle),
            m_start.y() - arrowLength * qSin(angle)
        );
    }

    // Draw the line (to correct endpoints)
    painter.drawLine(lineStart.toPoint(), lineEnd.toPoint());

    // Draw arrowhead(s) based on line end style
    switch (m_lineEndStyle) {
    case LineEndStyle::None:
        // Plain line, no arrowheads
        break;
    case LineEndStyle::EndArrow:
        drawArrowhead(painter, m_start, m_end, true);  // filled
        break;
    case LineEndStyle::EndArrowOutline:
        drawArrowhead(painter, m_start, m_end, false); // outline
        break;
    case LineEndStyle::EndArrowLine:
        drawArrowheadLine(painter, m_start, m_end);
        break;
    case LineEndStyle::BothArrow:
        drawArrowhead(painter, m_start, m_end, true);   // filled at end
        drawArrowhead(painter, m_end, m_start, true);   // filled at start
        break;
    case LineEndStyle::BothArrowOutline:
        drawArrowhead(painter, m_start, m_end, false);  // outline at end
        drawArrowhead(painter, m_end, m_start, false);  // outline at start
        break;
    }

    painter.restore();
}

void ArrowAnnotation::drawArrowhead(QPainter &painter, const QPoint &start, const QPoint &end, bool filled) const
{
    // Calculate the angle of the line
    double angle = qAtan2(end.y() - start.y(), end.x() - start.x());

    // Arrowhead size proportional to line width
    double arrowLength = qMax(10.0, m_width * 3.0);
    double arrowAngle = M_PI / 6.0;  // 30 degrees

    // Calculate arrowhead points
    QPointF arrowP1(
        end.x() - arrowLength * qCos(angle - arrowAngle),
        end.y() - arrowLength * qSin(angle - arrowAngle)
    );
    QPointF arrowP2(
        end.x() - arrowLength * qCos(angle + arrowAngle),
        end.y() - arrowLength * qSin(angle + arrowAngle)
    );

    // Draw arrowhead triangle
    QPainterPath arrowPath;
    arrowPath.moveTo(end);
    arrowPath.lineTo(arrowP1);
    arrowPath.lineTo(arrowP2);
    arrowPath.closeSubpath();

    if (filled) {
        // Solid fill without outline stroke
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_color);
    } else {
        // Outline only
        QPen solidPen(m_color, m_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(solidPen);
        painter.setBrush(Qt::NoBrush);
    }
    painter.drawPath(arrowPath);
}

void ArrowAnnotation::drawArrowheadLine(QPainter &painter, const QPoint &start, const QPoint &end) const
{
    // Calculate the angle of the line
    double angle = qAtan2(end.y() - start.y(), end.x() - start.x());

    // Arrowhead size proportional to line width
    double arrowLength = qMax(10.0, m_width * 3.0);
    double arrowAngle = M_PI / 6.0;  // 30 degrees

    // Calculate arrowhead points
    QPointF arrowP1(
        end.x() - arrowLength * qCos(angle - arrowAngle),
        end.y() - arrowLength * qSin(angle - arrowAngle)
    );
    QPointF arrowP2(
        end.x() - arrowLength * qCos(angle + arrowAngle),
        end.y() - arrowLength * qSin(angle + arrowAngle)
    );

    // Draw two lines forming a V (no closed path)
    QPen solidPen(m_color, m_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(solidPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(arrowP1, QPointF(end));
    painter.drawLine(QPointF(end), arrowP2);
}

QRect ArrowAnnotation::boundingRect() const
{
    int margin = 20;  // Extra margin for arrowhead
    int minX = qMin(m_start.x(), m_end.x()) - margin;
    int maxX = qMax(m_start.x(), m_end.x()) + margin;
    int minY = qMin(m_start.y(), m_end.y()) - margin;
    int maxY = qMax(m_start.y(), m_end.y()) + margin;

    return QRect(minX, minY, maxX - minX, maxY - minY);
}

std::unique_ptr<AnnotationItem> ArrowAnnotation::clone() const
{
    return std::make_unique<ArrowAnnotation>(m_start, m_end, m_color, m_width, m_lineEndStyle, m_lineStyle);
}

void ArrowAnnotation::setEnd(const QPoint &end)
{
    m_end = end;
}
