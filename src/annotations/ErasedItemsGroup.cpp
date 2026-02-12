#include "annotations/ErasedItemsGroup.h"
#include <QPainter>

// ============================================================================
// ErasedItemsGroup Implementation
// ============================================================================

ErasedItemsGroup::ErasedItemsGroup(std::vector<IndexedItem> items)
    : m_erasedItems(std::move(items))
{
}

void ErasedItemsGroup::draw(QPainter &) const
{
    // ErasedItemsGroup is invisible - it's just a marker for undo support
}

QRect ErasedItemsGroup::boundingRect() const
{
    return QRect();  // Empty rect since this is just an undo marker
}

std::unique_ptr<AnnotationItem> ErasedItemsGroup::clone() const
{
    std::vector<IndexedItem> clonedItems;
    clonedItems.reserve(m_erasedItems.size());
    for (const auto &indexed : m_erasedItems) {
        clonedItems.push_back({indexed.originalIndex, indexed.item->clone()});
    }
    return std::make_unique<ErasedItemsGroup>(std::move(clonedItems));
}

void ErasedItemsGroup::translate(const QPointF& delta)
{
    for (auto& indexed : m_erasedItems) {
        if (indexed.item) {
            indexed.item->translate(delta);
        }
    }
}

std::vector<ErasedItemsGroup::IndexedItem> ErasedItemsGroup::extractItems()
{
    return std::move(m_erasedItems);
}

void ErasedItemsGroup::adjustIndicesForTrim(size_t trimCount)
{
    for (auto& indexed : m_erasedItems) {
        if (indexed.originalIndex >= trimCount) {
            indexed.originalIndex -= trimCount;
        } else {
            indexed.originalIndex = 0;
        }
    }
    for (size_t i = 0; i < m_originalIndices.size(); ++i) {
        if (m_originalIndices[i] >= trimCount) {
            m_originalIndices[i] -= trimCount;
        } else {
            m_originalIndices[i] = 0;
        }
    }
}

void ErasedItemsGroup::forEachStoredItem(const std::function<void(AnnotationItem*)>& visitor)
{
    if (!visitor) {
        return;
    }

    for (auto& indexed : m_erasedItems) {
        visitor(indexed.item.get());
    }
}
