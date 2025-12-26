#ifndef STEPBADGEANNOTATION_H
#define STEPBADGEANNOTATION_H

#include "AnnotationItem.h"
#include <QPoint>
#include <QColor>

/**
 * @brief Step badge annotation (auto-incrementing numbered circle)
 */
class StepBadgeAnnotation : public AnnotationItem
{
public:
    StepBadgeAnnotation(const QPoint &position, const QColor &color, int number = 1);

    void draw(QPainter &painter) const override;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;

    void setNumber(int number);
    int number() const { return m_number; }

    static constexpr int kBadgeRadius = 14;

private:
    QPoint m_position;
    QColor m_color;
    int m_number;
};

#endif // STEPBADGEANNOTATION_H
