#ifndef TEXTANNOTATION_H
#define TEXTANNOTATION_H

#include "annotations/AnnotationItem.h"
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QColor>
#include <QFont>
#include <QPixmap>
#include <QPolygonF>
#include <memory>

// Text annotation with rotation and scaling support
class TextAnnotation : public AnnotationItem
{
public:
    TextAnnotation(const QPoint &position, const QString &text, const QFont &font, const QColor &color);
    void draw(QPainter &painter) const override;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;

    void setText(const QString &text);
    void moveBy(const QPoint &delta) { m_position += delta; }

    // Getters for re-editing
    QString text() const { return m_text; }
    QPoint position() const { return m_position; }
    QFont font() const { return m_font; }
    QColor color() const { return m_color; }

    // Setters for re-editing (invalidate cache on change)
    void setFont(const QFont &font) { m_font = font; invalidateCache(); }
    void setColor(const QColor &color) { m_color = color; invalidateCache(); }
    void setPosition(const QPoint &position) { m_position = position; }

    // Transformation methods
    void setRotation(qreal degrees) { m_rotation = degrees; }
    qreal rotation() const { return m_rotation; }
    void setScale(qreal scale) { m_scale = scale; }
    qreal scale() const { return m_scale; }

    // Geometry helpers for hit-testing and gizmo
    QPointF center() const;
    QPolygonF transformedBoundingPolygon() const;
    bool containsPoint(const QPoint &point) const;

private:
    QPoint m_position;
    QString m_text;
    QFont m_font;
    QColor m_color;
    qreal m_rotation = 0.0;  // Rotation angle in degrees (clockwise)
    qreal m_scale = 1.0;     // Uniform scale factor

    // Pixmap cache for rendering optimization (similar to MarkerStroke)
    mutable QPixmap m_cachedPixmap;
    mutable QPoint m_cachedOrigin;
    mutable qreal m_cachedDpr = 0.0;
    mutable QString m_cachedText;
    mutable QFont m_cachedFont;
    mutable QColor m_cachedColor;
    mutable QPoint m_cachedPosition;

    void regenerateCache(qreal dpr) const;
    bool isCacheValid(qreal dpr) const;
    void invalidateCache() const { m_cachedPixmap = QPixmap(); }
};

#endif // TEXTANNOTATION_H
