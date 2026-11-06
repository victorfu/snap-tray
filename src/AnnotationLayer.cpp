#include "annotations/AnnotationLayer.h"
#include "annotations/AnnotationGeometry.h"
#include "annotations/AnnotationStyle.h"
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
#include <QPainterPathStroker>
#include <QDebug>
#include <QSize>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

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
    // Drawing something new ends a gesture whose release never came (#237).
    if (isGestureActive() && !gestureIsReEdit()) commitGesture(ModifyStyle | ModifyGeometry);
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
    endModifyCoalescing();
    dropGesture();
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
            m_items.push_back({0, std::move(item)});
        }
    }
    // Step badges are numbered by id (#268), and the items arrive in drawing
    // order. Give badges their ids last, in the order of their stored
    // numbers, so a reordered scene keeps its numbering. Older data stored
    // numbers in drawing order, so it comes back the same.
    std::vector<std::size_t> badgeIndices;
    for (std::size_t i = 0; i < m_items.size(); ++i) {
        if (dynamic_cast<const StepBadgeAnnotation*>(m_items[i].get())) {
            badgeIndices.push_back(i);
        } else {
            m_items[i].id = allocateAnnotationId();
        }
    }
    std::stable_sort(badgeIndices.begin(), badgeIndices.end(),
        [this](std::size_t lhs, std::size_t rhs) {
            return static_cast<const StepBadgeAnnotation*>(m_items[lhs].get())->number()
                < static_cast<const StepBadgeAnnotation*>(m_items[rhs].get())->number();
        });
    for (std::size_t index : badgeIndices) {
        m_items[index].id = allocateAnnotationId();
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
    endModifyCoalescing();
    if (m_eraseTransactionActive || isGestureActive() || m_undoStack.empty()) return;
    const bool keepSelection = (m_undoStack.back().kind == HistoryCommandKind::Modify
                                || m_undoStack.back().kind == HistoryCommandKind::Reorder)
        && !m_undoStack.back().items.empty()
        && selectedId() == m_undoStack.back().items.front().id;

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
    // A Modify or Reorder keeps the selected annotation; keep it selected.
    if (!keepSelection) clearSelection();
    emit changed();
}

void AnnotationLayer::redo()
{
    endModifyCoalescing();
    if (m_eraseTransactionActive || isGestureActive() || m_redoStack.empty()) return;
    const bool keepSelection = (m_redoStack.back().kind == HistoryCommandKind::Modify
                                || m_redoStack.back().kind == HistoryCommandKind::Reorder)
        && !m_redoStack.back().items.empty()
        && selectedId() == m_redoStack.back().items.front().id;

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
    // A Modify or Reorder keeps the selected annotation; keep it selected.
    if (!keepSelection) clearSelection();
    emit changed();
}

