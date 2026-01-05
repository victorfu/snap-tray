#include "region/SelectionResizeHelper.h"

Qt::CursorShape SelectionResizeHelper::cursorForHandle(ResizeHandle handle)
{
    switch (handle) {
    case ResizeHandle::TopLeft:
    case ResizeHandle::BottomRight:
        return Qt::SizeFDiagCursor;
    case ResizeHandle::TopRight:
    case ResizeHandle::BottomLeft:
        return Qt::SizeBDiagCursor;
    case ResizeHandle::Top:
    case ResizeHandle::Bottom:
        return Qt::SizeVerCursor;
    case ResizeHandle::Left:
    case ResizeHandle::Right:
        return Qt::SizeHorCursor;
    case ResizeHandle::None:
    default:
        return Qt::ArrowCursor;
    }
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
