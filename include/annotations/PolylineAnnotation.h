#ifndef POLYLINEANNOTATION_H
#define POLYLINEANNOTATION_H

#include "AnnotationItem.h"
#include "ArrowAnnotation.h"  // For LineEndStyle
#include "LineStyle.h"

#include <QVector>
#include <QPoint>
#include <QColor>

/**
 * @brief Polyline annotation (multi-segment line with optional arrowhead)
 *
 * Users click to add points, double-click to finish.
 * Supports optional arrowhead at the end point.
 */
class PolylineAnnotation : public AnnotationItem
{
public:
    PolylineAnnotation(const QColor& color, int width,
                       LineEndStyle style = LineEndStyle::EndArrow,
                       LineStyle lineStyle = LineStyle::Solid);

    PolylineAnnotation(const QVector<QPoint>& points, const QColor& color, int width,
                       LineEndStyle style = LineEndStyle::EndArrow,
                       LineStyle lineStyle = LineStyle::Solid);

    void draw(QPainter& painter) const override;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;
    void translate(const QPointF& delta) override;
    bool containsPoint(const QPoint& point) const;

    // Point management
    void addPoint(const QPoint& point);
    void updateLastPoint(const QPoint& point);
    void setPoint(int index, const QPoint& point);
    void removeLastPoint();
    void moveBy(const QPoint& delta);
    int pointCount() const { return m_points.size(); }
    QVector<QPoint> points() const { return m_points; }

    // Style accessors
    void setLineEndStyle(LineEndStyle style) { m_lineEndStyle = style; }
    LineEndStyle lineEndStyle() const { return m_lineEndStyle; }
    QColor color() const { return m_color; }
    int width() const { return m_width; }

private:
    void drawArrowhead(QPainter& painter, const QPoint& from, const QPoint& to, bool filled) const;
    void drawArrowheadLine(QPainter& painter, const QPoint& from, const QPoint& to) const;

    QVector<QPoint> m_points;
    QColor m_color;
    int m_width;
    LineEndStyle m_lineEndStyle;
    LineStyle m_lineStyle;
};

#endif // POLYLINEANNOTATION_H
