#ifndef ANNOTATIONITEM_H
#define ANNOTATIONITEM_H

#include <QPainter>
#include <QRect>
#include <memory>

/**
 * @brief 所有標註項目的抽象基類
 */
class AnnotationItem
{
public:
    virtual ~AnnotationItem() = default;
    virtual void draw(QPainter &painter) const = 0;
    virtual QRect boundingRect() const = 0;
    virtual std::unique_ptr<AnnotationItem> clone() const = 0;
    virtual void translate(const QPointF& delta) { Q_UNUSED(delta); }

    void setVisible(bool visible) { m_visible = visible; }
    bool isVisible() const { return m_visible; }

protected:
    bool m_visible = true;
};

#endif // ANNOTATIONITEM_H
