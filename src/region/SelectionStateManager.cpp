#include "region/SelectionStateManager.h"

SelectionStateManager::SelectionStateManager(QObject* parent)
    : QObject(parent)
{
}

void SelectionStateManager::setState(State state)
{
    if (m_state != state) {
        m_state = state;
        emit stateChanged(m_state);
    }
}

void SelectionStateManager::setSelectionRect(const QRect& rect)
{
    if (m_selectionRect != rect) {
        m_selectionRect = rect;
        emit selectionChanged(m_selectionRect.normalized());
    }
    // Ensure state is Complete so hasSelection() returns true
    if (rect.isValid() && !rect.isEmpty() && m_state == State::None) {
        setState(State::Complete);
    }
}

void SelectionStateManager::setBounds(const QRect& bounds)
{
    m_bounds = bounds;
}

// ============================================================================
// Selection Operations
// ============================================================================

void SelectionStateManager::startSelection(const QPoint& pos)
{
    m_startPoint = pos;
    m_lastPoint = pos;
    m_selectionRect = QRect(pos, pos);
    setState(State::Selecting);
}

void SelectionStateManager::updateSelection(const QPoint& pos)
{
    if (m_state != State::Selecting) return;

    QPoint delta = pos - m_startPoint;
    if (m_aspectRatio > 0.0) {
        delta = adjustDeltaForAspectRatio(delta);
    }

    m_lastPoint = m_startPoint + delta;
    m_selectionRect = QRect(m_startPoint, m_lastPoint);
    emit selectionChanged(m_selectionRect.normalized());
}

void SelectionStateManager::finishSelection()
{
    if (m_state != State::Selecting) return;

    m_selectionRect = m_selectionRect.normalized();

    // If selection is too small, clear it
    if (m_selectionRect.width() < 5 || m_selectionRect.height() < 5) {
        clearSelection();
        return;
    }

    setState(State::Complete);
}

void SelectionStateManager::clearSelection()
{
    m_selectionRect = QRect();
    m_activeHandle = ResizeHandle::None;
    setState(State::None);
    emit selectionChanged(QRect());
}

// ============================================================================
// Resize Operations
// ============================================================================

SelectionStateManager::ResizeHandle SelectionStateManager::hitTestHandle(
    const QPoint& pos, int handleSize) const
{
    if (m_state != State::Complete) return ResizeHandle::None;

    QRect rect = m_selectionRect.normalized();
    int half = handleSize / 2;

    // Define handle hit areas
    QRect topLeft(rect.left() - half, rect.top() - half, handleSize, handleSize);
    QRect top(rect.center().x() - half, rect.top() - half, handleSize, handleSize);
    QRect topRight(rect.right() - half, rect.top() - half, handleSize, handleSize);
    QRect left(rect.left() - half, rect.center().y() - half, handleSize, handleSize);
    QRect right(rect.right() - half, rect.center().y() - half, handleSize, handleSize);
    QRect bottomLeft(rect.left() - half, rect.bottom() - half, handleSize, handleSize);
    QRect bottom(rect.center().x() - half, rect.bottom() - half, handleSize, handleSize);
    QRect bottomRight(rect.right() - half, rect.bottom() - half, handleSize, handleSize);

    // Check corners first (higher priority)
    if (topLeft.contains(pos)) return ResizeHandle::TopLeft;
    if (topRight.contains(pos)) return ResizeHandle::TopRight;
    if (bottomLeft.contains(pos)) return ResizeHandle::BottomLeft;
    if (bottomRight.contains(pos)) return ResizeHandle::BottomRight;

    // Check edges
    if (top.contains(pos)) return ResizeHandle::Top;
    if (bottom.contains(pos)) return ResizeHandle::Bottom;
    if (left.contains(pos)) return ResizeHandle::Left;
    if (right.contains(pos)) return ResizeHandle::Right;

    return ResizeHandle::None;
}

void SelectionStateManager::startResize(const QPoint& pos, ResizeHandle handle)
{
    if (m_state != State::Complete) return;

    m_activeHandle = handle;
    m_startPoint = pos;
    m_originalRect = m_selectionRect;
    setState(State::ResizingHandle);
}

