#include "WindowLevel.h"
#include "platform/WindowInput.h"

#include <QGuiApplication>
#include <QtGui/qguiapplication_platform.h>

#include <QRegion>
#include <QSet>
#include <QVector>
#include <QtMath>

#include <X11/Xlib.h>
#include <X11/extensions/shape.h>

void raiseWindowAboveMenuBar(QWidget* widget)
{
    Q_UNUSED(widget);
}

void raiseWindowAboveMenuBar(QWindow* window)
{
    Q_UNUSED(window);
}

bool SnapTray::windowAcceptsPointerInput(QWindow* window)
{
    if (!window || window->flags().testFlag(Qt::WindowTransparentForInput)) return false;
    return true;
}

QRegion SnapTray::windowPointerInputRegion(QWindow* window)
{
    if (!window || !window->isVisible() || !windowAcceptsPointerInput(window)) return {};
    auto* app = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    if (!app || !app->display()) return {};
    Display* display = app->display();
    const ::Window root = DefaultRootWindow(display);
    const auto rootChild = [display, root](::Window child) {
        while (child && child != root) {
            ::Window treeRoot = 0, parent = 0, *children = nullptr;
            unsigned int count = 0;
            if (!XQueryTree(display, child, &treeRoot, &parent, &children, &count)) return ::Window(0);
            if (children) XFree(children);
            if (parent == root) return child;
            child = parent;
        }
        return ::Window(0);
    };
    const auto native = static_cast<::Window>(window->winId());
    const ::Window frame = rootChild(native);
    if (!frame) return {};
    XWindowAttributes frameAttributes{};
    if (!XGetWindowAttributes(display, frame, &frameAttributes)
        || frameAttributes.map_state != IsViewable) return {};
    ::Window treeRoot = 0, parent = 0, *children = nullptr;
    unsigned int count = 0;
    if (!XQueryTree(display, root, &treeRoot, &parent, &children, &count)) return {};
    int nativeX = 0, nativeY = 0;
    ::Window unused = 0;
    XTranslateCoordinates(display, native, root, 0, 0, &nativeX, &nativeY, &unused);
    const qreal scale = window->devicePixelRatio();
    const QPoint logicalOrigin = window->geometry().topLeft();
    QRegion exposed(window->frameGeometry());
    int shapeMajor = 0, shapeMinor = 0;
    const bool hasInputShape = XShapeQueryVersion(display, &shapeMajor, &shapeMinor)
        && (shapeMajor > 1 || (shapeMajor == 1 && shapeMinor >= 1));
    // XQueryTree is bottom-to-top and includes unmanaged popups, unlike the
    // window manager's client list. A reparented client's root child is its frame.
    QSet<::Window> clickThroughFrames;
    for (QWindow* own : QGuiApplication::topLevelWindows()) {
        if (own->isVisible() && !windowAcceptsPointerInput(own))
            clickThroughFrames.insert(rootChild(static_cast<::Window>(own->winId())));
    }
    bool above = false;
    for (unsigned int i = 0; i < count; ++i) {
        if (children[i] == frame) { above = true; continue; }
        if (!above) continue;
        if (clickThroughFrames.contains(children[i])) continue;
        XWindowAttributes attributes{};
        if (!XGetWindowAttributes(display, children[i], &attributes)
            || attributes.map_state != IsViewable) continue;
        QRegion input(QRect(0, 0, attributes.width, attributes.height));
        if (hasInputShape) {
            int rectangleCount = 0, ordering = 0;
            XRectangle* rectangles = XShapeGetRectangles(display, children[i], ShapeInput,
                                                        &rectangleCount, &ordering);
            input = {};
            for (int j = 0; j < rectangleCount; ++j) {
                const auto& r = rectangles[j];
                input += QRect(r.x, r.y, r.width, r.height);
            }
            if (rectangles) XFree(rectangles);
        }
        for (const QRect& rect : input) {
            exposed -= QRectF(logicalOrigin.x() + (attributes.x + rect.x() - nativeX) / scale,
                              logicalOrigin.y() + (attributes.y + rect.y() - nativeY) / scale,
                              rect.width() / scale, rect.height() / scale).toAlignedRect();
        }
        if (exposed.isEmpty()) break;
    }
    if (children) XFree(children);
    return above ? exposed : QRegion{};
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
