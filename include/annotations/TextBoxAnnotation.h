#ifndef TEXTBOXANNOTATION_H
#define TEXTBOXANNOTATION_H

#include "annotations/AnnotationItem.h"
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QFont>
#include <QColor>
#include <QPolygonF>
#include <QPixmap>
#include <QTransform>
#include <memory>

/**
 * @brief Text annotation with box-bounded word-wrap and resize support.
 *
 * Unlike TextAnnotation which uses uniform scaling, TextBoxAnnotation:
 * - Has a resizable box that text wraps within
 * - Corner drag = resize box (text reflows), NOT scale
 * - Supports rotation around box center
 */
class TextBoxAnnotation : public AnnotationItem
{
public:
    static constexpr int kMinWidth = 50;
    static constexpr int kMinHeight = 20;
    static constexpr int kDefaultWidth = 80;
    static constexpr int kPadding = 4;
    static constexpr int kHitMargin = 8;   // Margin for easier click detection

    /**
     * @brief Construct with position and text (box auto-sized).
     * @param position Top-left corner of the text box in world coordinates.
     * @param text The text content.
     * @param font Font to use for rendering.
     * @param color Text color.
     */
    TextBoxAnnotation(const QPointF &position, const QString &text,
                      const QFont &font, const QColor &color);

    // AnnotationItem interface
    void draw(QPainter &painter) const override;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;

    // Text manipulation
    void setText(const QString &text);
    QString text() const { return m_text; }

    // Box manipulation (for resize operations)
    QRectF box() const { return m_box; }

    // Position (top-left of box in world coordinates)
    void setPosition(const QPointF &position);
    QPointF position() const { return m_position; }
    void moveBy(const QPointF &delta);

    // For compatibility with existing code expecting QPoint
    void moveBy(const QPoint &delta) { moveBy(QPointF(delta)); }

    // Font/Color
    void setFont(const QFont &font);
    QFont font() const { return m_font; }
    void setColor(const QColor &color);
    QColor color() const { return m_color; }

    // Rotation (around box center)
    void setRotation(qreal degrees);
    qreal rotation() const { return m_rotation; }

    // Scale (uniform scaling around center)
    void setScale(qreal scale);
    qreal scale() const { return m_scale; }

    // Mirror (around box center)
    void setMirror(bool mirrorX, bool mirrorY);
    bool mirrorX() const { return m_mirrorX; }
    bool mirrorY() const { return m_mirrorY; }

    // Geometry helpers for hit-testing and gizmo
    QPointF center() const;
    QPolygonF transformedBoundingPolygon() const;
    bool containsPoint(const QPoint &point) const;

    // Coordinate conversion helpers in annotation space.
    // localPoint is relative to the untransformed top-left of the text box.
    QPointF mapLocalPointToTransformed(const QPointF& localPoint) const;
    QPointF topLeftFromTransformedLocalPoint(const QPointF& transformedPoint,
                                             const QPointF& localPoint) const;

private:
    QPointF m_position;      // Top-left anchor in world coordinates
    QRectF m_box;            // Local rect (0,0 to width,height)
    QString m_text;
    QFont m_font;
    QColor m_color;
    qreal m_rotation = 0.0;  // Degrees, clockwise
    qreal m_scale = 1.0;     // Uniform scale factor
    bool m_mirrorX = false;
    bool m_mirrorY = false;

    // Pixmap cache (DPR-aware)
    mutable QPixmap m_cachedPixmap;
    mutable QPointF m_cachedOrigin;
    mutable qreal m_cachedDpr = 0.0;
    mutable QString m_cachedText;
    mutable QFont m_cachedFont;
    mutable QColor m_cachedColor;
    mutable QRectF m_cachedBox;

    void regenerateCache(qreal dpr) const;
    bool isCacheValid(qreal dpr) const;
    void invalidateCache() const { m_cachedPixmap = QPixmap(); }

    // Word-wrap helper
    QStringList wrapText() const;

    // Calculate initial box size based on text
    void calculateInitialBox();

    // Local linear transform (rotation + mirror + scale).
    QTransform localLinearTransform() const;
};

#endif // TEXTBOXANNOTATION_H
