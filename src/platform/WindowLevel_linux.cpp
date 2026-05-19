#include "WindowLevel.h"

void raiseWindowAboveMenuBar(QWidget* widget)
{
    Q_UNUSED(widget);
}

void setWindowClickThrough(QWidget* widget, bool enabled)
{
    Q_UNUSED(widget);
    Q_UNUSED(enabled);
}

void setWindowFloatingWithoutFocus(QWidget* widget)
{
    Q_UNUSED(widget);
}

void setWindowExcludedFromCapture(QWidget* widget, bool excluded)
{
    Q_UNUSED(widget);
    Q_UNUSED(excluded);
}

void setWindowExcludedFromCapture(QWindow* window, bool excluded)
{
    Q_UNUSED(window);
    Q_UNUSED(excluded);
}

void setWindowVisibleOnAllWorkspaces(QWidget* widget, bool enabled)
{
    Q_UNUSED(widget);
    Q_UNUSED(enabled);
}

void preventWindowHideOnDeactivate(QWidget* widget)
{
    Q_UNUSED(widget);
}

void forceNativeCursor(const QCursor& cursor, QWidget* widget)
{
    Q_UNUSED(cursor);
    Q_UNUSED(widget);
}

void raiseWindowAboveOverlays(QWidget* widget)
{
    Q_UNUSED(widget);
}

void raiseTransientWindowAboveParent(QWindow* window, QWidget* parentWidget)
{
    Q_UNUSED(window);
    Q_UNUSED(parentWidget);
}

void reinforceFramelessToolWindow(QWindow* window)
{
    Q_UNUSED(window);
}

void hideNativeWindowTitleBarIcon(QWindow* window)
{
    Q_UNUSED(window);
}

void hideNativeWindowTitleBarIcon(QWidget* widget)
{
    Q_UNUSED(widget);
}
