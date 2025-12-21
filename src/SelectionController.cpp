#include "SelectionController.h"

const int SelectionController::HANDLE_SIZE;

SelectionController::SelectionController(QObject* parent)
    : QObject(parent)
    , m_isComplete(false)
    , m_isResizing(false)
    , m_isMoving(false)
    , m_activeHandle(Handle::None)
    , m_minimumSize(10, 10)
{
}

void SelectionController::setSelectionRect(const QRect& rect)
{
    if (m_selectionRect != rect) {
        m_selectionRect = rect;
        emit selectionChanged(rect);
    }
}

void SelectionController::setComplete(bool complete)
{
    m_isComplete = complete;
}

SelectionController::Handle SelectionController::handleAtPosition(const QPoint& pos) const
{
    if (!m_isComplete) return Handle::None;

    QRect sel = m_selectionRect.normalized();
    const int hs = HANDLE_SIZE;

    // Define handle hit areas
    QRect topLeft(sel.left() - hs/2, sel.top() - hs/2, hs, hs);
    QRect top(sel.center().x() - hs/2, sel.top() - hs/2, hs, hs);
    QRect topRight(sel.right() - hs/2, sel.top() - hs/2, hs, hs);
    QRect left(sel.left() - hs/2, sel.center().y() - hs/2, hs, hs);
    QRect right(sel.right() - hs/2, sel.center().y() - hs/2, hs, hs);
    QRect bottomLeft(sel.left() - hs/2, sel.bottom() - hs/2, hs, hs);
    QRect bottom(sel.center().x() - hs/2, sel.bottom() - hs/2, hs, hs);
    QRect bottomRight(sel.right() - hs/2, sel.bottom() - hs/2, hs, hs);

    // Check corners first (higher priority)
    if (topLeft.contains(pos)) return Handle::TopLeft;
    if (topRight.contains(pos)) return Handle::TopRight;
    if (bottomLeft.contains(pos)) return Handle::BottomLeft;
    if (bottomRight.contains(pos)) return Handle::BottomRight;

    // Check edges
    if (top.contains(pos)) return Handle::Top;
    if (bottom.contains(pos)) return Handle::Bottom;
    if (left.contains(pos)) return Handle::Left;
    if (right.contains(pos)) return Handle::Right;

    // Check if inside selection (for move)
    if (sel.contains(pos)) return Handle::Center;

    return Handle::None;
}

void SelectionController::startResize(Handle handle, const QPoint& pos)
{
    if (handle == Handle::None) return;

    m_activeHandle = handle;
    m_startPoint = pos;
    m_originalRect = m_selectionRect;

    if (handle == Handle::Center) {
        m_isMoving = true;
        m_isResizing = false;
    } else {
        m_isResizing = true;
        m_isMoving = false;
    }

    emit resizeStarted();
}

void SelectionController::updateResize(const QPoint& pos)
{
    if (!m_isResizing && !m_isMoving) return;

    QPoint delta = pos - m_startPoint;

    if (m_isMoving) {
        // Move selection
        m_selectionRect = m_originalRect.translated(delta);

        // Clamp to viewport
        if (!m_viewportRect.isEmpty()) {
            if (m_selectionRect.left() < m_viewportRect.left())
                m_selectionRect.moveLeft(m_viewportRect.left());
            if (m_selectionRect.top() < m_viewportRect.top())
                m_selectionRect.moveTop(m_viewportRect.top());
            if (m_selectionRect.right() > m_viewportRect.right())
                m_selectionRect.moveRight(m_viewportRect.right());
            if (m_selectionRect.bottom() > m_viewportRect.bottom())
                m_selectionRect.moveBottom(m_viewportRect.bottom());
        }
    } else {
        // Resize based on handle
        QRect newRect = m_originalRect;

        switch (m_activeHandle) {
        case Handle::TopLeft:
            newRect.setTopLeft(m_originalRect.topLeft() + delta);
            break;
        case Handle::Top:
            newRect.setTop(m_originalRect.top() + delta.y());
            break;
        case Handle::TopRight:
            newRect.setTopRight(m_originalRect.topRight() + delta);
            break;
        case Handle::Left:
            newRect.setLeft(m_originalRect.left() + delta.x());
            break;
        case Handle::Right:
            newRect.setRight(m_originalRect.right() + delta.x());
            break;
        case Handle::BottomLeft:
            newRect.setBottomLeft(m_originalRect.bottomLeft() + delta);
            break;
        case Handle::Bottom:
            newRect.setBottom(m_originalRect.bottom() + delta.y());
            break;
        case Handle::BottomRight:
            newRect.setBottomRight(m_originalRect.bottomRight() + delta);
            break;
        default:
            break;
        }

        m_selectionRect = newRect;
    }

    emit selectionChanged(m_selectionRect);
}

void SelectionController::finishResize()
{
    m_isResizing = false;
    m_isMoving = false;
    m_activeHandle = Handle::None;
    m_selectionRect = m_selectionRect.normalized();
    emit resizeFinished();
}

void SelectionController::cancelResize()
{
    m_selectionRect = m_originalRect;
    m_isResizing = false;
    m_isMoving = false;
    m_activeHandle = Handle::None;
    emit selectionChanged(m_selectionRect);
}

Qt::CursorShape SelectionController::cursorForHandle(Handle handle) const
{
    switch (handle) {
    case Handle::TopLeft:
    case Handle::BottomRight:
        return Qt::SizeFDiagCursor;
    case Handle::TopRight:
    case Handle::BottomLeft:
        return Qt::SizeBDiagCursor;
    case Handle::Top:
    case Handle::Bottom:
        return Qt::SizeVerCursor;
    case Handle::Left:
    case Handle::Right:
        return Qt::SizeHorCursor;
    case Handle::Center:
        return Qt::SizeAllCursor;
    default:
        return Qt::ArrowCursor;
    }
}

void SelectionController::setViewportRect(const QRect& rect)
{
    m_viewportRect = rect;
}

void SelectionController::setMinimumSize(const QSize& size)
{
    m_minimumSize = size;
}
