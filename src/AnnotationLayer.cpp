#include "annotations/AnnotationLayer.h"
#include "annotations/TextBoxAnnotation.h"
#include "annotations/PencilStroke.h"
#include "annotations/MarkerStroke.h"
#include "annotations/MosaicStroke.h"
#include "annotations/StepBadgeAnnotation.h"
#include "annotations/EmojiStickerAnnotation.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/PolylineAnnotation.h"
#include "utils/CoordinateHelper.h"
#include <QImage>
#include <QPixmap>
#include <QPainterPath>
#include <QDebug>
#include <QSize>
#include <QtMath>
#include <algorithm>

namespace {

constexpr std::size_t kMaxTrackedCommandBytes =
    AnnotationLayer::kMaxHistoryOwnedBytes + 1;
static_assert(sizeof(std::size_t) >= sizeof(std::uint64_t),
              "Bounded annotation history requires a 64-bit address space");

void addCappedCommandBytes(std::size_t& total, std::size_t bytes)
{
    if (total >= kMaxTrackedCommandBytes
        || bytes >= kMaxTrackedCommandBytes - total) {
        total = kMaxTrackedCommandBytes;
    } else {
        total += bytes;
    }
}

void adjustRetainedByteCounter(std::size_t& counter,
                               std::size_t before,
                               std::size_t after)
{
    if (after >= before) {
        // Each command estimate is capped just above the soft limit and the
        // history contains at most kMaxHistoryCommands + 1 entries while
        // trimming, so this aggregate remains exact on supported 64-bit hosts.
        counter += after - before;
        return;
    }

    const std::size_t delta = before - after;
    counter = delta > counter ? 0 : counter - delta;
}

} // namespace

AnnotationLayer::AnnotationLayer(QObject *parent)
    : QObject(parent)
{
    // Keep one spare slot so moving a command between stacks or pushing the
    // command that triggers trimming never allocates after ownership changes.
    m_undoStack.reserve(kMaxHistoryCommands + 1);
    m_redoStack.reserve(kMaxHistoryCommands + 1);
    m_currentHistoryState = allocateHistoryState();
}

AnnotationLayer::~AnnotationLayer() = default;

void AnnotationLayer::addItem(std::unique_ptr<AnnotationItem> item)
{
    if (!item) {
        return;
    }
    if (m_eraseTransactionActive) {
        m_deferredItems.push_back(std::move(item));
        return;
    }

    addItemNow(std::move(item));
    emit changed();
}

void AnnotationLayer::addItemNow(std::unique_ptr<AnnotationItem> item)
{
    if (!item) {
        return;
    }

    const AnnotationId id = allocateAnnotationId();
    const size_t index = m_items.size();
    HistoryCommand command;
    command.kind = HistoryCommandKind::Add;
    command.items.push_back({index, nullptr, id});

    m_items.push_back({id, std::move(item)});
    pushAppliedCommand(std::move(command));

    if (dynamic_cast<const PencilStroke*>(m_items.back().get())) {
        // Pencil completion is immutable at commit time, so existing viewport
        // caches can absorb only the new stroke instead of rebuilding the
        // entire retained annotation scene on mouse release.
        appendItemToCaches(*m_items.back());
        ++m_revision;
    } else {
        // Other tools may apply host-specific compensation immediately after
        // addItem(), so keep their conservative full invalidation behavior.
        invalidateCache();
    }
}

void AnnotationLayer::replaceItems(std::vector<std::unique_ptr<AnnotationItem>> items)
{
    m_eraseTransactionActive = false;
    m_deferredItems.clear();
    clearSelection();
    m_items.clear();
    m_undoStack.clear();
    m_redoStack.clear();
    m_historyOwnedBytes = 0;
    m_redoOwnedBytes = 0;
    m_currentHistoryState = allocateHistoryState();

    m_items.reserve(items.size());
    for (auto& item : items) {
        if (item) {
            m_items.push_back({allocateAnnotationId(), std::move(item)});
        }
    }

    const size_t undoableStart = m_items.size() > kMaxHistoryCommands
        ? m_items.size() - kMaxHistoryCommands
        : 0;
    m_undoStack.reserve(m_items.size() - undoableStart);
    for (size_t i = undoableStart; i < m_items.size(); ++i) {
        HistoryCommand command;
        command.kind = HistoryCommandKind::Add;
        command.items.push_back({i, nullptr, m_items[i].id});
        command.beforeState = m_currentHistoryState;
        command.afterState = allocateHistoryState();
        m_currentHistoryState = command.afterState;
        refreshCommandRetainedBytes(command);
        adjustHistoryBytes(0, command.retainedBytes);
        m_undoStack.push_back(std::move(command));
    }

    // Metadata alone can exceed the soft byte limit for unusually large bulk
    // imports. Keep the newest state and commit the oldest prefix.
    trimHistory(m_currentHistoryState);
    invalidateCache();
    emit changed();
}

