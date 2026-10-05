#include "WindowLevel.h"

#include <QGuiApplication>
#include <QtGui/qguiapplication_platform.h>

#include <QRegion>
#include <QVector>
#include <QtMath>

#include <X11/Xlib.h>
#include <X11/extensions/shape.h>

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

void requestNativeWindowFocus(QWidget* widget)
{
    if (!widget || !widget->isVisible()) {
        return;
    }

    QWindow* window = widget->windowHandle();
    if (!window || !window->isVisible() || window->winId() == 0) {
        return;
    }

    auto* x11Application = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    if (!x11Application || !x11Application->display()) {
        return;
    }

    Display* display = x11Application->display();
    const auto nativeWindow = static_cast<::Window>(window->winId());
    XSetInputFocus(display, nativeWindow, RevertToParent, CurrentTime);
    XFlush(display);
}

void hideNativeWindowTitleBarIcon(QWindow* window)
{
    Q_UNUSED(window);
}

void hideNativeWindowTitleBarIcon(QWidget* widget)
{
    Q_UNUSED(widget);
}

bool setWindowInputRegion(QWidget* widget, const QRegion& region)
{
    auto* application = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    if (!widget || !widget->windowHandle() || !application || !application->display()) {
        return false;
    }

    Display* display = application->display();
    static const bool supportsInputShape = [display]() {
        int major = 0;
        int minor = 0;
        return XShapeQueryVersion(display, &major, &minor)
            && (major > 1 || (major == 1 && minor >= 1));
    }();
    if (!supportsInputShape) {
        return false;
    }

    const qreal scale = widget->devicePixelRatioF();
    QVector<XRectangle> rectangles;
    rectangles.reserve(region.rectCount());
    for (const QRect& rect : region) {
        const int left = qRound(rect.x() * scale);
        const int top = qRound(rect.y() * scale);
        const int right = qRound((rect.x() + rect.width()) * scale);
        const int bottom = qRound((rect.y() + rect.height()) * scale);
        rectangles.append({static_cast<short>(left), static_cast<short>(top),
                           static_cast<unsigned short>(right - left),
                           static_cast<unsigned short>(bottom - top)});
    }
    XShapeCombineRectangles(display, widget->winId(), ShapeInput, 0, 0,
                           rectangles.data(), rectangles.size(), ShapeSet, Unsorted);
    XFlush(display);
    return true;
}
