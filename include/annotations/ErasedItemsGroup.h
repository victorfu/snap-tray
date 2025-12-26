#ifndef ERASEDITEMSGROUP_H
#define ERASEDITEMSGROUP_H

#include "AnnotationItem.h"
#include <vector>
#include <cstddef>
#include <memory>

/**
 * @brief Group of erased items (for undo support)
 *
 * This is an invisible annotation item that stores items removed by the eraser.
 * When undo is triggered, the erased items can be restored to their original positions.
 */
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
    QRect boundingRect() const override;          // Returns empty rect
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

#endif // ERASEDITEMSGROUP_H
