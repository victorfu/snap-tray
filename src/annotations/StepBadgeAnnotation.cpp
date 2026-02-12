#include "annotations/StepBadgeAnnotation.h"
#include <QPainter>
#include <QFont>

// ============================================================================
// StepBadgeAnnotation Implementation
// ============================================================================

int StepBadgeAnnotation::radiusForSize(StepBadgeSize size)
{
    switch (size) {
    case StepBadgeSize::Small:
        return kBadgeRadiusSmall;
    case StepBadgeSize::Large:
        return kBadgeRadiusLarge;
    default:
        return kBadgeRadiusMedium;
    }
}

StepBadgeAnnotation::StepBadgeAnnotation(const QPoint &position, const QColor &color,
                                         int number, int radius)
    : m_position(position)
    , m_color(color)
    , m_number(number)
    , m_radius(radius)
{
}

void StepBadgeAnnotation::draw(QPainter &painter) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw filled circle with annotation color (rotates with canvas - OK since circle is symmetric)
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_color);
    painter.drawEllipse(m_position, m_radius, m_radius);

    // Draw white number in center
    // Scale font proportionally: 8pt for small (r=10), 12pt for medium (r=14), 17pt for large (r=20)
    painter.setPen(Qt::white);
    QFont font;
    int fontSize = (m_radius * 12) / kBadgeRadiusMedium;
    font.setPointSize(fontSize);
    font.setBold(true);
    painter.setFont(font);

    QRect textRect(m_position.x() - m_radius, m_position.y() - m_radius,
                   m_radius * 2, m_radius * 2);
    painter.drawText(textRect, Qt::AlignCenter, QString::number(m_number));

    painter.restore();
}

QRect StepBadgeAnnotation::boundingRect() const
{
    int margin = m_radius + 2;
    return QRect(m_position.x() - margin, m_position.y() - margin,
                 margin * 2, margin * 2);
}

std::unique_ptr<AnnotationItem> StepBadgeAnnotation::clone() const
{
    return std::make_unique<StepBadgeAnnotation>(m_position, m_color, m_number, m_radius);
}

void StepBadgeAnnotation::setNumber(int number)
{
    m_number = number;
}