void AnnotationLayer::undo()
{
    if (m_eraseTransactionActive || m_undoStack.empty()) return;

    const std::size_t beforeBytes = m_undoStack.back().retainedBytes;
    if (!undoCommand(m_undoStack.back())) {
        return;
    }
    refreshCommandRetainedBytes(m_undoStack.back());
    const std::size_t afterBytes = m_undoStack.back().retainedBytes;
    adjustHistoryBytes(beforeBytes, afterBytes);

    m_currentHistoryState = m_undoStack.back().beforeState;
    m_redoStack.push_back(std::move(m_undoStack.back()));
    adjustRetainedByteCounter(m_redoOwnedBytes, 0, afterBytes);
    m_undoStack.pop_back();
    trimHistory(m_redoStack.back().afterState);

    renumberStepBadges();
    invalidateCache();
    clearSelection();
    emit changed();
}

void AnnotationLayer::redo()
{
    if (m_eraseTransactionActive || m_redoStack.empty()) return;

    const std::size_t beforeBytes = m_redoStack.back().retainedBytes;
    if (!redoCommand(m_redoStack.back())) {
        return;
    }
    refreshCommandRetainedBytes(m_redoStack.back());
    const std::size_t afterBytes = m_redoStack.back().retainedBytes;
    adjustHistoryBytes(beforeBytes, afterBytes);

    m_currentHistoryState = m_redoStack.back().afterState;
    m_undoStack.push_back(std::move(m_redoStack.back()));
    m_redoStack.pop_back();
    adjustRetainedByteCounter(m_redoOwnedBytes, beforeBytes, 0);
    trimHistory(m_undoStack.back().afterState);

    renumberStepBadges();
    invalidateCache();
    clearSelection();
    emit changed();
}

void AnnotationLayer::clear()
{
    // A clear is authoritative. Any items temporarily owned by an active
    // eraser stroke must not be restored into the newly cleared layer.
    m_eraseTransactionActive = false;
    m_deferredItems.clear();
    clearSelection();
    m_items.clear();
    m_undoStack.clear();
    m_redoStack.clear();
    m_historyOwnedBytes = 0;
    m_redoOwnedBytes = 0;
    m_currentHistoryState = allocateHistoryState();
    invalidateCache();
    emit changed();
}

void AnnotationLayer::translateAll(const QPointF& delta)
{
    forEachOwnedItem([&delta](AnnotationItem* item) {
        if (item) {
            item->translate(delta);
        }
    });

    invalidateCache();
    emit changed();
}

void AnnotationLayer::forEachItem(const std::function<void(AnnotationItem*)>& visitor,
                                  bool includeRedoStack)
{
    if (!visitor) {
        return;
    }

    for (auto& sceneItem : m_items) {
        visitor(sceneItem.get());
    }
    if (includeRedoStack) {
        const auto visitOwnedCommands = [this, &visitor](auto& commands,
                                                          bool isRedoStack) {
            for (auto& command : commands) {
                const std::size_t beforeBytes = command.retainedBytes;
                visitCommandOwnedItems(command, visitor);
                adjustHistoryBytes(beforeBytes, command.retainedBytes);
                if (isRedoStack) {
                    adjustRetainedByteCounter(
                        m_redoOwnedBytes, beforeBytes, command.retainedBytes);
                }
            }
        };
        visitOwnedCommands(m_undoStack, false);
        visitOwnedCommands(m_redoStack, true);
        trimHistory(m_currentHistoryState);
    }
}

void AnnotationLayer::forEachItem(const std::function<void(const AnnotationItem*)>& visitor,
                                  bool includeRedoStack) const
{
    if (!visitor) {
        return;
    }

    for (const auto& sceneItem : m_items) {
        visitor(sceneItem.get());
    }
    if (includeRedoStack) {
        for (const auto& command : m_undoStack) {
            for (const auto& item : command.items) {
                if (item.item) visitor(item.item.get());
            }
        }
        for (const auto& command : m_redoStack) {
            for (const auto& item : command.items) {
                if (item.item) visitor(item.item.get());
            }
        }
    }
}

