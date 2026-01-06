#ifndef EMOJISTICKERANNOTATION_H
#define EMOJISTICKERANNOTATION_H

#include "annotations/AnnotationItem.h"
#include <QPoint>
#include <QPointF>
#include <QPolygonF>
#include <QString>

class EmojiStickerAnnotation : public AnnotationItem
{
public:
    static constexpr int kBaseSize = 48;
    static constexpr qreal kMinScale = 0.25;
    static constexpr qreal kMaxScale = 4.0;

    EmojiStickerAnnotation(const QPoint &position, const QString &emoji, qreal scale = 1.0);

    void draw(QPainter &painter) const override;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;

    // Selection support
    bool containsPoint(const QPoint &point) const;

    // Getters
    QPoint position() const { return m_position; }
    QString emoji() const { return m_emoji; }
    qreal scale() const { return m_scale; }

    // Transformation
    void setPosition(const QPoint &position) { m_position = position; }
    void setScale(qreal scale);
    void moveBy(const QPoint &delta) { m_position += delta; }

    // Geometry helpers for gizmo
    QPointF center() const;
    QPolygonF transformedBoundingPolygon() const;

private:
    QPoint m_position;
    QString m_emoji;
    qreal m_scale = 1.0;

    QSize emojiSize() const;
};

#endif // EMOJISTICKERANNOTATION_H
