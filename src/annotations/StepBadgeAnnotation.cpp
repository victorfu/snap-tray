#include "annotations/StepBadgeAnnotation.h"
#include <QPainter>
#include <QFont>
#include <cmath>

namespace {
constexpr qreal kStepBadgeBrightFillLumaThreshold = 0.75;

qreal rec709Luma(const QColor& color)
{
    return (0.2126 * color.redF()) + (0.7152 * color.greenF()) + (0.0722 * color.blueF());
}

QColor stepBadgeTextColorForFill(const QColor& fillColor)
{
    return rec709Luma(fillColor) >= kStepBadgeBrightFillLumaThreshold ? Qt::black : Qt::white;
}
} // namespace

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

void StepBadgeAnnotation::setRotation(qreal degrees)
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

void StepBadgeAnnotation::setMirror(bool mirrorX, bool mirrorY)
{
    m_mirrorX = mirrorX;
    m_mirrorY = mirrorY;
}

void StepBadgeAnnotation::draw(QPainter &painter) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.translate(m_position);
    if (!qFuzzyIsNull(m_rotation)) {
        painter.rotate(m_rotation);
    }
    if (m_mirrorX || m_mirrorY) {
        painter.scale(m_mirrorX ? -1.0 : 1.0, m_mirrorY ? -1.0 : 1.0);
    }
    painter.translate(-m_position);

    // Draw filled circle with annotation color (rotates with canvas - OK since circle is symmetric)
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_color);
    painter.drawEllipse(m_position, m_radius, m_radius);

    // Draw number in center with a contrast color based on badge fill.
    // Scale font proportionally: 8pt for small (r=10), 12pt for medium (r=14), 17pt for large (r=20)
    painter.setPen(stepBadgeTextColorForFill(m_color));
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
    auto cloned = std::make_unique<StepBadgeAnnotation>(m_position, m_color, m_number, m_radius);
    cloned->setRotation(m_rotation);
    cloned->setMirror(m_mirrorX, m_mirrorY);
    return cloned;
}

void StepBadgeAnnotation::setNumber(int number)
{
    m_number = number;
}