void AnnotationLayer::forEachOwnedItem(
    const std::function<void(AnnotationItem*)>& visitor)
{
    forEachItem(visitor, true);
    if (!visitor) {
        return;
    }
    for (auto& item : m_deferredItems) {
        visitor(item.get());
    }
}

void AnnotationLayer::forEachOwnedItem(
    const std::function<void(const AnnotationItem*)>& visitor) const
{
    forEachItem(visitor, true);
    if (!visitor) {
        return;
    }
    for (const auto& item : m_deferredItems) {
        visitor(item.get());
    }
}

AnnotationLayer::AnnotationId AnnotationLayer::allocateAnnotationId()
{
    const AnnotationId id = m_nextAnnotationId++;
    if (m_nextAnnotationId == 0) {
        // Zero is reserved for legacy/unassigned transport records.
        m_nextAnnotationId = 1;
    }
    return id;
}

AnnotationLayer::HistoryStateToken AnnotationLayer::allocateHistoryState()
{
    const HistoryStateToken state = m_nextHistoryState++;
    if (m_nextHistoryState == 0) {
        // History state wraparound is practically unreachable. Preserve the
        // public invariant that zero is never a valid token.
        m_nextHistoryState = 1;
    }
    return state;
}

AnnotationLayer::HistoryStateRelation AnnotationLayer::historyStateRelation(
    HistoryStateToken token) const
{
    if (token == 0) {
        return HistoryStateRelation::Unreachable;
    }
    if (token == m_currentHistoryState) {
        return HistoryStateRelation::Current;
    }

    for (auto it = m_undoStack.rbegin(); it != m_undoStack.rend(); ++it) {
        if (it->beforeState == token) {
            return HistoryStateRelation::UndoReachable;
        }
    }
    for (auto it = m_redoStack.rbegin(); it != m_redoStack.rend(); ++it) {
        if (it->afterState == token) {
            return HistoryStateRelation::RedoReachable;
        }
    }
    return HistoryStateRelation::Unreachable;
}

std::vector<AnnotationLayer::SceneItem>::iterator AnnotationLayer::findSceneItem(
    AnnotationId id)
{
    return std::find_if(m_items.begin(), m_items.end(),
        [id](const SceneItem& sceneItem) { return sceneItem.id == id; });
}

std::vector<AnnotationLayer::SceneItem>::const_iterator AnnotationLayer::findSceneItem(
    AnnotationId id) const
{
    return std::find_if(m_items.cbegin(), m_items.cend(),
        [id](const SceneItem& sceneItem) { return sceneItem.id == id; });
}

std::size_t AnnotationLayer::calculateCommandRetainedBytes(
    const HistoryCommand& command) const
{
    std::size_t bytes = sizeof(HistoryCommand);
    if (command.items.capacity()
        >= kMaxTrackedCommandBytes / sizeof(RemovedItem)) {
        bytes = kMaxTrackedCommandBytes;
    } else {
        addCappedCommandBytes(
            bytes, command.items.capacity() * sizeof(RemovedItem));
    }
    for (const auto& record : command.items) {
        if (record.item) {
            addCappedCommandBytes(bytes, record.item->estimatedRetainedBytes());
        }
    }
    return bytes;
}

void AnnotationLayer::refreshCommandRetainedBytes(HistoryCommand& command)
{
    command.retainedBytes = calculateCommandRetainedBytes(command);
}

void AnnotationLayer::visitCommandOwnedItems(
    HistoryCommand& command,
    const std::function<void(AnnotationItem*)>& visitor)
{
    std::size_t bytes = sizeof(HistoryCommand);
    if (command.items.capacity()
        >= kMaxTrackedCommandBytes / sizeof(RemovedItem)) {
        bytes = kMaxTrackedCommandBytes;
    } else {
        addCappedCommandBytes(
            bytes, command.items.capacity() * sizeof(RemovedItem));
    }

    for (auto& record : command.items) {
        if (!record.item) {
            continue;
        }
        visitor(record.item.get());
        addCappedCommandBytes(bytes, record.item->estimatedRetainedBytes());
    }
    command.retainedBytes = bytes;
}

void AnnotationLayer::adjustHistoryBytes(std::size_t before, std::size_t after)
{
    adjustRetainedByteCounter(m_historyOwnedBytes, before, after);
}

