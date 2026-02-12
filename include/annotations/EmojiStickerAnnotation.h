#ifndef EMOJISTICKERANNOTATION_H
#define EMOJISTICKERANNOTATION_H

#include "annotations/AnnotationItem.h"
#include <QPoint>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QTransform>

class EmojiStickerAnnotation : public AnnotationItem
{
public:
    static constexpr int kBaseSize = 32;
    static constexpr qreal kMinScale = 0.25;
    static constexpr qreal kMaxScale = 4.0;

    EmojiStickerAnnotation(const QPoint &position, const QString &emoji, qreal scale = 1.0);

    void draw(QPainter &painter) const override;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;
    void translate(const QPointF& delta) override;

    // Selection support
    bool containsPoint(const QPoint &point) const;

    // Getters
    QPoint position() const { return m_position; }
    QString emoji() const { return m_emoji; }
    qreal scale() const { return m_scale; }
    qreal rotation() const { return m_rotation; }
    bool mirrorX() const { return m_mirrorX; }
    bool mirrorY() const { return m_mirrorY; }

    // Transformation
    void setPosition(const QPoint &position) { m_position = position; }
    void setScale(qreal scale);
    void setRotation(qreal degrees);
    void setMirror(bool mirrorX, bool mirrorY);
    void moveBy(const QPoint &delta) { m_position += delta; }

    // Geometry helpers for gizmo
    QPointF center() const;
    QPolygonF transformedBoundingPolygon() const;

private:
    QPoint m_position;
    QString m_emoji;
    qreal m_scale = 1.0;
    qreal m_rotation = 0.0;
    bool m_mirrorX = false;
    bool m_mirrorY = false;

    QSize emojiSize() const;
    QRectF glyphRect() const;
    QTransform localLinearTransform() const;
};

#endif // EMOJISTICKERANNOTATION_H
