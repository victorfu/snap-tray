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

    // Calculate line endpoint (adjust if arrowhead is present)
    QPointF lineEnd = m_end;
    if (m_lineEndStyle != LineEndStyle::None) {
        // Move line end back to arrowhead base so line doesn't protrude through arrow
        double angle = qAtan2(m_end.y() - m_start.y(), m_end.x() - m_start.x());
        double arrowLength = qMax(15.0, m_width * 5.0);
        lineEnd = QPointF(
            m_end.x() - arrowLength * qCos(angle),
            m_end.y() - arrowLength * qSin(angle)
        );
    }

    // Draw the line (to correct endpoint)
    painter.drawLine(m_start, lineEnd.toPoint());

    // Draw arrowhead(s) based on line end style
    switch (m_lineEndStyle) {
    case LineEndStyle::None:
        // Plain line, no arrowheads
        break;
    case LineEndStyle::EndArrow:
        drawArrowhead(painter, m_start, m_end);
        break;
    }

    painter.restore();
}

void ArrowAnnotation::drawArrowhead(QPainter &painter, const QPoint &start, const QPoint &end) const
{
    // Calculate the angle of the line
    double angle = qAtan2(end.y() - start.y(), end.x() - start.x());

    // Arrowhead size proportional to line width
    double arrowLength = qMax(15.0, m_width * 5.0);
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

    // Draw filled arrowhead with solid outline (not affected by line style)
    QPainterPath arrowPath;
    arrowPath.moveTo(end);
    arrowPath.lineTo(arrowP1);
    arrowPath.lineTo(arrowP2);
    arrowPath.closeSubpath();

    // Use solid pen for arrowhead outline
    QPen solidPen(m_color, m_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(solidPen);
    painter.setBrush(m_color);
    painter.drawPath(arrowPath);
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