void AnnotationLayer::clearRedoHistory()
{
    adjustHistoryBytes(m_redoOwnedBytes, 0);
    m_redoOwnedBytes = 0;
    m_redoStack.clear();
}

void AnnotationLayer::evictUndoFront()
{
    if (m_undoStack.empty()) return;
    const std::size_t bytes = m_undoStack.front().retainedBytes;
    m_historyOwnedBytes = bytes > m_historyOwnedBytes
        ? 0
        : m_historyOwnedBytes - bytes;
    m_undoStack.erase(m_undoStack.begin());
}

void AnnotationLayer::evictRedoFront()
{
    if (m_redoStack.empty()) return;
    const std::size_t bytes = m_redoStack.front().retainedBytes;
    m_historyOwnedBytes = bytes > m_historyOwnedBytes
        ? 0
        : m_historyOwnedBytes - bytes;
    m_redoOwnedBytes = bytes > m_redoOwnedBytes
        ? 0
        : m_redoOwnedBytes - bytes;
    m_redoStack.erase(m_redoStack.begin());
}

void AnnotationLayer::trimHistory(HistoryStateToken protectedState)
{
    auto exceedsLimits = [this]() {
        return historyCommandCount() > kMaxHistoryCommands
            || m_historyOwnedBytes > kMaxHistoryOwnedBytes;
    };

    while (exceedsLimits() && historyCommandCount() > 1) {
        const bool undoEvictable = !m_undoStack.empty()
            && m_undoStack.front().afterState != protectedState;
        const bool redoEvictable = !m_redoStack.empty()
            && m_redoStack.front().afterState != protectedState;
        const bool undoHasSpare = undoEvictable && m_undoStack.size() > 1;
        const bool redoHasSpare = redoEvictable && m_redoStack.size() > 1;

        // Preserve the nearest command on both sides of the cursor whenever
        // the limits allow it. The front of either vector is the farthest
        // command in that direction, so trimming never punches a hole.
        if (undoHasSpare || redoHasSpare) {
            if (undoHasSpare && redoHasSpare) {
                if (m_redoStack.size() > m_undoStack.size()) {
                    evictRedoFront();
                } else if (m_undoStack.size() > m_redoStack.size()) {
                    evictUndoFront();
                } else if (m_historyOwnedBytes > kMaxHistoryOwnedBytes
                           && m_redoStack.front().retainedBytes
                               > m_undoStack.front().retainedBytes) {
                    evictRedoFront();
                } else {
                    evictUndoFront();
                }
            } else if (undoHasSpare) {
                evictUndoFront();
            } else {
                evictRedoFront();
            }
            continue;
        }

        // If one command on each side remains and their combined payload is
        // still too large, keep the command that triggered this trim. With no
        // explicit protected command, prefer retaining immediate Undo.
        if (undoEvictable && redoEvictable) {
            evictRedoFront();
        } else if (undoEvictable) {
            evictUndoFront();
        } else if (redoEvictable) {
            evictRedoFront();
        } else {
            break;
        }
    }
}

void AnnotationLayer::pushAppliedCommand(HistoryCommand command)
{
    clearRedoHistory();
    command.beforeState = m_currentHistoryState;
    command.afterState = allocateHistoryState();
    m_currentHistoryState = command.afterState;
    refreshCommandRetainedBytes(command);
    adjustHistoryBytes(0, command.retainedBytes);
    m_undoStack.push_back(std::move(command));
    trimHistory(m_currentHistoryState);
}

void AnnotationLayer::restoreCommandItems(HistoryCommand& command)
{
    std::sort(command.items.begin(), command.items.end(),
        [](const RemovedItem& lhs, const RemovedItem& rhs) {
            return lhs.originalIndex < rhs.originalIndex;
        });

    m_items.reserve(m_items.size() + command.items.size());
    for (auto& record : command.items) {
        if (!record.item) continue;
        const size_t insertPos = (std::min)(record.originalIndex, m_items.size());
        m_items.insert(m_items.begin() + static_cast<ptrdiff_t>(insertPos),
            SceneItem{record.id, std::move(record.item)});
    }
}

void AnnotationLayer::removeCommandItems(HistoryCommand& command)
{
    for (auto& record : command.items) {
        if (record.item) continue;
        auto sceneIt = findSceneItem(record.id);
        if (sceneIt == m_items.end()) continue;
        record.item = std::move(sceneIt->item);
        m_items.erase(sceneIt);
    }
}

