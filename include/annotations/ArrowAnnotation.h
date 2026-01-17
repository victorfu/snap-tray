#ifndef ARROWANNOTATION_H
#define ARROWANNOTATION_H

#include "AnnotationItem.h"
#include "LineStyle.h"
#include <QPoint>
#include <QColor>

// Line end style for arrow annotations
enum class LineEndStyle {
    None = 0,        // Plain line (no arrows)
    EndArrow,        // Filled triangle at end (default)
    EndArrowOutline, // Outline triangle at end
    EndArrowLine,    // V-line at end (two lines, no fill)
    BothArrow,       // Filled triangles at both ends
    BothArrowOutline // Outline triangles at both ends
};

/**
 * @brief Arrow annotation (line with optional arrowhead) - supports Quadratic Bézier curves.
 *
 * The arrow is defined by three points:
 * - Start point (m_start)
 * - End point (m_end)
 * - Control point (m_controlPoint) - determines the curvature
 *
 * When the control point is at the midpoint of start/end, the arrow appears straight.
 * Moving the control point creates a smooth curve.
 */
class ArrowAnnotation : public AnnotationItem
{
public:
    ArrowAnnotation(const QPoint &start, const QPoint &end, const QColor &color, int width,
                    LineEndStyle style = LineEndStyle::EndArrow,
                    LineStyle lineStyle = LineStyle::Solid);

    void draw(QPainter &painter) const override;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;

    // Point accessors
    void setStart(const QPoint &start);
    void setEnd(const QPoint &end);
    void setControlPoint(const QPoint &p);

    QPoint start() const { return m_start; }
    QPoint end() const { return m_end; }
    QPoint controlPoint() const { return m_controlPoint; }

    // Line end style
    void setLineEndStyle(LineEndStyle style) { m_lineEndStyle = style; }
    LineEndStyle lineEndStyle() const { return m_lineEndStyle; }

    /**
     * @brief Check if a point lies on or near the curve.
     * Uses QPainterPathStroker for accurate hit testing on the Bézier curve.
     * @param pos The point to test
     * @return true if the point is within hit tolerance of the curve
     */
    bool containsPoint(const QPoint &pos) const;

    /**
     * @brief Check if the curve is actually curved (control point is off the straight line).
     * @return true if control point deviates from the line segment by more than tolerance
     */
    bool isCurved() const;

    /**
     * @brief Move all points by the given delta.
     * Used for moving the entire annotation.
     */
    void moveBy(const QPoint &delta);

private:
    // Arrowhead drawing helpers
    void drawArrowheadAtAngle(QPainter &painter, const QPoint &tip, double angle, bool filled) const;
    void drawArrowheadLineAtAngle(QPainter &painter, const QPoint &tip, double angle) const;

    // Tangent angle calculations for Bézier curve
    double endTangentAngle() const;
    double startTangentAngle() const;

    QPoint m_start;
    QPoint m_end;
    QPoint m_controlPoint;
    QColor m_color;
    int m_width;
    LineEndStyle m_lineEndStyle;
    LineStyle m_lineStyle;

    // Hit testing tolerance
    static constexpr int kHitTolerance = 10;
};

#endif // ARROWANNOTATION_H
