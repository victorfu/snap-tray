#include "annotations/StepBadgeAnnotation.h"
#include <QPainter>
#include <QFont>

// ============================================================================
// StepBadgeAnnotation Implementation
// ============================================================================

StepBadgeAnnotation::StepBadgeAnnotation(const QPoint &position, const QColor &color, int number)
    : m_position(position)
    , m_color(color)
    , m_number(number)
{
}

void StepBadgeAnnotation::draw(QPainter &painter) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw filled circle with annotation color
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_color);
    painter.drawEllipse(m_position, kBadgeRadius, kBadgeRadius);

    // Draw white number in center
    painter.setPen(Qt::white);
    QFont font;
    font.setPointSize(12);
    font.setBold(true);
    painter.setFont(font);

    QRect textRect(m_position.x() - kBadgeRadius, m_position.y() - kBadgeRadius,
                   kBadgeRadius * 2, kBadgeRadius * 2);
    painter.drawText(textRect, Qt::AlignCenter, QString::number(m_number));

    painter.restore();
}

QRect StepBadgeAnnotation::boundingRect() const
{
    int margin = kBadgeRadius + 2;
    return QRect(m_position.x() - margin, m_position.y() - margin,
                 margin * 2, margin * 2);
}

std::unique_ptr<AnnotationItem> StepBadgeAnnotation::clone() const
{
    return std::make_unique<StepBadgeAnnotation>(m_position, m_color, m_number);
}

void StepBadgeAnnotation::setNumber(int number)
{
    m_number = number;
}
