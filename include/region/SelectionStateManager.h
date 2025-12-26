#ifndef SELECTIONSTATEMANAGER_H
#define SELECTIONSTATEMANAGER_H

#include <QObject>
#include <QRect>
#include <QPoint>

/**
 * @brief Selection state and operation management
 *
 * Responsible for:
 * - Selection rectangle creation and modification
 * - Resize handle hit testing
 * - Move operations
 * - Bounds clamping
 */
class SelectionStateManager : public QObject {
    Q_OBJECT

public:
    enum class State {
        None,           // No selection
        Selecting,      // Dragging to create selection
        Complete,       // Selection complete
        ResizingHandle, // Resizing in progress
        Moving          // Moving in progress
    };
    Q_ENUM(State)

    enum class ResizeHandle {
        None,
        TopLeft, Top, TopRight,
        Left, Right,
        BottomLeft, Bottom, BottomRight
    };
    Q_ENUM(ResizeHandle)

    explicit SelectionStateManager(QObject* parent = nullptr);

    // State queries
    State state() const { return m_state; }
    bool isComplete() const { return m_state == State::Complete; }
    bool isSelecting() const { return m_state == State::Selecting; }
    bool isResizing() const { return m_state == State::ResizingHandle; }
    bool isMoving() const { return m_state == State::Moving; }

    // Selection rectangle
    QRect selectionRect() const { return m_selectionRect.normalized(); }
    void setSelectionRect(const QRect& rect);

    // Bounds setting
    void setBounds(const QRect& bounds);
    QRect bounds() const { return m_bounds; }

    // Selection operations
    void startSelection(const QPoint& pos);
    void updateSelection(const QPoint& pos);
    void finishSelection();
    void clearSelection();

    // Resize operations
    ResizeHandle hitTestHandle(const QPoint& pos, int handleSize = 16) const;
    void startResize(const QPoint& pos, ResizeHandle handle);
    void updateResize(const QPoint& pos);
    void finishResize();

    // Move operations
    bool hitTestMove(const QPoint& pos) const;
    void startMove(const QPoint& pos);
    void updateMove(const QPoint& pos);
    void finishMove();

    // Set selection from detected window
    void setFromDetectedWindow(const QRect& windowRect);

    // Cancel resize/move and restore original rect
    void cancelResizeOrMove();

    // Get original rect (for restoration on cancel)
    QRect originalRect() const { return m_originalRect; }

signals:
    void stateChanged(State newState);
    void selectionChanged(const QRect& rect);

private:
    void setState(State state);
    void clampToBounds();

    State m_state = State::None;
    QRect m_selectionRect;
    QRect m_bounds;

    // Operation temporaries
    QPoint m_startPoint;
    QPoint m_lastPoint;
    QRect m_originalRect;  // For resize/move restoration
    ResizeHandle m_activeHandle = ResizeHandle::None;
};

#endif // SELECTIONSTATEMANAGER_H
