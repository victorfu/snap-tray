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

// Line end style for arrow annotations
enum class LineEndStyle {
    None = 0,    // ───── Plain line (no arrows)
    EndArrow     // ─────▶ Arrow at end (default)
};

// Arrow annotation (line with arrowhead)
class ArrowAnnotation : public AnnotationItem
{
public:
    ArrowAnnotation(const QPoint &start, const QPoint &end, const QColor &color, int width,
                    LineEndStyle style = LineEndStyle::EndArrow);
    void draw(QPainter &painter) const override;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;

    void setEnd(const QPoint &end);
    QPoint start() const { return m_start; }
    QPoint end() const { return m_end; }
    void setLineEndStyle(LineEndStyle style) { m_lineEndStyle = style; }
    LineEndStyle lineEndStyle() const { return m_lineEndStyle; }

private:
    void drawArrowhead(QPainter &painter, const QPoint &start, const QPoint &end) const;

    QPoint m_start;
    QPoint m_end;
    QColor m_color;
    int m_width;
    LineEndStyle m_lineEndStyle;
};

// Rectangle annotation
class RectangleAnnotation : public AnnotationItem
{
public:
    RectangleAnnotation(const QRect &rect, const QColor &color, int width, bool filled = false);
    void draw(QPainter &painter) const override;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;

    void setRect(const QRect &rect);

private:
    QRect m_rect;
    QColor m_color;
    int m_width;
    bool m_filled;
};

// Ellipse/Circle annotation
class EllipseAnnotation : public AnnotationItem
{
public:
    EllipseAnnotation(const QRect &rect, const QColor &color, int width, bool filled = false);
    void draw(QPainter &painter) const override;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;

    void setRect(const QRect &rect);

private:
    QRect m_rect;
    QColor m_color;
    int m_width;
    bool m_filled;
};

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

// Step badge annotation (auto-incrementing numbered circle)
class StepBadgeAnnotation : public AnnotationItem
{
public:
    StepBadgeAnnotation(const QPoint &position, const QColor &color, int number = 1);
    void draw(QPainter &painter) const override;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;

    void setNumber(int number);
    int number() const { return m_number; }
    static constexpr int kBadgeRadius = 14;

private:
    QPoint m_position;
    QColor m_color;
    int m_number;
};

// Freehand mosaic stroke with classic pixelation (block averaging)
class MosaicStroke : public AnnotationItem
{
public:
    MosaicStroke(const QVector<QPoint> &points, const QPixmap &sourcePixmap,
                 int width = 24, int blockSize = 12);
    void draw(QPainter &painter) const override;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;

    void addPoint(const QPoint &point);
    void updateSource(const QPixmap &sourcePixmap);

private:
    QVector<QPoint> m_points;
    QPixmap m_sourcePixmap;
    mutable QImage m_sourceImageCache;
    int m_width;      // Brush width
    int m_blockSize;  // Mosaic block size
    qreal m_devicePixelRatio;

    // Performance optimization: rendered result cache
    mutable QPixmap m_renderedCache;
    mutable int m_cachedPointCount = 0;
    mutable QRect m_cachedBounds;
    mutable qreal m_cachedDpr = 0.0;

    // Pixelated mosaic algorithm aligned to the source image grid
    QImage applyPixelatedMosaic(const QRect &strokeBounds) const;
    QRgb calculateBlockAverageColor(const QImage &image, int x, int y,
                                     int blockW, int blockH) const;
};

// Group of erased items (for undo support)
class ErasedItemsGroup : public AnnotationItem
{
public:
    // Item paired with its original index for position-preserving undo
    struct IndexedItem {
        size_t originalIndex;
        std::unique_ptr<AnnotationItem> item;
    };

    explicit ErasedItemsGroup(std::vector<IndexedItem> items);
    void draw(QPainter &painter) const override;  // Does nothing (invisible marker)
    QRect boundingRect() const override;  // Returns empty rect
    std::unique_ptr<AnnotationItem> clone() const override;

    // Check if this group contains any items
    bool hasItems() const { return !m_erasedItems.empty(); }

    // Extract items for restoration (moves ownership)
    std::vector<IndexedItem> extractItems();

    // Track original indices for redo support (stored when items are extracted)
    const std::vector<size_t>& originalIndices() const { return m_originalIndices; }
    void setOriginalIndices(std::vector<size_t> indices) { m_originalIndices = std::move(indices); }

    // Adjust stored indices when trimHistory() removes items from front
    void adjustIndicesForTrim(size_t trimCount);

private:
    std::vector<IndexedItem> m_erasedItems;
    std::vector<size_t> m_originalIndices;  // Stored after extractItems() for redo
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
