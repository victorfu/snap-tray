#ifndef ANNOTATIONLAYER_H
#define ANNOTATIONLAYER_H

#include <QObject>
#include <QPainter>
#include <QPoint>
#include <QRect>
#include <QPixmap>
#include <functional>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "annotations/AnnotationItem.h"
#include "annotations/RemovedAnnotationItem.h"

// Forward declarations
class TextBoxAnnotation;
class EmojiStickerAnnotation;
class ArrowAnnotation;
class ShapeAnnotation;

// Annotation layer that manages all annotations with undo/redo
class AnnotationLayer : public QObject
{
    Q_OBJECT

public:
    using AnnotationId = std::uint64_t;
    using HistoryStateToken = std::uint64_t;
    using RemovedItem = RemovedAnnotationItem;

    enum class HistoryStateRelation {
        Current,
        UndoReachable,
        RedoReachable,
        Unreachable
    };

    static constexpr std::size_t kMaxHistoryCommands = 100;
    static constexpr std::size_t kMaxHistoryOwnedBytes = 64u * 1024u * 1024u;

    explicit AnnotationLayer(QObject *parent = nullptr);
    ~AnnotationLayer();

    void addItem(std::unique_ptr<AnnotationItem> item);
    // Replaces the visible snapshot without N cache rebuilds/signals. All
    // items remain visible; only the newest kMaxHistoryCommands are undoable.
    void replaceItems(std::vector<std::unique_ptr<AnnotationItem>> items);
    void undo();
    void redo();
    void clear();
    void draw(QPainter &painter) const;
    void translateAll(const QPointF& delta);
    // Applies a caller-provided translation to every annotation owned by the
    // layer, including history- and transaction-owned items, then invalidates
    // rendering caches once. Canvas-wide transforms must keep these hidden
    // items aligned so a later undo/redo cannot restore stale coordinates.
    void translateOwnedItems(
        const std::function<QPointF(const AnnotationItem&)>& deltaForItem);
    void forEachItem(const std::function<void(AnnotationItem*)>& visitor,
                     bool includeRedoStack = false);
    void forEachItem(const std::function<void(const AnnotationItem*)>& visitor,
                     bool includeRedoStack = false) const;

    bool canUndo() const;
    bool canRedo() const;
    bool isEmpty() const;
    size_t itemCount() const { return m_items.size(); }
    QRect contentBoundingRect() const;
    std::uint64_t revision() const { return m_revision; }
    HistoryStateToken historyStateToken() const { return m_currentHistoryState; }
    HistoryStateRelation historyStateRelation(HistoryStateToken token) const;
    std::size_t historyCommandCount() const
    {
        return m_undoStack.size() + m_redoStack.size();
    }
    std::size_t historyOwnedBytes() const { return m_historyOwnedBytes; }

    // Access item by index (for re-editing)
    AnnotationItem* itemAt(int index);

    // Step badge helpers
    int countStepBadges() const;

    // Eraser support: remove items that intersect with the given path
    // Returns the removed items with their original indices (for undo support)
    std::vector<RemovedItem> removeItemsIntersecting(const QPoint &point, int strokeWidth);

    // Guard history while an eraser stroke owns items removed from the layer.
    // New annotations arriving asynchronously are deferred until the
    // transaction is committed or cancelled.
    void beginEraseTransaction();
    // Ends an empty/manual transaction and installs deferred annotations.
    // Returns false if a destructive layer operation invalidated the transaction.
    bool endEraseTransaction();
    // Finalizes a valid eraser gesture as one Remove command. The visible
    // items have already been removed by removeItemsIntersecting().
    bool commitEraseTransaction(std::vector<RemovedItem> items);
    // Restores an in-progress gesture without creating a Remove command, then
    // installs annotations that arrived while the gesture held the history lock.
    bool cancelEraseTransaction(std::vector<RemovedItem> items);
    bool isHistoryLocked() const { return m_eraseTransactionActive; }

    // Restore items removed by an in-progress eraser stroke without creating
    // a history entry. Used when the stroke is cancelled.
    void restoreRemovedItems(std::vector<RemovedItem> items);

    // Visits visible scene items, command-owned annotations, and deferred
    // additions exactly once. Intended for host-wide coordinate/source updates
    // such as PinWindow crop handling.
    void forEachOwnedItem(const std::function<void(AnnotationItem*)>& visitor);
    void forEachOwnedItem(const std::function<void(const AnnotationItem*)>& visitor) const;

    // Selection support for text/emoji annotations
    int hitTestText(const QPoint &pos) const;
    int hitTestEmojiSticker(const QPoint &pos) const;
    int hitTestShape(const QPoint &pos) const;
    int hitTestArrow(const QPoint &pos) const;
    int hitTestPolyline(const QPoint &pos) const;
    void setSelectedIndex(int index);
    int selectedIndex() const { return m_selectedIndex; }
    AnnotationItem* selectedItem();
    void clearSelection() { m_selectedIndex = -1; }
    bool removeSelectedItem();

