// Multi-object selection and its group transactions (#370).
#include "annotations/AnnotationLayer.h"
#include "annotations/AnnotationGeometry.h"
#include "annotations/AnnotationStyle.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/PolylineAnnotation.h"
#include "annotations/ShapeAnnotation.h"

#include <QSet>
#include <algorithm>
#include <array>

namespace {

template <typename T> bool is(const AnnotationItem& item) { return dynamic_cast<const T*>(&item) != nullptr; }
template <typename T> bool touches(const AnnotationItem& item, const QRect& rect)
{
    return static_cast<const T&>(item).intersects(rect);
}

// Kinds whose painted shape is far from their bounding box (lines, curves,
// rotated or hollow shapes) test a marquee against that shape. Text and
// emoji fill their box, so the box is used for them.
struct MarqueeTest {
    bool (*matches)(const AnnotationItem&);
    bool (*touches)(const AnnotationItem&, const QRect&);
};
const std::array<MarqueeTest, 3> kMarqueeTests = {{
    {&is<ArrowAnnotation>, &touches<ArrowAnnotation>},
    {&is<PolylineAnnotation>, &touches<PolylineAnnotation>},
    {&is<ShapeAnnotation>, &touches<ShapeAnnotation>},
}};

bool marqueeTouches(const AnnotationItem& item, const QRect& rect)
{
    for (const MarqueeTest& test : kMarqueeTests) {
        if (test.matches(item)) return test.touches(item, rect);
    }
    return item.boundingRect().intersects(rect);
}

} // namespace

bool AnnotationLayer::isSelectable(const AnnotationItem& item)
{
    // The kinds the single-object editors select: text, emoji, shape, arrow
    // and polyline. Masks, strokes and step badges stay unselectable.
    return item.isVisible() && AnnotationGeometry::supports(item);
}

std::vector<AnnotationLayer::AnnotationId> AnnotationLayer::selectedIds() const
{
    std::vector<AnnotationId> ids;
    if (m_multiSelection.empty()) {
        const int index = selectedIndex();
        if (index >= 0 && m_items[index].item && m_items[index]->isVisible())
            ids.push_back(m_items[index].id);
        return ids;
    }
    // Drawing order; ids that left the scene (undo, eraser) or are hidden
    // (a text being re-edited) are skipped.
    const QSet<AnnotationId> wanted(m_multiSelection.cbegin(), m_multiSelection.cend());
    for (const auto& scene : m_items) {
        if (scene.item && scene->isVisible() && wanted.contains(scene.id)) ids.push_back(scene.id);
    }
    return ids;
}

bool AnnotationLayer::isSelected(AnnotationId id) const
{
    if (id == 0) return false;
    const auto ids = selectedIds();
    return std::find(ids.cbegin(), ids.cend(), id) != ids.cend();
}

void AnnotationLayer::setSelectedIds(const std::vector<AnnotationId>& ids)
{
    std::vector<AnnotationId> next;
    for (const auto& scene : m_items) {
        if (scene.item && isSelectable(*scene.item)
            && std::find(ids.cbegin(), ids.cend(), scene.id) != ids.cend())
            next.push_back(scene.id);
    }
    if (next.size() <= 1) {
        const AnnotationId single = next.empty() ? 0 : next.front();
        if (single == m_selectedId && m_multiSelection.empty()) return;
        m_selectedId = single;
        m_multiSelection.clear();
        emit selectionChanged();
        return;
    }
    if (m_selectedId == 0 && next == m_multiSelection) return;
    m_selectedId = 0;
    m_multiSelection = std::move(next);
    emit selectionChanged();
}

void AnnotationLayer::toggleSelected(AnnotationId id)
{
    auto ids = selectedIds();
    const auto it = std::find(ids.begin(), ids.end(), id);
    if (it != ids.end()) ids.erase(it);
    else ids.push_back(id);
    setSelectedIds(ids);
}

void AnnotationLayer::selectAll()
{
    std::vector<AnnotationId> ids;
    ids.reserve(m_items.size());
    for (const auto& scene : m_items) ids.push_back(scene.id);
    setSelectedIds(ids);
}

int AnnotationLayer::hitTestSelectable(const QPoint& pos) const
{
    // The topmost of the per-kind hit tests the single-object editors use.
    return std::max({hitTestText(pos), hitTestEmojiSticker(pos), hitTestShape(pos),
                     hitTestArrow(pos), hitTestPolyline(pos)});
}

std::vector<AnnotationLayer::AnnotationId> AnnotationLayer::selectableIdsIntersecting(
    const QRect& rect) const
{
    std::vector<AnnotationId> ids;
    const QRect area = rect.normalized();
    if (area.isEmpty()) return ids;
    for (const auto& scene : m_items) {
        if (scene.item && isSelectable(*scene.item) && marqueeTouches(*scene.item, area))
            ids.push_back(scene.id);
    }
    return ids;
}

