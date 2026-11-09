#ifndef ANNOTATIONITEM_H
#define ANNOTATIONITEM_H

#include <QPainter>
#include <QPixmap>
#include <QRect>
#include <cstddef>
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

    // Approximate memory retained exclusively by this annotation. History uses
    // this constant-time estimate for its soft byte limit; subclasses with
    // dynamically allocated data should override it. Shared source images must
    // not be counted here because history does not duplicate them.
    virtual std::size_t estimatedRetainedBytes() const { return sizeof(AnnotationItem); }

    void setVisible(bool visible) { m_visible = visible; }
    bool isVisible() const { return m_visible; }

protected:
    static std::size_t estimatedPixmapBytes(const QPixmap& pixmap)
    {
        if (pixmap.isNull()) return 0;
        return static_cast<std::size_t>(pixmap.width())
            * static_cast<std::size_t>(pixmap.height())
            * static_cast<std::size_t>(pixmap.depth()) / 8u;
    }

    bool m_visible = true;
};

#endif // ANNOTATIONITEM_H
