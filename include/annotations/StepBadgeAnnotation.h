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

    void setNumber(int number);
    int number() const { return m_number; }
    int radius() const { return m_radius; }

    /**
     * @brief Get the radius value for a given size
     */
    static int radiusForSize(StepBadgeSize size);

private:
    QPoint m_position;
    QColor m_color;
    int m_number;
    int m_radius;
};

#endif // STEPBADGEANNOTATION_H