QRect AnnotationLayer::selectionBoundingRect() const
{
    QRect bounds;
    for (AnnotationId id : selectedIds()) {
        const auto it = findSceneItem(id);
        if (it != m_items.cend() && it->item) bounds = bounds.united(it->item->boundingRect());
    }
    return bounds;
}

// ---------------------------------------------------------------------------
// Group transactions
// ---------------------------------------------------------------------------

void AnnotationLayer::dropGroupGesture()
{
    m_groupSnapshots.clear();
}

void AnnotationLayer::applyGroupOffset(const std::vector<GroupSnapshot>& from, const QPointF& offset)
{
    // Whole pixels, so arrows (integer endpoints) and the other kinds move alike.
    const QPointF delta(offset.toPoint());
    QSet<QUuid> moving;
    for (const auto& snapshot : from) {
        if (snapshot.item) moving.insert(snapshot.item->persistentId());
    }
    // Arrows first: a target moved afterwards re-resolves the endpoints bound
    // to it onto the already moved arrow, instead of moving them twice.
    for (bool arrows : {true, false}) {
        for (const auto& snapshot : from) {
            auto sceneIt = findSceneItem(snapshot.id);
            if (sceneIt == m_items.end() || !sceneIt->item || !snapshot.item) continue;
            AnnotationItem& item = *sceneIt->item;
            auto* arrow = dynamic_cast<ArrowAnnotation*>(&item);
            if ((arrow != nullptr) != arrows) continue;
            AnnotationGeometry::copy(*snapshot.item, item);
            if (delta.isNull()) continue;
            if (arrow) {
                // Like a single arrow's body drag, an endpoint bound to an
                // annotation that stays put comes loose; one bound inside the
                // group keeps its link and moves with it.
                for (bool end : {false, true}) {
                    if (!arrow->binding(end).targetId.isNull()
                        && !moving.contains(arrow->binding(end).targetId))
                        arrow->setBinding(end, {});
                }
            }
            item.translate(delta);
        }
    }
}

bool AnnotationLayer::beginGroupGesture()
{
    if (m_eraseTransactionActive) return false;
    if (isGestureActive()) {
        if (gestureIsReEdit()) return false;
        commitGesture(ModifyStyle | ModifyGeometry);
    }
    if (isGroupGestureActive()) commitGroupGesture();
    const auto ids = selectedIds();
    if (ids.empty()) return false;
    endModifyCoalescing();
    clearDirtyRect();
    for (AnnotationId id : ids) {
        auto sceneIt = findSceneItem(id);
        m_groupSnapshots.push_back({id, sceneIt->item->clone()});
    }
    emit changed();   // Undo/Redo availability
    return true;
}

bool AnnotationLayer::updateGroupGesture(const QPointF& totalOffset)
{
    if (!isGroupGestureActive()) return false;
    applyGroupOffset(m_groupSnapshots, totalOffset);
    invalidateCache();
    emit changed();
    return true;
}

bool AnnotationLayer::commitGroupGesture()
{
    if (!isGroupGestureActive()) return false;
    auto snapshots = std::move(m_groupSnapshots);
    dropGroupGesture();
    std::vector<RemovedItem> befores;
    std::vector<AnnotationId> ids;
    for (auto& snapshot : snapshots) {
        auto sceneIt = findSceneItem(snapshot.id);
        if (sceneIt == m_items.end() || !sceneIt->item || !snapshot.item) continue;
        ids.push_back(snapshot.id);
        if (AnnotationGeometry::equal(*snapshot.item, *sceneIt->item)) continue;
        befores.push_back({static_cast<std::size_t>(std::distance(m_items.begin(), sceneIt)),
                           std::move(snapshot.item), snapshot.id});
    }
    const bool recorded = !befores.empty();
    if (recorded) recordGroupModify(std::move(befores), ModifyGeometry, ids, QString());
    invalidateCache();
    emit changed();
    return recorded;
}

void AnnotationLayer::cancelGroupGesture()
{
    if (!isGroupGestureActive()) return;
    auto snapshots = std::move(m_groupSnapshots);
    dropGroupGesture();
    applyGroupOffset(snapshots, QPointF());
    invalidateCache();
    emit changed();
}

bool AnnotationLayer::extendsOpenGroupModify(const std::vector<AnnotationId>& ids,
                                             const QString& key) const
{
    return !key.isEmpty() && m_openModifyKey == key && m_openModifyGroup == ids
        && !m_undoStack.empty() && m_undoStack.back().kind == HistoryCommandKind::Modify;
}

