#ifndef ANNOTATIONLAYER_H
#define ANNOTATIONLAYER_H

#include <QObject>
#include <QPainter>
#include <QPoint>
#include <QRect>
#include <QPixmap>
#include <memory>
#include <vector>

#include "annotations/AnnotationItem.h"
#include "annotations/ErasedItemsGroup.h"

// Forward declarations
class TextAnnotation;
class EmojiStickerAnnotation;

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

    // Selection support for text/emoji annotations
    int hitTestText(const QPoint &pos) const;
    int hitTestEmojiSticker(const QPoint &pos) const;
    void setSelectedIndex(int index) { m_selectedIndex = index; }
    int selectedIndex() const { return m_selectedIndex; }
    AnnotationItem* selectedItem();
    void clearSelection() { m_selectedIndex = -1; }
    bool removeSelectedItem();

    // Cache management for rendering optimization
    void invalidateCache();
    void drawCached(QPainter &painter, const QSize &canvasSize, qreal devicePixelRatio = 1.0) const;

signals:
    void changed();

private:
    static constexpr size_t kMaxHistorySize = 50;

    void trimHistory();
    void renumberStepBadges();

    std::vector<std::unique_ptr<AnnotationItem>> m_items;
    std::vector<std::unique_ptr<AnnotationItem>> m_redoStack;
    int m_selectedIndex = -1;

    // Completed annotations cache for rendering optimization
    mutable QPixmap m_annotationCache;
    mutable bool m_cacheValid = false;
};

#endif // ANNOTATIONLAYER_H
