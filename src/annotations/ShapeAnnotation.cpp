#include "annotations/ShapeAnnotation.h"
#include <QPainter>

ShapeAnnotation::ShapeAnnotation(const QRect &rect, ShapeType type,
                                 const QColor &color, int width, bool filled)
    : m_rect(rect)
    , m_type(type)
    , m_color(color)
    , m_width(width)
    , m_filled(filled)
{
}

void ShapeAnnotation::draw(QPainter &painter) const
{
    painter.save();

    QRect normalizedRect = m_rect.normalized();

    if (m_type == ShapeType::Rectangle) {
        QPen pen(m_color, m_width, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
        painter.setPen(pen);
        painter.setRenderHint(QPainter::Antialiasing, true);

        if (m_filled) {
            painter.setBrush(m_color);
        } else {
            painter.setBrush(Qt::NoBrush);
        }

        painter.drawRect(normalizedRect);
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

        painter.drawEllipse(normalizedRect);
    }

    painter.restore();
}

QRect ShapeAnnotation::boundingRect() const
{
    int margin = m_width / 2 + 1;
    return m_rect.normalized().adjusted(-margin, -margin, margin, margin);
}

std::unique_ptr<AnnotationItem> ShapeAnnotation::clone() const
{
    return std::make_unique<ShapeAnnotation>(m_rect, m_type, m_color, m_width, m_filled);
}

void ShapeAnnotation::setRect(const QRect &rect)
{
    m_rect = rect;
}