bool AnnotationLayer::undoCommand(HistoryCommand& command)
{
    if (command.kind == HistoryCommandKind::Add) {
        if (command.items.size() != 1 || command.items.front().item) {
            return false;
        }
        RemovedItem& record = command.items.front();
        auto sceneIt = m_items.end();
        if (record.originalIndex < m_items.size() &&
            m_items[record.originalIndex].id == record.id) {
            sceneIt = m_items.begin() + static_cast<ptrdiff_t>(record.originalIndex);
        } else {
            sceneIt = findSceneItem(record.id);
        }
        if (sceneIt == m_items.end()) {
            return false;
        }
        record.originalIndex =
            static_cast<size_t>(std::distance(m_items.begin(), sceneIt));
        record.item = std::move(sceneIt->item);
        m_items.erase(sceneIt);
        return true;
    }

    const bool allOwned = std::all_of(command.items.cbegin(), command.items.cend(),
        [](const RemovedItem& record) { return static_cast<bool>(record.item); });
    if (!allOwned) {
        return false;
    }
    restoreCommandItems(command);
    return true;
}

bool AnnotationLayer::redoCommand(HistoryCommand& command)
{
    if (command.kind == HistoryCommandKind::Add) {
        if (command.items.size() != 1 || !command.items.front().item) {
            return false;
        }
        restoreCommandItems(command);
        return true;
    }

    bool indicesMatch = true;
    for (const RemovedItem& record : command.items) {
        if (record.item || record.originalIndex >= m_items.size() ||
            m_items[record.originalIndex].id != record.id) {
            indicesMatch = false;
            break;
        }
    }
    if (indicesMatch) {
        for (auto it = command.items.rbegin(); it != command.items.rend(); ++it) {
            const auto sceneIt = m_items.begin() +
                static_cast<ptrdiff_t>(it->originalIndex);
            it->item = std::move(sceneIt->item);
            m_items.erase(sceneIt);
        }
        return true;
    }

    const bool allPresent = std::all_of(command.items.cbegin(), command.items.cend(),
        [this](const RemovedItem& record) {
            return !record.item && findSceneItem(record.id) != m_items.cend();
        });
    if (!allPresent) return false;
    removeCommandItems(command);
    return true;
}

void AnnotationLayer::draw(QPainter &painter) const
{
    if (m_items.empty()) return;

    const QRectF clipBounds = painter.hasClipping()
        ? painter.clipBoundingRect()
        : QRectF();
    for (const auto &item : m_items) {
        if (!item->isVisible()) {
            continue;
        }
        if (clipBounds.isValid() && !clipBounds.isEmpty()) {
            // PencilStroke owns an exact smooth-path bound. Other annotation
            // types still have legacy conservative bounds that may not cover
            // wide arrowheads, so do not use them for destructive clip culling.
            if (const auto* pencil = dynamic_cast<const PencilStroke*>(item.get());
                pencil && !QRectF(pencil->boundingRect()).intersects(clipBounds)) {
                continue;
            }
        }
        item->draw(painter);
    }
}

bool AnnotationLayer::canUndo() const
{
    return !m_eraseTransactionActive && !m_undoStack.empty();
}

bool AnnotationLayer::canRedo() const
{
    return !m_eraseTransactionActive && !m_redoStack.empty();
}

bool AnnotationLayer::isEmpty() const
{
    return m_items.empty();
}