void AnnotationLayer::clear()
{
    endModifyCoalescing();
    dropGesture();
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

void AnnotationLayer::translateOwnedItems(
    const std::function<QPointF(const AnnotationItem&)>& deltaForItem)
{
    if (!deltaForItem) {
        return;
    }

    forEachOwnedItem([&deltaForItem](AnnotationItem* item) {
        if (!item) {
            return;
        }

        const QPointF delta = deltaForItem(*item);
        if (!delta.isNull()) {
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
    // An active gesture's snapshot moves with canvas-wide translations too.
    if (m_gestureSnapshot) visitor(m_gestureSnapshot.get());
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
    if (m_gestureSnapshot) visitor(m_gestureSnapshot.get());
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
    endModifyCoalescing();
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

bool AnnotationLayer::swapModifiedParts(HistoryCommand& command)
{
    // Exchanges only the recorded parts between the scene item and the stored
    // state. The scene object, its id and its visibility stay: changes outside
    // these parts must survive (#224 review).
    if (command.items.size() != 1 || !command.items.front().item) return false;
    auto sceneIt = findSceneItem(command.items.front().id);
    if (sceneIt == m_items.end() || !sceneIt->item) return false;
    std::unique_ptr<AnnotationItem> current = sceneIt->item->clone();
    const AnnotationItem& stored = *command.items.front().item;
    // Style first: a text's font sets its box, which geometry then restores.
    if ((command.parts & ModifyStyle) && !AnnotationStyle::copyStyle(stored, *sceneIt->item)) return false;
    if ((command.parts & ModifyGeometry) && !AnnotationGeometry::copy(stored, *sceneIt->item)) return false;
    command.items.front().item = std::move(current);
    return true;
}

bool AnnotationLayer::moveSceneItem(AnnotationId id, std::size_t toIndex)
{
    auto sceneIt = findSceneItem(id);
    if (sceneIt == m_items.end() || toIndex >= m_items.size()) return false;
    const auto from = std::distance(m_items.begin(), sceneIt);
    const auto to = static_cast<std::ptrdiff_t>(toIndex);
    // Rotating keeps every other annotation in its relative order.
    if (from < to) {
        std::rotate(sceneIt, sceneIt + 1, m_items.begin() + to + 1);
    } else if (to < from) {
        std::rotate(m_items.begin() + to, sceneIt, sceneIt + 1);
    }
    return true;
}

bool AnnotationLayer::undoCommand(HistoryCommand& command)
{
    if (command.kind == HistoryCommandKind::Modify) {
        return swapModifiedParts(command);
    }
    if (command.kind == HistoryCommandKind::Reorder) {
        return command.items.size() == 1
            && moveSceneItem(command.items.front().id, command.items.front().originalIndex);
    }
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
    if (command.kind == HistoryCommandKind::Modify) {
        return swapModifiedParts(command);
    }
    if (command.kind == HistoryCommandKind::Reorder) {
        return command.items.size() == 1
            && moveSceneItem(command.items.front().id, command.toIndex);
    }
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
    return !m_eraseTransactionActive && !isGestureActive() && !m_undoStack.empty();
}

bool AnnotationLayer::canRedo() const
{
    return !m_eraseTransactionActive && !isGestureActive() && !m_redoStack.empty();
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
    // Numbers follow creation order (ids are allocated in it), never drawing
    // order: moving a badge up or down must not renumber it (#268). Removed
    // badges keep their ids, so undoing a removal restores their numbers.
    std::vector<std::pair<AnnotationId, StepBadgeAnnotation*>> badges;
    for (auto &item : m_items) {
        if (auto* badge = dynamic_cast<StepBadgeAnnotation*>(item.get())) {
            badges.emplace_back(item.id, badge);
        }
    }
    std::sort(badges.begin(), badges.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
    int badgeNumber = 1;
    for (auto& entry : badges) {
        entry.second->setNumber(badgeNumber++);
    }
}

int AnnotationLayer::zOrderTargetIndex(ZOrderMove move) const
{
    if (m_eraseTransactionActive || isGestureActive()) return -1;
    const int from = selectedIndex();
    if (from < 0) return -1;
    const int last = static_cast<int>(m_items.size()) - 1;
    const bool up = move == ZOrderMove::Forward || move == ZOrderMove::ToFront;
    if ((up && from == last) || (!up && from == 0)) return -1;
    if (move == ZOrderMove::ToFront) return last;
    if (move == ZOrderMove::ToBack) return 0;

    // Step past the nearest annotation that overlaps the selection, as
    // passing the ones that do not would change nothing on screen.
    const QRect bounds = m_items[from]->boundingRect();
    const int step = up ? 1 : -1;
    for (int i = from + step; i >= 0 && i <= last; i += step) {
        const AnnotationItem* other = m_items[i].get();
        if (other && other->isVisible() && other->boundingRect().intersects(bounds)) {
            return i;
        }
    }
    return up ? last : 0;
}

bool AnnotationLayer::moveSelectedItem(ZOrderMove move)
{
    const int target = zOrderTargetIndex(move);
    if (target < 0) return false;
    const int from = selectedIndex();
    const AnnotationId id = m_items[from].id;
    const QString coalesceKey =
        QStringLiteral("zorder:%1").arg(static_cast<int>(move));

    const bool extend = m_openModifyId == id && m_openModifyKey == coalesceKey
        && !m_undoStack.empty()
        && m_undoStack.back().kind == HistoryCommandKind::Reorder
        && m_undoStack.back().items.size() == 1
        && m_undoStack.back().items.front().id == id;

    if (!moveSceneItem(id, static_cast<std::size_t>(target))) return false;
    if (extend) {
        m_undoStack.back().toIndex = static_cast<std::size_t>(target);
    } else {
        HistoryCommand command;
        command.kind = HistoryCommandKind::Reorder;
        command.items.push_back({static_cast<std::size_t>(from), nullptr, id});
        command.toIndex = static_cast<std::size_t>(target);
        pushAppliedCommand(std::move(command));   // ends any open coalescing
        m_openModifyId = id;
        m_openModifyKey = coalesceKey;
    }
    invalidateCache();
    emit changed();
    return true;
}

std::vector<AnnotationLayer::RemovedItem> AnnotationLayer::removeItemsIntersecting(
    const QPoint &point, int strokeWidth)
{
    return removeItemsIntersecting(point, point, strokeWidth);
}

std::vector<AnnotationLayer::RemovedItem> AnnotationLayer::removeItemsIntersecting(
    const QPoint &from, const QPoint &to, int strokeWidth)
{
    std::vector<RemovedItem> removedItems;
    if (!m_eraseTransactionActive) {
        return removedItems;
    }
    int radius = strokeWidth / 2;
    QPainterPath segment;
    segment.moveTo(from);
    segment.lineTo(to);
    QPainterPath eraserPath;
    if (from == to) {
        eraserPath.addEllipse(from, radius, radius);
    } else {
        QPainterPathStroker stroker;
        stroker.setWidth(radius * 2);
        stroker.setCapStyle(Qt::RoundCap);
        eraserPath = stroker.createStroke(segment);
    }
    const QRectF eraserBounds = eraserPath.boundingRect();
    size_t currentIndex = 0;

    for (auto it = m_items.begin(); it != m_items.end(); ) {
        bool shouldRemove = false;

        // Use path-based intersection for strokes (more accurate)
        if (auto* pencil = dynamic_cast<PencilStroke*>(it->get())) {
            shouldRemove = eraserBounds.intersects(pencil->boundingRect())
                && pencil->strokePath().intersects(eraserPath);
        } else if (auto* marker = dynamic_cast<MarkerStroke*>(it->get())) {
            shouldRemove = eraserBounds.intersects(marker->boundingRect())
                && marker->strokePath().intersects(eraserPath);
        } else if (auto* mosaic = dynamic_cast<MosaicStroke*>(it->get())) {
            shouldRemove = eraserBounds.intersects(mosaic->boundingRect())
                && mosaic->strokePath().intersects(eraserPath);
        } else {
            // Fallback: expanded bounding rect for shapes/text/badges/etc.
            QRect itemRect = (*it)->boundingRect();
            QRect expandedRect = itemRect.adjusted(-radius, -radius, radius, radius);
            shouldRemove = expandedRect.contains(from) || expandedRect.contains(to)
                || segment.intersects(QRectF(expandedRect));
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
        // Selection follows the annotation id, so erasing other items keeps it.
        if (m_selectedId != 0 && indexOfId(m_selectedId) < 0) clearSelection();
        emit changed();
    }

    return removedItems;
}

void AnnotationLayer::beginEraseTransaction()
{
    endModifyCoalescing();
    dropGesture();
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
    AnnotationId next = 0;
    if (index >= 0 && index < static_cast<int>(m_items.size())) {
        AnnotationItem* candidate = m_items[index].get();
        next = (candidate && candidate->isVisible()) ? m_items[index].id : 0;
    }
    if (next == m_selectedId) return;
    m_selectedId = next;
    emit selectionChanged();
}

void AnnotationLayer::clearSelection()
{
    if (m_selectedId == 0) return;
    m_selectedId = 0;
    emit selectionChanged();
}

AnnotationLayer::AnnotationId AnnotationLayer::idAt(int index) const
{
    return (index >= 0 && index < static_cast<int>(m_items.size())) ? m_items[index].id : 0;
}

int AnnotationLayer::indexOfId(AnnotationId id) const
{
    const auto it = findSceneItem(id);
    return it == m_items.cend() ? -1 : static_cast<int>(std::distance(m_items.cbegin(), it));
}

AnnotationLayer::AnnotationId AnnotationLayer::selectedId() const
{
    return selectedIndex() >= 0 ? m_selectedId : 0;
}

int AnnotationLayer::selectedIndex() const
{
    return m_selectedId == 0 ? -1 : indexOfId(m_selectedId);
}

void AnnotationLayer::endModifyCoalescing()
{
    m_openModifyId = 0;
    m_openModifyKey.clear();
}

bool AnnotationLayer::modifyItem(AnnotationId id, const QString& coalesceKey,
                                 const std::function<bool(AnnotationItem&)>& mutate)
{
    if (m_eraseTransactionActive || !mutate || id == 0) return false;
    auto sceneIt = findSceneItem(id);
    if (sceneIt == m_items.end() || !sceneIt->item) return false;

    const bool extend = !coalesceKey.isEmpty() && m_openModifyId == id
        && m_openModifyKey == coalesceKey && !m_undoStack.empty()
        && m_undoStack.back().kind == HistoryCommandKind::Modify
        && m_undoStack.back().items.size() == 1
        && m_undoStack.back().items.front().id == id;

    std::unique_ptr<AnnotationItem> before = extend ? nullptr : sceneIt->item->clone();
    if (!mutate(*sceneIt->item)) return false;

    // A style edit during a drag is its own step: keep it out of the
    // gesture, so cancelling the drag does not revert it.
    if (m_gestureId == id && m_gestureSnapshot && !gestureIsReEdit()
        && AnnotationStyle::kindOf(*sceneIt->item) != StyleTargetKind::None)
        AnnotationStyle::copyStyle(*sceneIt->item, *m_gestureSnapshot);

    if (extend) {
        HistoryCommand& open = m_undoStack.back();
        const std::size_t beforeBytes = open.retainedBytes;
        refreshCommandRetainedBytes(open);
        adjustHistoryBytes(beforeBytes, open.retainedBytes);
    } else {
        HistoryCommand command;
        command.kind = HistoryCommandKind::Modify;
        command.parts = ModifyStyle;
        command.items.push_back({static_cast<size_t>(std::distance(m_items.begin(), sceneIt)),
                                 std::move(before), id});
        pushAppliedCommand(std::move(command));   // ends any open coalescing
        if (!coalesceKey.isEmpty()) {
            m_openModifyId = id;
            m_openModifyKey = coalesceKey;
        }
    }
    invalidateCache();
    emit changed();
    return true;
}

bool AnnotationLayer::beginGesture(AnnotationId id)
{
    if (m_eraseTransactionActive || id == 0) return false;
    if (isGestureActive()) {
        if (gestureIsReEdit()) return false;   // the inline editor owns it
        commitGesture(ModifyStyle | ModifyGeometry);   // a release that never came
    }
    auto sceneIt = findSceneItem(id);
    if (sceneIt == m_items.end() || !sceneIt->item) return false;
    endModifyCoalescing();
    m_gestureId = id;
    m_gestureSnapshot = sceneIt->item->clone();
    emit changed();   // Undo/Redo availability
    return true;
}

bool AnnotationLayer::ensureGesture()
{
    // A gesture on another item is stale: beginGesture records it first.
    if (isGestureActive() && m_gestureId == selectedId()) return true;
    return beginGesture(selectedId());
}

bool AnnotationLayer::gestureIsReEdit() const
{
    if (!isGestureActive()) return false;
    auto sceneIt = findSceneItem(m_gestureId);
    return sceneIt != m_items.end() && sceneIt->item && !sceneIt->item->isVisible();
}

void AnnotationLayer::dropGesture()
{
    m_gestureId = 0;
    m_gestureSnapshot.reset();
}

bool AnnotationLayer::commitGesture(unsigned parts)
{
    if (!isGestureActive()) return false;
    const AnnotationId id = m_gestureId;
    std::unique_ptr<AnnotationItem> before = std::move(m_gestureSnapshot);
    dropGesture();
    bool recorded = false;
    auto sceneIt = findSceneItem(id);
    if (sceneIt != m_items.end() && sceneIt->item && before) {
        const AnnotationItem& now = *sceneIt->item;
        unsigned changedParts = 0;
        if ((parts & ModifyStyle) && AnnotationStyle::kindOf(now) != StyleTargetKind::None
            && !AnnotationStyle::equalStyle(*before, now))
            changedParts |= ModifyStyle;
        if ((parts & ModifyGeometry) && AnnotationGeometry::supports(now)
            && !AnnotationGeometry::equal(*before, now))
            changedParts |= ModifyGeometry;
        if (changedParts != 0) {
            HistoryCommand command;
            command.kind = HistoryCommandKind::Modify;
            command.parts = changedParts;
            command.items.push_back({static_cast<size_t>(std::distance(m_items.begin(), sceneIt)),
                                     std::move(before), id});
            pushAppliedCommand(std::move(command));
            recorded = true;
        }
    }
    invalidateCache();
    emit changed();
    return recorded;
}

void AnnotationLayer::cancelGesture()
{
    if (!isGestureActive()) return;
    auto sceneIt = findSceneItem(m_gestureId);
    if (sceneIt != m_items.end() && sceneIt->item && m_gestureSnapshot) {
        if (AnnotationStyle::kindOf(*sceneIt->item) != StyleTargetKind::None)
            AnnotationStyle::copyStyle(*m_gestureSnapshot, *sceneIt->item);
        AnnotationGeometry::copy(*m_gestureSnapshot, *sceneIt->item);
    }
    dropGesture();
    invalidateCache();
    emit changed();
}

AnnotationItem* AnnotationLayer::selectedItem()
{
    const int selected = selectedIndex();
    if (selected >= 0) {
        AnnotationItem* item = m_items[selected].get();
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
    const int selected = selectedIndex();
    if (m_eraseTransactionActive ||
        selected < 0) {
        return false;
    }

    if (m_items[selected].id == m_gestureId) dropGesture();
    HistoryCommand command;
    command.kind = HistoryCommandKind::Remove;
    command.items.push_back({static_cast<size_t>(selected),
                             nullptr,
                             m_items[selected].id});
    command.items.front().item = std::move(m_items[selected].item);
    m_items.erase(m_items.begin() + selected);

    pushAppliedCommand(std::move(command));
    clearSelection();
    renumberStepBadges();
    invalidateCache();
    emit changed();
    return true;
}

void AnnotationLayer::invalidateCache()
{
    m_annotationCaches.clear();
    m_annotationCacheBytes = 0;
    m_annotationCacheClock = 0;
    ++m_revision;
}

AnnotationLayer::CacheMap::iterator AnnotationLayer::eraseAnnotationCache(CacheMap::iterator it) const
{
    m_annotationCacheBytes -= it->second.bytes;
    return m_annotationCaches.erase(it);
}

void AnnotationLayer::makeAnnotationCacheRoom(std::uint64_t incomingBytes) const
{
    while (!m_annotationCaches.empty()
           && (m_annotationCaches.size() >= kMaxAnnotationCacheEntries
               || incomingBytes > kMaxAnnotationCacheBytes
               || m_annotationCacheBytes > kMaxAnnotationCacheBytes - incomingBytes)) {
        auto victim = m_annotationCaches.begin();
        for (auto it = std::next(victim); it != m_annotationCaches.end(); ++it) {
            const bool transient = it->first.excludeIndex >= 0;
            const bool victimTransient = victim->first.excludeIndex >= 0;
            if ((transient && !victimTransient)
                || (transient == victimTransient && it->second.lastUsed < victim->second.lastUsed))
                victim = it;
        }
        eraseAnnotationCache(victim);
    }
}

AnnotationLayer::CacheKey AnnotationLayer::cacheKeyForPainter(
    const QPainter& painter, const QSize& canvasSize, qreal devicePixelRatio,
    const QPoint& origin, int excludeIndex)
{
    const QTransform transform = painter.deviceTransform();
    const auto pixelPhase = [](qreal position) {
        const qreal phase = position - std::floor(position);
        return qFuzzyIsNull(phase) || qFuzzyCompare(phase, qreal(1.0)) ? 0.0 : phase;
    };
    // Only a native-scale, axis-aligned blit can preserve the pixel grid this
    // way. Keep the existing sampling for zoomed/rotated annotation views.
    const bool nativeScale = qFuzzyCompare(transform.m11(), devicePixelRatio)
        && qFuzzyCompare(transform.m22(), devicePixelRatio)
        && qFuzzyIsNull(transform.m12()) && qFuzzyIsNull(transform.m21());
    const qreal phaseX = nativeScale ? pixelPhase(transform.dx()) : 0.0;
    const qreal phaseY = nativeScale ? pixelPhase(transform.dy()) : 0.0;
    const QSize physicalSize = CoordinateHelper::toPhysical(canvasSize, devicePixelRatio);
    // A translated viewport may start between physical pixels. Preserve that
    // phase while rasterizing, then blit at an integral device-pixel position.
    // The extra edge pixel keeps the phase shift from clipping the cache.
    return {physicalSize.width() > 0 ? physicalSize.width() + qCeil(phaseX) : 0,
            physicalSize.height() > 0 ? physicalSize.height() + qCeil(phaseY) : 0,
            origin.x(), origin.y(), excludeIndex,
            qRound(devicePixelRatio * 1000.0), phaseX, phaseY};
}

QPointF AnnotationLayer::cachePixelPhase(const CacheKey& key, qreal devicePixelRatio)
{
    return QPointF(key.pixelPhaseX / devicePixelRatio, key.pixelPhaseY / devicePixelRatio);
}

void AnnotationLayer::paintCacheContents(QPainter& painter, const QPointF& origin, int excludeIndex) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.translate(-QPointF(origin));
    for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
        if (i != excludeIndex && m_items[i]->isVisible())
            m_items[i]->draw(painter);
    }
    painter.restore();
}

const QPixmap* AnnotationLayer::annotationCache(const CacheKey& key, qreal devicePixelRatio) const
{
    const auto found = m_annotationCaches.find(key);
    if (found != m_annotationCaches.end()) {
        found->second.lastUsed = ++m_annotationCacheClock;
        return &found->second.pixmap;
    }
    if (key.physicalWidth <= 0 || key.physicalHeight <= 0)
        return nullptr;
    const auto byteSize = [&](int depth) {
        const std::uint64_t pixels = std::uint64_t(key.physicalWidth) * std::uint64_t(key.physicalHeight);
        const std::uint64_t bytesPerPixel = std::uint64_t(qMax(1, (depth + 7) / 8));
        return pixels > (std::numeric_limits<std::uint64_t>::max)() / bytesPerPixel
            ? (std::numeric_limits<std::uint64_t>::max)() : pixels * bytesPerPixel;
    };
    // Evict before allocating, including before a single oversized MRU entry.
    makeAnnotationCacheRoom(byteSize(qMax(32, QPixmap::defaultDepth())));
    QPixmap cache(QSize(key.physicalWidth, key.physicalHeight));
    if (cache.isNull()) return nullptr;
    cache.setDevicePixelRatio(devicePixelRatio);
    cache.fill(Qt::transparent);
    const std::uint64_t bytes = byteSize(cache.depth());
    makeAnnotationCacheRoom(bytes);
    {
        QPainter painter(&cache);
        paintCacheContents(painter,
                           QPointF(key.originX, key.originY) - cachePixelPhase(key, devicePixelRatio),
                           key.excludeIndex);
    }
    const auto inserted = m_annotationCaches.emplace(key,
        CacheEntry{std::move(cache), bytes, ++m_annotationCacheClock}).first;
    m_annotationCacheBytes += bytes;
    return &inserted->second.pixmap;
}

void AnnotationLayer::appendItemToCaches(const AnnotationItem& item)
{
    for (auto it = m_annotationCaches.begin(); it != m_annotationCaches.end();) {
        const CacheKey& key = it->first;
        if (key.excludeIndex >= 0) {
            it = eraseAnnotationCache(it);
            continue;
        }

        if (item.isVisible()) {
            QPainter cachePainter(&it->second.pixmap);
            cachePainter.setRenderHint(QPainter::Antialiasing);
            cachePainter.setRenderHint(QPainter::SmoothPixmapTransform);
            cachePainter.translate(cachePixelPhase(key, it->second.pixmap.devicePixelRatio())
                                   - QPointF(key.originX, key.originY));
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

    const CacheKey cacheKey = cacheKeyForPainter(painter, canvasSize, devicePixelRatio, origin, -1);

    if (const QPixmap* cache = annotationCache(cacheKey, devicePixelRatio))
        painter.drawPixmap(-cachePixelPhase(cacheKey, devicePixelRatio), *cache);
    else
        paintCacheContents(painter, origin, -1);
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

    const int normalizedExcludeIndex =
        (excludeIndex >= 0 && excludeIndex < static_cast<int>(m_items.size())) ? excludeIndex : -1;
    const CacheKey cacheKey = cacheKeyForPainter(
        painter, canvasSize, devicePixelRatio, origin, normalizedExcludeIndex);

    if (normalizedExcludeIndex >= 0) {
        for (auto it = m_annotationCaches.begin(); it != m_annotationCaches.end();) {
            const CacheKey& existingKey = it->first;
            const bool sameViewport =
                existingKey.physicalWidth == cacheKey.physicalWidth &&
                existingKey.physicalHeight == cacheKey.physicalHeight &&
                existingKey.originX == cacheKey.originX &&
                existingKey.originY == cacheKey.originY &&
                existingKey.devicePixelRatioMilli == cacheKey.devicePixelRatioMilli &&
                existingKey.pixelPhaseX == cacheKey.pixelPhaseX &&
                existingKey.pixelPhaseY == cacheKey.pixelPhaseY;

            if (sameViewport && existingKey.excludeIndex != normalizedExcludeIndex) {
                it = eraseAnnotationCache(it);
            } else {
                ++it;
            }
        }
    }

    // Draw the cached background (all items except the one being dragged)
    if (const QPixmap* cache = annotationCache(cacheKey, devicePixelRatio))
        painter.drawPixmap(-cachePixelPhase(cacheKey, devicePixelRatio), *cache);
    else
        paintCacheContents(painter, origin, normalizedExcludeIndex);

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
