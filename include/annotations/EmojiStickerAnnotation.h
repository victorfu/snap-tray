#ifndef EMOJISTICKERANNOTATION_H
#define EMOJISTICKERANNOTATION_H

#include "annotations/AnnotationItem.h"
#include <QPoint>
#include <QPointF>
#include <QPolygonF>
#include <QPixmap>
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

    mutable QPixmap m_cachedPixmap;
    mutable QPointF m_cachedOrigin;
    mutable QRectF m_cachedBaseGlyphRect;
    mutable qreal m_cachedDpr = 0.0;
    mutable QString m_cachedEmoji;

    QSize emojiSize() const;
    QRectF glyphRect() const;
    QTransform localLinearTransform() const;
    void regenerateCache(qreal dpr) const;
    bool isCacheValid(qreal dpr) const;
    void invalidateCache() const { m_cachedPixmap = QPixmap(); }
};

#endif // EMOJISTICKERANNOTATION_H