QRect AnnotationLayer::contentBoundingRect() const
{
    QRect bounds;
    for (const auto& item : m_items) {
        if (!item || !item->isVisible()) {
            continue;
        }
        const QRect itemBounds = item->boundingRect();
        bounds = bounds.isValid() ? bounds.united(itemBounds) : itemBounds;
    }

    return bounds;
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

std::vector<AnnotationLayer::RemovedItem> AnnotationLayer::removeItemsIntersecting(
    const QPoint &point, int strokeWidth)
{
    std::vector<RemovedItem> removedItems;
    if (!m_eraseTransactionActive) {
        return removedItems;
    }
    int radius = strokeWidth / 2;
    size_t currentIndex = 0;

    for (auto it = m_items.begin(); it != m_items.end(); ) {
        bool shouldRemove = false;

        // Use path-based intersection for strokes (more accurate)
        if (auto* pencil = dynamic_cast<PencilStroke*>(it->get())) {
            shouldRemove = pencil->intersectsCircle(point, radius);
        } else if (auto* marker = dynamic_cast<MarkerStroke*>(it->get())) {
            shouldRemove = marker->intersectsCircle(point, radius);
        } else if (auto* mosaic = dynamic_cast<MosaicStroke*>(it->get())) {
            shouldRemove = mosaic->intersectsCircle(point, radius);
        } else {
            // Fallback: expanded bounding rect for shapes/text/badges/etc.
            QRect itemRect = (*it)->boundingRect();
            QRect expandedRect = itemRect.adjusted(-radius, -radius, radius, radius);
            shouldRemove = expandedRect.contains(point);
        }

        if (shouldRemove) {
            // Item intersects with eraser - remove it and record original index
            if (removedItems.size() == removedItems.capacity()) {
                const size_t currentSize = removedItems.size();
                if (currentSize > removedItems.max_size() / 2) {
                    restoreRemovedItems(std::move(removedItems));
                    return {};
                }
                const size_t nextCapacity = currentSize == 0 ? 1 : currentSize * 2;
                try {
                    // Allocate before moving the next scene-owned object. If
                    // allocation fails, roll back prior removals in-place.
                    removedItems.reserve(nextCapacity);
                } catch (...) {
                    restoreRemovedItems(std::move(removedItems));
                    return {};
                }
            }
            removedItems.push_back({currentIndex, nullptr, it->id});
            removedItems.back().item = std::move(it->item);
            it = m_items.erase(it);
            // currentIndex tracks the original scan position, not post-erase offsets.
            ++currentIndex;
        } else {
            ++it;
            ++currentIndex;
        }
    }

    if (!removedItems.empty()) {
        renumberStepBadges();
        invalidateCache();
        clearSelection();
        emit changed();
    }

    return removedItems;
}

void AnnotationLayer::beginEraseTransaction()
{
    m_eraseTransactionActive = true;
}

bool AnnotationLayer::endEraseTransaction()
{
    if (!m_eraseTransactionActive) {
        return false;
    }

    m_eraseTransactionActive = false;
    if (flushDeferredItems()) {
        emit changed();
    }
    return true;
}

bool AnnotationLayer::commitEraseTransaction(std::vector<RemovedItem> items)
{
    if (!m_eraseTransactionActive) {
        return false;
    }

    if (items.empty()) {
        m_eraseTransactionActive = false;
        if (flushDeferredItems()) {
            emit changed();
        }
        return true;
    }

    HistoryCommand command;
    command.kind = HistoryCommandKind::Remove;
    command.items = std::move(items);
    m_eraseTransactionActive = false;
    pushAppliedCommand(std::move(command));
    flushDeferredItems();
    emit changed();
    return true;
}

bool AnnotationLayer::cancelEraseTransaction(std::vector<RemovedItem> items)
{
    if (!m_eraseTransactionActive) {
        return false;
    }

    m_eraseTransactionActive = false;
    const bool restored = restoreRemovedItemsNow(std::move(items));
    const bool installedDeferredItems = flushDeferredItems();
    if (restored || installedDeferredItems) {
        emit changed();
    }
    return true;
}

bool AnnotationLayer::flushDeferredItems()
{
    if (m_eraseTransactionActive || m_deferredItems.empty()) {
        return false;
    }

    auto deferredItems = std::move(m_deferredItems);
    m_deferredItems.clear();
    for (auto& item : deferredItems) {
        addItemNow(std::move(item));
    }
    return true;
}

void AnnotationLayer::restoreRemovedItems(std::vector<RemovedItem> items)
{
    if (restoreRemovedItemsNow(std::move(items))) {
        emit changed();
    }
}

bool AnnotationLayer::restoreRemovedItemsNow(std::vector<RemovedItem> items)
{
    if (items.empty()) {
        return false;
    }

    std::sort(items.begin(), items.end(),
        [](const auto& a, const auto& b) { return a.originalIndex < b.originalIndex; });

    m_items.reserve(m_items.size() + items.size());
    for (auto& indexed : items) {
        const size_t insertPos = (std::min)(indexed.originalIndex, m_items.size());
        AnnotationId id = indexed.id;
        if (id == 0) {
            id = allocateAnnotationId();
        }
        m_items.insert(m_items.begin() + static_cast<ptrdiff_t>(insertPos),
            SceneItem{id, std::move(indexed.item)});
    }

    renumberStepBadges();
    invalidateCache();
    clearSelection();
    return true;
}

int AnnotationLayer::hitTestText(const QPoint &pos) const
{
    // Iterate in reverse order (top-most items first)
    for (int i = static_cast<int>(m_items.size()) - 1; i >= 0; --i) {
        // Hit-test TextBoxAnnotation items (new resizable text boxes)
        if (auto* textItem = dynamic_cast<TextBoxAnnotation*>(m_items[i].get())) {
            if (textItem->isVisible() && textItem->containsPoint(pos)) {
                return i;
            }
        }
    }
    return -1;
}

int AnnotationLayer::hitTestEmojiSticker(const QPoint &pos) const
{
    // Iterate in reverse order (top-most items first)
    for (int i = static_cast<int>(m_items.size()) - 1; i >= 0; --i) {
        if (auto* emojiItem = dynamic_cast<EmojiStickerAnnotation*>(m_items[i].get())) {
            if (emojiItem->isVisible() && emojiItem->containsPoint(pos)) {
                return i;
            }
        }
    }
    return -1;
}

int AnnotationLayer::hitTestShape(const QPoint &pos) const
{
    // Iterate in reverse order (top-most items first)
    for (int i = static_cast<int>(m_items.size()) - 1; i >= 0; --i) {
        if (auto* shapeItem = dynamic_cast<ShapeAnnotation*>(m_items[i].get())) {
            if (shapeItem->isVisible() && shapeItem->containsPoint(pos)) {
                return i;
            }
        }
    }
    return -1;
}

int AnnotationLayer::hitTestArrow(const QPoint &pos) const
{
    // Iterate in reverse order (top-most items first)
    for (int i = static_cast<int>(m_items.size()) - 1; i >= 0; --i) {
        if (auto* arrowItem = dynamic_cast<ArrowAnnotation*>(m_items[i].get())) {
            if (arrowItem->isVisible() && arrowItem->containsPoint(pos)) {
                return i;
            }
        }
    }
    return -1;
}

int AnnotationLayer::hitTestPolyline(const QPoint &pos) const
{
    // Iterate in reverse order (top-most items first)
    for (int i = static_cast<int>(m_items.size()) - 1; i >= 0; --i) {
        if (auto* polylineItem = dynamic_cast<PolylineAnnotation*>(m_items[i].get())) {
            if (polylineItem->isVisible() && polylineItem->containsPoint(pos)) {
                return i;
            }
        }
    }
    return -1;
}

void AnnotationLayer::setSelectedIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(m_items.size())) {
        m_selectedIndex = -1;
        return;
    }

    AnnotationItem* candidate = m_items[index].get();
    m_selectedIndex = (candidate && candidate->isVisible()) ? index : -1;
}