void AnnotationLayer::recordGroupModify(std::vector<RemovedItem> befores, unsigned parts,
                                        const std::vector<AnnotationId>& ids,
                                        const QString& coalesceKey)
{
    if (extendsOpenGroupModify(ids, coalesceKey) && m_undoStack.back().parts == parts) {
        // The open command already holds the original state of annotations it
        // changed; add the ones this edit changes for the first time.
        HistoryCommand& open = m_undoStack.back();
        const std::size_t beforeBytes = open.retainedBytes;
        for (auto& record : befores) {
            const bool known = std::any_of(open.items.cbegin(), open.items.cend(),
                [&record](const RemovedItem& existing) { return existing.id == record.id; });
            if (!known) open.items.push_back(std::move(record));
        }
        refreshCommandRetainedBytes(open);
        adjustHistoryBytes(beforeBytes, open.retainedBytes);
        trimHistory(m_currentHistoryState);
        return;
    }
    HistoryCommand command;
    command.kind = HistoryCommandKind::Modify;
    command.parts = parts;
    command.items = std::move(befores);
    pushAppliedCommand(std::move(command));   // ends any open coalescing
    if (!coalesceKey.isEmpty()) {
        m_openModifyKey = coalesceKey;
        m_openModifyGroup = ids;
    }
}

bool AnnotationLayer::translateSelectedItems(const QPointF& delta, const QString& coalesceKey)
{
    if (m_eraseTransactionActive || isAnyGestureActive()) return false;
    if (QPointF(delta.toPoint()).isNull()) return false;
    const auto ids = selectedIds();
    if (ids.empty()) return false;
    std::vector<GroupSnapshot> snapshots;
    for (AnnotationId id : ids) snapshots.push_back({id, findSceneItem(id)->item->clone()});
    applyGroupOffset(snapshots, delta);

    std::vector<RemovedItem> befores;
    for (auto& snapshot : snapshots) {
        auto sceneIt = findSceneItem(snapshot.id);
        if (AnnotationGeometry::equal(*snapshot.item, *sceneIt->item)) continue;
        befores.push_back({static_cast<std::size_t>(std::distance(m_items.begin(), sceneIt)),
                           std::move(snapshot.item), snapshot.id});
    }
    if (befores.empty()) return false;
    recordGroupModify(std::move(befores), ModifyGeometry, ids, coalesceKey);
    invalidateCache();
    emit changed();
    return true;
}

bool AnnotationLayer::modifySelectedItems(const QString& coalesceKey,
                                          const std::function<bool(AnnotationItem&)>& mutate)
{
    if (m_eraseTransactionActive || isGroupGestureActive() || !mutate) return false;
    // A re-edited text is hidden and so never among the selected ids.
    const auto ids = selectedIds();
    std::vector<std::pair<AnnotationId, AnnotationItem*>> targets;
    for (AnnotationId id : ids) {
        auto sceneIt = findSceneItem(id);
        if (sceneIt == m_items.end() || !sceneIt->item) return false;
        // Annotations without style (emoji) are never handed to mutate.
        if (AnnotationStyle::kindOf(*sceneIt->item) != StyleTargetKind::None)
            targets.emplace_back(id, sceneIt->item.get());
    }
    if (targets.empty()) return false;

    std::vector<RemovedItem> befores;
    for (auto& [id, item] : targets) {
        std::unique_ptr<AnnotationItem> before = item->clone();
        if (!mutate(*item)) continue;
        // A style edit during a single drag is its own step (see modifyItem).
        if (m_gestureId == id && m_gestureSnapshot && !gestureIsReEdit())
            AnnotationStyle::copyStyle(*item, *m_gestureSnapshot);
        befores.push_back({static_cast<std::size_t>(indexOfId(id)), std::move(before), id});
    }
    if (befores.empty()) return false;
    recordGroupModify(std::move(befores), ModifyStyle, ids, coalesceKey);
    invalidateCache();
    emit changed();
    return true;
}

bool AnnotationLayer::removeSelectedItems()
{
    if (m_eraseTransactionActive || isGroupGestureActive()) return false;
    const auto ids = selectedIds();
    if (ids.empty()) return false;
    if (std::find(ids.cbegin(), ids.cend(), m_gestureId) != ids.cend()) {
        if (gestureIsReEdit()) return false;
        dropGesture();
    }

    HistoryCommand command;
    command.kind = HistoryCommandKind::Remove;
    // Ascending original indices; removing from the back keeps them valid,
    // which is also what Redo expects of a Remove command.
    for (AnnotationId id : ids) command.items.push_back({static_cast<std::size_t>(indexOfId(id)), nullptr, id});
    for (auto it = command.items.rbegin(); it != command.items.rend(); ++it) {
        const auto sceneIt = m_items.begin() + static_cast<std::ptrdiff_t>(it->originalIndex);
        prepareBindingRemoval(*it, *sceneIt->item);
        it->item = std::move(sceneIt->item);
        m_items.erase(sceneIt);
    }

    pushAppliedCommand(std::move(command));
    clearSelection();
    renumberStepBadges();
    rebuildBindingIndex();
    invalidateCache();
    emit changed();
    return true;
}
