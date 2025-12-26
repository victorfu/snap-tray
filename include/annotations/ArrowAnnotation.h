#ifndef ARROWANNOTATION_H
#define ARROWANNOTATION_H

#include "AnnotationItem.h"
#include <QPoint>
#include <QColor>

// Line end style for arrow annotations
enum class LineEndStyle {
    None = 0,    // Plain line (no arrows)
    EndArrow     // Arrow at end (default)
};

/**
 * @brief Arrow annotation (line with optional arrowhead)
 */
class ArrowAnnotation : public AnnotationItem
{
public:
    ArrowAnnotation(const QPoint &start, const QPoint &end, const QColor &color, int width,
                    LineEndStyle style = LineEndStyle::EndArrow);

    void draw(QPainter &painter) const override;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;

    void setEnd(const QPoint &end);
    QPoint start() const { return m_start; }
    QPoint end() const { return m_end; }
    void setLineEndStyle(LineEndStyle style) { m_lineEndStyle = style; }
    LineEndStyle lineEndStyle() const { return m_lineEndStyle; }

private:
    void drawArrowhead(QPainter &painter, const QPoint &start, const QPoint &end) const;

    QPoint m_start;
    QPoint m_end;
    QColor m_color;
    int m_width;
    LineEndStyle m_lineEndStyle;
};

#endif // ARROWANNOTATION_H
