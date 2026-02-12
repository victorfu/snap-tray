#ifndef STEPBADGEANNOTATION_H
#define STEPBADGEANNOTATION_H

#include "AnnotationItem.h"
#include <QPoint>
#include <QColor>

/**
 * @brief Step badge size options
 */
enum class StepBadgeSize {
    Small = 0,
    Medium = 1,
    Large = 2
};

/**
 * @brief Step badge annotation (auto-incrementing numbered circle)
 */
class StepBadgeAnnotation : public AnnotationItem
{
public:
    // Size constants
    static constexpr int kBadgeRadiusSmall = 10;
    static constexpr int kBadgeRadiusMedium = 14;
    static constexpr int kBadgeRadiusLarge = 20;

    StepBadgeAnnotation(const QPoint &position, const QColor &color,
                        int number = 1, int radius = kBadgeRadiusMedium);

    void draw(QPainter &painter) const override;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;
    void translate(const QPointF& delta) override;

    void setNumber(int number);
    int number() const { return m_number; }
    int radius() const { return m_radius; }
    void setRotation(qreal degrees);
    qreal rotation() const { return m_rotation; }
    void setMirror(bool mirrorX, bool mirrorY);
    bool mirrorX() const { return m_mirrorX; }
    bool mirrorY() const { return m_mirrorY; }

    /**
     * @brief Get the radius value for a given size
     */
    static int radiusForSize(StepBadgeSize size);

private:
    QPoint m_position;
    QColor m_color;
    int m_number;
    int m_radius;
    qreal m_rotation = 0.0;
    bool m_mirrorX = false;
    bool m_mirrorY = false;
};

#endif // STEPBADGEANNOTATION_H