void SelectionStateManager::updateResize(const QPoint& pos)
{
    if (m_state != State::ResizingHandle) return;

    QPoint delta = pos - m_startPoint;
    QRect newRect = m_originalRect;

    if (m_aspectRatio > 0.0 && isCornerHandle(m_activeHandle)) {
        QRect rect = m_originalRect.normalized();
        QPoint anchor;
        switch (m_activeHandle) {
        case ResizeHandle::TopLeft:
            anchor = rect.bottomRight();
            break;
        case ResizeHandle::TopRight:
            anchor = rect.bottomLeft();
            break;
        case ResizeHandle::BottomLeft:
            anchor = rect.topRight();
            break;
        case ResizeHandle::BottomRight:
            anchor = rect.topLeft();
            break;
        default:
            anchor = rect.topLeft();
            break;
        }

        int dx = pos.x() - anchor.x();
        int dy = pos.y() - anchor.y();
        double absDx = qAbs(dx);
        double absDy = qAbs(dy);
        if (absDx < 1.0) absDx = 1.0;
        if (absDy < 1.0) absDy = 1.0;

        if (absDx / absDy > m_aspectRatio) {
            absDx = absDy * m_aspectRatio;
        } else {
            absDy = absDx / m_aspectRatio;
        }

        int adjDx = (dx < 0 ? -1 : 1) * static_cast<int>(absDx + 0.5);
        int adjDy = (dy < 0 ? -1 : 1) * static_cast<int>(absDy + 0.5);
        QPoint newCorner(anchor.x() + adjDx, anchor.y() + adjDy);
        newRect = QRect(anchor, newCorner);
    } else {
        switch (m_activeHandle) {
        case ResizeHandle::TopLeft:
            newRect.setTopLeft(m_originalRect.topLeft() + delta);
            break;
        case ResizeHandle::Top:
            newRect.setTop(m_originalRect.top() + delta.y());
            break;
        case ResizeHandle::TopRight:
            newRect.setTopRight(m_originalRect.topRight() + delta);
            break;
        case ResizeHandle::Left:
            newRect.setLeft(m_originalRect.left() + delta.x());
            break;
        case ResizeHandle::Right:
            newRect.setRight(m_originalRect.right() + delta.x());
            break;
        case ResizeHandle::BottomLeft:
            newRect.setBottomLeft(m_originalRect.bottomLeft() + delta);
            break;
        case ResizeHandle::Bottom:
            newRect.setBottom(m_originalRect.bottom() + delta.y());
            break;
        case ResizeHandle::BottomRight:
            newRect.setBottomRight(m_originalRect.bottomRight() + delta);
            break;
        default:
            break;
        }
    }

    // Ensure minimum size
    QRect normalized = newRect.normalized();
    if (normalized.width() >= 10 && normalized.height() >= 10) {
        m_selectionRect = newRect;
        emit selectionChanged(normalized);
    }
}

void SelectionStateManager::finishResize()
{
    if (m_state != State::ResizingHandle) return;

    m_selectionRect = m_selectionRect.normalized();
    m_activeHandle = ResizeHandle::None;
    setState(State::Complete);
}

// ============================================================================
// Move Operations
// ============================================================================

bool SelectionStateManager::hitTestMove(const QPoint& pos) const
{
    if (m_state != State::Complete) return false;
    return m_selectionRect.normalized().contains(pos);
}

void SelectionStateManager::startMove(const QPoint& pos)
{
    if (m_state != State::Complete) return;

    m_startPoint = pos;
    m_originalRect = m_selectionRect;
    setState(State::Moving);
}

void SelectionStateManager::updateMove(const QPoint& pos)
{
    if (m_state != State::Moving) return;

    QPoint delta = pos - m_startPoint;
    m_selectionRect = m_originalRect.translated(delta);

    // Clamp to bounds
    clampToBounds();

    emit selectionChanged(m_selectionRect.normalized());
}

void SelectionStateManager::finishMove()
{
    if (m_state != State::Moving) return;

    m_selectionRect = m_selectionRect.normalized();
    setState(State::Complete);
}

// ============================================================================
// Window Detection Support
// ============================================================================

void SelectionStateManager::setFromDetectedWindow(const QRect& windowRect)
{
    m_selectionRect = windowRect;
    clampToBounds();
    setState(State::Complete);
    emit selectionChanged(m_selectionRect.normalized());
}

// ============================================================================
// Cancel/Restore
// ============================================================================

void SelectionStateManager::cancelResizeOrMove()
{
    if (m_state == State::ResizingHandle || m_state == State::Moving) {
        m_selectionRect = m_originalRect;
        m_activeHandle = ResizeHandle::None;
        setState(State::Complete);
        emit selectionChanged(m_selectionRect.normalized());
    }
}

// ============================================================================
// Private Helpers
// ============================================================================

void SelectionStateManager::clampToBounds()
{
    if (m_bounds.isEmpty()) return;

    if (m_selectionRect.left() < m_bounds.left())
        m_selectionRect.moveLeft(m_bounds.left());
    if (m_selectionRect.top() < m_bounds.top())
        m_selectionRect.moveTop(m_bounds.top());
    if (m_selectionRect.right() > m_bounds.right())
        m_selectionRect.moveRight(m_bounds.right());
    if (m_selectionRect.bottom() > m_bounds.bottom())
        m_selectionRect.moveBottom(m_bounds.bottom());
}

QPoint SelectionStateManager::adjustDeltaForAspectRatio(const QPoint& delta) const
{
    if (m_aspectRatio <= 0.0) {
        return delta;
    }

    int dx = delta.x();
    int dy = delta.y();
    if (dx == 0 && dy == 0) {
        return delta;
    }

    double absDx = qAbs(dx);
    double absDy = qAbs(dy);
    if (absDx < 1.0) absDx = 1.0;
    if (absDy < 1.0) absDy = 1.0;

    if (absDx / absDy > m_aspectRatio) {
        absDx = absDy * m_aspectRatio;
    } else {
        absDy = absDx / m_aspectRatio;
    }

    int adjDx = (dx < 0 ? -1 : 1) * static_cast<int>(absDx + 0.5);
    int adjDy = (dy < 0 ? -1 : 1) * static_cast<int>(absDy + 0.5);
    return QPoint(adjDx, adjDy);
}

bool SelectionStateManager::isCornerHandle(ResizeHandle handle) const
{
    return handle == ResizeHandle::TopLeft ||
           handle == ResizeHandle::TopRight ||
           handle == ResizeHandle::BottomLeft ||
           handle == ResizeHandle::BottomRight;
}