AnnotationItem* AnnotationLayer::selectedItem()
{
    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_items.size())) {
        AnnotationItem* item = m_items[m_selectedIndex].get();
        if (item && item->isVisible()) {
            return item;
        }
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
    if (m_eraseTransactionActive ||
        m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_items.size())) {
        return false;
    }

    HistoryCommand command;
    command.kind = HistoryCommandKind::Remove;
    command.items.push_back({static_cast<size_t>(m_selectedIndex),
                             nullptr,
                             m_items[m_selectedIndex].id});
    command.items.front().item = std::move(m_items[m_selectedIndex].item);
    m_items.erase(m_items.begin() + m_selectedIndex);

    pushAppliedCommand(std::move(command));
    m_selectedIndex = -1;
    renumberStepBadges();
    invalidateCache();
    emit changed();
    return true;
}

void AnnotationLayer::invalidateCache()
{
    m_annotationCaches.clear();
    ++m_revision;
}

void AnnotationLayer::appendItemToCaches(const AnnotationItem& item)
{
    for (auto it = m_annotationCaches.begin(); it != m_annotationCaches.end();) {
        const CacheKey& key = it->first;
        if (key.excludeIndex >= 0) {
            it = m_annotationCaches.erase(it);
            continue;
        }

        if (item.isVisible()) {
            QPainter cachePainter(&it->second);
            cachePainter.setRenderHint(QPainter::Antialiasing);
            cachePainter.setRenderHint(QPainter::SmoothPixmapTransform);
            cachePainter.translate(-QPointF(key.originX, key.originY));
            item.draw(cachePainter);
        }
        ++it;
    }
}

