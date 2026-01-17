#include "region/SelectionResizeHelper.h"

SelectionResizeHelper::ResizeHandle SelectionResizeHelper::hitTestHandle(
    const QRect& selectionRect, const QPoint& pos, int handleSize)
{
    QRect rect = selectionRect.normalized();
    if (!rect.isValid() || rect.isEmpty()) {
        return ResizeHandle::None;
    }

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

SelectionResizeHelper::ResizeHandle SelectionResizeHelper::determineHandleFromOutsideClick(
    const QPoint& clickPos, const QRect& selectionRect)
{
    bool isAbove = clickPos.y() < selectionRect.top();
    bool isBelow = clickPos.y() > selectionRect.bottom();
    bool isLeft = clickPos.x() < selectionRect.left();
    bool isRight = clickPos.x() > selectionRect.right();

    // Corner cases
    if (isAbove && isLeft) return ResizeHandle::TopLeft;
    if (isAbove && isRight) return ResizeHandle::TopRight;
    if (isBelow && isLeft) return ResizeHandle::BottomLeft;
    if (isBelow && isRight) return ResizeHandle::BottomRight;

    // Edge cases
    if (isAbove) return ResizeHandle::Top;
    if (isBelow) return ResizeHandle::Bottom;
    if (isLeft) return ResizeHandle::Left;
    if (isRight) return ResizeHandle::Right;

    return ResizeHandle::None;
}

QRect SelectionResizeHelper::adjustEdgesToPosition(
    const QPoint& pos, ResizeHandle handle, const QRect& selectionRect)
{
    QRect adjusted = selectionRect;

    switch (handle) {
    case ResizeHandle::Top:
        adjusted.setTop(pos.y());
        break;
    case ResizeHandle::Bottom:
        adjusted.setBottom(pos.y());
        break;
    case ResizeHandle::Left:
        adjusted.setLeft(pos.x());
        break;
    case ResizeHandle::Right:
        adjusted.setRight(pos.x());
        break;
    case ResizeHandle::TopLeft:
        adjusted.setTop(pos.y());
        adjusted.setLeft(pos.x());
        break;
    case ResizeHandle::TopRight:
        adjusted.setTop(pos.y());
        adjusted.setRight(pos.x());
        break;
    case ResizeHandle::BottomLeft:
        adjusted.setBottom(pos.y());
        adjusted.setLeft(pos.x());
        break;
    case ResizeHandle::BottomRight:
        adjusted.setBottom(pos.y());
        adjusted.setRight(pos.x());
        break;
    default:
        break;
    }

    return adjusted.normalized();
}

bool SelectionResizeHelper::meetsMinimumSize(const QRect& rect, int minWidth, int minHeight)
{
    return rect.width() >= minWidth && rect.height() >= minHeight;
}
