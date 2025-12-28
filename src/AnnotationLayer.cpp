#include "annotations/AnnotationLayer.h"
#include "annotations/TextAnnotation.h"
#include "annotations/PencilStroke.h"
#include "annotations/MarkerStroke.h"
#include "annotations/MosaicStroke.h"
#include "annotations/StepBadgeAnnotation.h"
#include "annotations/EraserStroke.h"
#include "annotations/ErasedItemsGroup.h"
#include <QImage>
#include <QPixmap>
#include <QPainterPath>
#include <QDebug>
#include <QtMath>
#include <algorithm>

AnnotationLayer::AnnotationLayer(QObject *parent)
    : QObject(parent)
{
}

AnnotationLayer::~AnnotationLayer() = default;

void AnnotationLayer::addItem(std::unique_ptr<AnnotationItem> item)
{
    m_items.push_back(std::move(item));
    m_redoStack.clear();  // Clear redo stack when new item is added
    trimHistory();
    emit changed();
}

void AnnotationLayer::trimHistory()
{
    size_t trimCount = 0;
    while (m_items.size() > kMaxHistorySize) {
        m_items.erase(m_items.begin());
        ++trimCount;
    }
    if (trimCount > 0) {
        // Adjust stored indices in all ErasedItemsGroups
        for (auto& item : m_items) {
            if (auto* group = dynamic_cast<ErasedItemsGroup*>(item.get())) {
                group->adjustIndicesForTrim(trimCount);
            }
        }
        for (auto& item : m_redoStack) {
            if (auto* group = dynamic_cast<ErasedItemsGroup*>(item.get())) {
                group->adjustIndicesForTrim(trimCount);
            }
        }
        renumberStepBadges();
    }
}

void AnnotationLayer::undo()
{
    if (m_items.empty()) return;

    // Check if the last item is an ErasedItemsGroup (from eraser action)
    if (auto* erasedGroup = dynamic_cast<ErasedItemsGroup*>(m_items.back().get())) {
        // Extract the erased items with their original indices
        auto restoredItems = erasedGroup->extractItems();

        // Store original indices for redo support
        std::vector<size_t> indices;
        indices.reserve(restoredItems.size());
        for (const auto& item : restoredItems) {
            indices.push_back(item.originalIndex);
        }
        erasedGroup->setOriginalIndices(std::move(indices));

        // Move the empty group to redo stack
        m_redoStack.push_back(std::move(m_items.back()));
        m_items.pop_back();

        // Sort by original index ascending to restore in correct order
        std::sort(restoredItems.begin(), restoredItems.end(),
            [](const auto& a, const auto& b) { return a.originalIndex < b.originalIndex; });

        // Re-insert items at their original indices
        for (auto &indexed : restoredItems) {
            size_t insertPos = std::min(indexed.originalIndex, m_items.size());
            m_items.insert(m_items.begin() + static_cast<ptrdiff_t>(insertPos), std::move(indexed.item));
        }
    } else {
        // Normal undo: move last item to redo stack
        m_redoStack.push_back(std::move(m_items.back()));
        m_items.pop_back();
    }

    renumberStepBadges();
    emit changed();
}

void AnnotationLayer::redo()
{
    if (m_redoStack.empty()) return;

    // Check if the item in redo stack is an ErasedItemsGroup
    if (auto* erasedGroup = dynamic_cast<ErasedItemsGroup*>(m_redoStack.back().get())) {
        // Get the stored original indices
        auto indices = erasedGroup->originalIndices();
        m_redoStack.pop_back();

        // Sort indices in descending order to remove from back to front
        // (prevents index shifting issues during removal)
        std::sort(indices.begin(), indices.end(), std::greater<size_t>());

        // Remove items at the stored indices
        std::vector<ErasedItemsGroup::IndexedItem> itemsToErase;
        itemsToErase.reserve(indices.size());

        for (size_t idx : indices) {
            if (idx < m_items.size() && !dynamic_cast<ErasedItemsGroup*>(m_items[idx].get())) {
                itemsToErase.push_back({idx, std::move(m_items[idx])});
                m_items.erase(m_items.begin() + static_cast<ptrdiff_t>(idx));
            }
        }

        // Reverse to restore original order (we collected in descending index order)
        std::reverse(itemsToErase.begin(), itemsToErase.end());
        m_items.push_back(std::make_unique<ErasedItemsGroup>(std::move(itemsToErase)));
    } else {
        // Normal redo
        m_items.push_back(std::move(m_redoStack.back()));
        m_redoStack.pop_back();
    }

    renumberStepBadges();
    emit changed();
}

void AnnotationLayer::clear()
{
    m_items.clear();
    m_redoStack.clear();
    emit changed();
}