void AnnotationLayer::drawCached(QPainter &painter,
                                 const QSize &canvasSize,
                                 qreal devicePixelRatio,
                                 const QPoint& origin) const
{
    if (m_items.empty()) return;

    const QSize physicalSize = CoordinateHelper::toPhysical(canvasSize, devicePixelRatio);
    const CacheKey cacheKey{
        physicalSize.width(),
        physicalSize.height(),
        origin.x(),
        origin.y(),
        -1,
        qRound(devicePixelRatio * 1000.0)
    };

    auto cacheIt = m_annotationCaches.find(cacheKey);
    if (cacheIt == m_annotationCaches.end()) {
        QPixmap cache(physicalSize);
        cache.setDevicePixelRatio(devicePixelRatio);
        cache.fill(Qt::transparent);

        QPainter cachePainter(&cache);
        cachePainter.setRenderHint(QPainter::Antialiasing);
        cachePainter.setRenderHint(QPainter::SmoothPixmapTransform);
        cachePainter.translate(-QPointF(origin));

        for (const auto &item : m_items) {
            if (item->isVisible()) {
                item->draw(cachePainter);
            }
        }

        cacheIt = m_annotationCaches.emplace(cacheKey, std::move(cache)).first;
    }

    painter.drawPixmap(0, 0, cacheIt->second);
}

void AnnotationLayer::markDirtyRect(const QRect& rect)
{
    if (m_hasDirtyRect) {
        m_dirtyRect = m_dirtyRect.united(rect);
    } else {
        m_dirtyRect = rect;
        m_hasDirtyRect = true;
    }
}

void AnnotationLayer::clearDirtyRect()
{
    m_dirtyRect = QRect();
    m_hasDirtyRect = false;
}

void AnnotationLayer::drawWithDirtyRegion(QPainter &painter, const QSize &canvasSize,
                                          qreal devicePixelRatio, int excludeIndex,
                                          const QPoint& origin) const
{
    if (m_items.empty()) return;

    const QSize physicalSize = CoordinateHelper::toPhysical(canvasSize, devicePixelRatio);
    const int normalizedExcludeIndex =
        (excludeIndex >= 0 && excludeIndex < static_cast<int>(m_items.size())) ? excludeIndex : -1;

    const CacheKey cacheKey{
        physicalSize.width(),
        physicalSize.height(),
        origin.x(),
        origin.y(),
        normalizedExcludeIndex,
        qRound(devicePixelRatio * 1000.0)
    };

    if (normalizedExcludeIndex >= 0) {
        for (auto it = m_annotationCaches.begin(); it != m_annotationCaches.end();) {
            const CacheKey& existingKey = it->first;
            const bool sameViewport =
                existingKey.physicalWidth == cacheKey.physicalWidth &&
                existingKey.physicalHeight == cacheKey.physicalHeight &&
                existingKey.originX == cacheKey.originX &&
                existingKey.originY == cacheKey.originY &&
                existingKey.devicePixelRatioMilli == cacheKey.devicePixelRatioMilli;

            if (sameViewport && existingKey.excludeIndex != normalizedExcludeIndex) {
                it = m_annotationCaches.erase(it);
            } else {
                ++it;
            }
        }
    }

    auto cacheIt = m_annotationCaches.find(cacheKey);
    if (cacheIt == m_annotationCaches.end()) {
        QPixmap cache(physicalSize);
        cache.setDevicePixelRatio(devicePixelRatio);
        cache.fill(Qt::transparent);

        QPainter cachePainter(&cache);
        cachePainter.setRenderHint(QPainter::Antialiasing);
        cachePainter.setRenderHint(QPainter::SmoothPixmapTransform);
        cachePainter.translate(-QPointF(origin));

        for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
            if (i != normalizedExcludeIndex && m_items[i]->isVisible()) {
                m_items[i]->draw(cachePainter);
            }
        }

        cacheIt = m_annotationCaches.emplace(cacheKey, std::move(cache)).first;
    }

    // Draw the cached background (all items except the one being dragged)
    painter.drawPixmap(0, 0, cacheIt->second);

    // Draw the excluded item (being dragged) on top, directly to painter
    if (normalizedExcludeIndex >= 0) {
        if (m_items[normalizedExcludeIndex]->isVisible()) {
            painter.save();
            painter.translate(-QPointF(origin));
            m_items[normalizedExcludeIndex]->draw(painter);
            painter.restore();
        }
    }
}

void AnnotationLayer::commitDirtyRegion(const QSize &canvasSize, qreal devicePixelRatio)
{
    Q_UNUSED(canvasSize);
    Q_UNUSED(devicePixelRatio);

    if (!m_hasDirtyRect) return;
    invalidateCache();
    clearDirtyRect();
}