    // Cache management for rendering optimization
    void invalidateCache();
    void drawCached(QPainter &painter,
                    const QSize &canvasSize,
                    qreal devicePixelRatio = 1.0,
                    const QPoint& origin = QPoint()) const;

    // Dirty region optimization for dragging operations
    // Instead of invalidating the entire cache during drag, mark only affected regions
    void markDirtyRect(const QRect& rect);
    void clearDirtyRect();
    bool hasDirtyRect() const { return m_hasDirtyRect; }

    // Draw with dirty region optimization - used during drag operations
    // Draws cached content for non-dirty areas, redraws only dirty region
    void drawWithDirtyRegion(QPainter &painter,
                             const QSize &canvasSize,
                             qreal devicePixelRatio,
                             int excludeIndex = -1,
                             const QPoint& origin = QPoint()) const;

    // Commit dirty region changes to cache after drag completes
    void commitDirtyRegion(const QSize &canvasSize, qreal devicePixelRatio);

signals:
    void changed();

private:
    struct SceneItem {
        AnnotationId id = 0;
        std::unique_ptr<AnnotationItem> item;

        AnnotationItem* get() const { return item.get(); }
        AnnotationItem* operator->() const { return item.get(); }
        AnnotationItem& operator*() const { return *item; }
        explicit operator bool() const { return static_cast<bool>(item); }
    };

    enum class HistoryCommandKind {
        Add,
        Remove
    };

    struct HistoryCommand {
        HistoryCommandKind kind = HistoryCommandKind::Add;
        std::vector<RemovedItem> items;
        HistoryStateToken beforeState = 0;
        HistoryStateToken afterState = 0;
        std::size_t retainedBytes = 0;
    };

    void addItemNow(std::unique_ptr<AnnotationItem> item);
    void renumberStepBadges();
    void appendItemToCaches(const AnnotationItem& item);
    AnnotationId allocateAnnotationId();
    HistoryStateToken allocateHistoryState();
    void pushAppliedCommand(HistoryCommand command);
    bool undoCommand(HistoryCommand& command);
    bool redoCommand(HistoryCommand& command);
    std::size_t calculateCommandRetainedBytes(const HistoryCommand& command) const;
    void refreshCommandRetainedBytes(HistoryCommand& command);
    void visitCommandOwnedItems(
        HistoryCommand& command,
        const std::function<void(AnnotationItem*)>& visitor);
    void adjustHistoryBytes(std::size_t before, std::size_t after);
    void trimHistory(HistoryStateToken protectedState);
    void clearRedoHistory();
    void evictUndoFront();
    void evictRedoFront();
    std::vector<SceneItem>::iterator findSceneItem(AnnotationId id);
    std::vector<SceneItem>::const_iterator findSceneItem(AnnotationId id) const;
    void restoreCommandItems(HistoryCommand& command);
    void removeCommandItems(HistoryCommand& command);
    bool restoreRemovedItemsNow(std::vector<RemovedItem> items);
    bool flushDeferredItems();

    // Authoritative canvas contents. Trimming this container deletes visible and
    // serialized annotations; any future undo limit must use separate history state.
    std::vector<SceneItem> m_items;
    std::vector<HistoryCommand> m_undoStack;
    std::vector<HistoryCommand> m_redoStack;
    AnnotationId m_nextAnnotationId = 1;
    HistoryStateToken m_nextHistoryState = 1;
    HistoryStateToken m_currentHistoryState = 0;
    std::size_t m_historyOwnedBytes = 0;
    std::size_t m_redoOwnedBytes = 0;
    bool m_eraseTransactionActive = false;
    std::vector<std::unique_ptr<AnnotationItem>> m_deferredItems;
    int m_selectedIndex = -1;

    struct CacheKey {
        int physicalWidth = 0;
        int physicalHeight = 0;
        int originX = 0;
        int originY = 0;
        int excludeIndex = -1;
        int devicePixelRatioMilli = 1000;

        bool operator<(const CacheKey& other) const
        {
            if (physicalWidth != other.physicalWidth) {
                return physicalWidth < other.physicalWidth;
            }
            if (physicalHeight != other.physicalHeight) {
                return physicalHeight < other.physicalHeight;
            }
            if (originX != other.originX) {
                return originX < other.originX;
            }
            if (originY != other.originY) {
                return originY < other.originY;
            }
            if (excludeIndex != other.excludeIndex) {
                return excludeIndex < other.excludeIndex;
            }
            return devicePixelRatioMilli < other.devicePixelRatioMilli;
        }
    };

    // Completed annotations caches for rendering optimization.
    mutable std::map<CacheKey, QPixmap> m_annotationCaches;
    std::uint64_t m_revision = 0;

    // Dirty region tracking for drag optimization
    mutable QRect m_dirtyRect;
    mutable bool m_hasDirtyRect = false;
};

#endif // ANNOTATIONLAYER_H
