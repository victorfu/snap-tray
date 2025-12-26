#ifndef ANNOTATIONLAYER_H
#define ANNOTATIONLAYER_H

#include <QObject>
#include <QPainter>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QColor>
#include <QFont>
#include <QPixmap>
#include <QPolygonF>
#include <QTransform>
#include <memory>
#include <vector>

#include "annotations/AnnotationItem.h"
#include "annotations/PencilStroke.h"
#include "annotations/MarkerStroke.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/MosaicStroke.h"
#include "annotations/StepBadgeAnnotation.h"
#include "annotations/ErasedItemsGroup.h"

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

// Annotation layer that manages all annotations with undo/redo
class AnnotationLayer : public QObject
{
    Q_OBJECT

public:
    explicit AnnotationLayer(QObject *parent = nullptr);
    ~AnnotationLayer();

    void addItem(std::unique_ptr<AnnotationItem> item);
    void undo();
    void redo();
    void clear();
    void draw(QPainter &painter) const;

    bool canUndo() const;
    bool canRedo() const;
    bool isEmpty() const;
    size_t itemCount() const { return m_items.size(); }

    // Access item by index (for re-editing)
    AnnotationItem* itemAt(int index);

    // For rendering annotations onto the final image
    void drawOntoPixmap(QPixmap &pixmap) const;

    // Step badge helpers
    int countStepBadges() const;

    // Eraser support: remove items that intersect with the given path
    // Returns the removed items with their original indices (for undo support)
    std::vector<ErasedItemsGroup::IndexedItem> removeItemsIntersecting(const QPoint &point, int strokeWidth);

    // Selection support for text annotations
    int hitTestText(const QPoint &pos) const;
    void setSelectedIndex(int index) { m_selectedIndex = index; }
    int selectedIndex() const { return m_selectedIndex; }
    AnnotationItem* selectedItem();
    void clearSelection() { m_selectedIndex = -1; }
    bool removeSelectedItem();

signals:
    void changed();

private:
    static constexpr size_t kMaxHistorySize = 50;

    void trimHistory();
    void renumberStepBadges();

    std::vector<std::unique_ptr<AnnotationItem>> m_items;
    std::vector<std::unique_ptr<AnnotationItem>> m_redoStack;
    int m_selectedIndex = -1;
};

#endif // ANNOTATIONLAYER_H