void AnnotationLayer::draw(QPainter &painter) const
{
    if (m_items.empty()) return;

    bool hasEraserStroke = false;
    for (const auto &item : m_items) {
        if (dynamic_cast<const EraserStroke*>(item.get())) {
            hasEraserStroke = true;
            break;
        }
    }

    if (!hasEraserStroke) {
        for (const auto &item : m_items) {
            if (item->isVisible()) {
                item->draw(painter);
            }
        }
        return;
    }

    QSize baseSize(painter.device()->width(), painter.device()->height());
    qreal dpr = painter.device()->devicePixelRatioF();
    QSize deviceSize = baseSize;
    if (!dynamic_cast<const QPixmap*>(painter.device()) &&
        !dynamic_cast<const QImage*>(painter.device())) {
        deviceSize = QSize(qRound(baseSize.width() * dpr), qRound(baseSize.height() * dpr));
    }

    if (deviceSize.isEmpty()) return;

    QImage layer(deviceSize, QImage::Format_ARGB32_Premultiplied);
    layer.setDevicePixelRatio(dpr);
    layer.fill(Qt::transparent);

    QPainter layerPainter(&layer);
    layerPainter.setRenderHints(painter.renderHints(), true);

    for (const auto &item : m_items) {
        if (item->isVisible()) {
            item->draw(layerPainter);
        }
    }

    layerPainter.end();
    painter.drawImage(QPoint(0, 0), layer);
}

bool AnnotationLayer::canUndo() const
{
    return !m_items.empty();
}

bool AnnotationLayer::canRedo() const
{
    return !m_redoStack.empty();
}

bool AnnotationLayer::isEmpty() const
{
    return m_items.empty();
}

void AnnotationLayer::drawOntoPixmap(QPixmap &pixmap) const
{
    if (isEmpty()) return;

    QPainter painter(&pixmap);
    draw(painter);
}

int AnnotationLayer::countStepBadges() const
{
    int count = 0;
    for (const auto &item : m_items) {
        if (dynamic_cast<const StepBadgeAnnotation*>(item.get())) {
            ++count;
        }
    }
    return count;
}

void AnnotationLayer::renumberStepBadges()
{
    int badgeNumber = 1;
    for (auto &item : m_items) {
        if (auto* badge = dynamic_cast<StepBadgeAnnotation*>(item.get())) {
            badge->setNumber(badgeNumber++);
        }
    }
}

std::vector<ErasedItemsGroup::IndexedItem> AnnotationLayer::removeItemsIntersecting(const QPoint &point, int strokeWidth)
{
    std::vector<ErasedItemsGroup::IndexedItem> removedItems;
    int radius = strokeWidth / 2;
    size_t currentIndex = 0;

    for (auto it = m_items.begin(); it != m_items.end(); ) {
        // Skip ErasedItemsGroup items (they are invisible markers)
        if (dynamic_cast<ErasedItemsGroup*>(it->get())) {
            ++it;
            ++currentIndex;
            continue;
        }

        bool shouldRemove = false;

        // Use path-based intersection for strokes (more accurate)
        if (auto* pencil = dynamic_cast<PencilStroke*>(it->get())) {
            shouldRemove = pencil->intersectsCircle(point, radius);
        } else if (auto* marker = dynamic_cast<MarkerStroke*>(it->get())) {
            shouldRemove = marker->intersectsCircle(point, radius);
        } else {
            // Fallback: expanded bounding rect for shapes/text/badges/etc.
            QRect itemRect = (*it)->boundingRect();
            QRect expandedRect = itemRect.adjusted(-radius, -radius, radius, radius);
            shouldRemove = expandedRect.contains(point);
        }

        if (shouldRemove) {
            // Item intersects with eraser - remove it and record original index
            removedItems.push_back({currentIndex, std::move(*it)});
            it = m_items.erase(it);
            // Don't increment currentIndex since we erased
        } else {
            ++it;
            ++currentIndex;
        }
    }

    if (!removedItems.empty()) {
        m_redoStack.clear();  // Clear redo stack when items are erased
        renumberStepBadges();
        emit changed();
    }

    return removedItems;
}

int AnnotationLayer::hitTestText(const QPoint &pos) const
{
    // Iterate in reverse order (top-most items first)
    for (int i = static_cast<int>(m_items.size()) - 1; i >= 0; --i) {
        // Only hit-test TextAnnotation items
        if (auto* textItem = dynamic_cast<TextAnnotation*>(m_items[i].get())) {
            // Use containsPoint() for accurate hit-testing of rotated/scaled text
            if (textItem->containsPoint(pos)) {
                return i;
            }
        }
    }
    return -1;
}

AnnotationItem* AnnotationLayer::selectedItem()
{
    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_items.size())) {
        return m_items[m_selectedIndex].get();
    }
    return nullptr;
}

AnnotationItem* AnnotationLayer::itemAt(int index)
{
    if (index >= 0 && index < static_cast<int>(m_items.size())) {
        return m_items[index].get();
    }
    return nullptr;
}

bool AnnotationLayer::removeSelectedItem()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_items.size())) {
        return false;
    }

    // Use ErasedItemsGroup for proper undo/redo support (same pattern as eraser)
    std::vector<ErasedItemsGroup::IndexedItem> removedItems;
    removedItems.push_back({static_cast<size_t>(m_selectedIndex), std::move(m_items[m_selectedIndex])});
    m_items.erase(m_items.begin() + m_selectedIndex);

    // Add ErasedItemsGroup to track the deletion for undo
    m_items.push_back(std::make_unique<ErasedItemsGroup>(std::move(removedItems)));

    m_redoStack.clear();
    m_selectedIndex = -1;
    renumberStepBadges();
    emit changed();
    return true;
}
