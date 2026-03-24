#ifndef CURSORSURFACESUPPORT_H
#define CURSORSURFACESUPPORT_H

#include <QEvent>
#include <QApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QWidget>
#include <QWindow>

#include "cursor/CursorAuthority.h"
#include "cursor/CursorManager.h"
#include "cursor/CursorPlatformApplier.h"

namespace CursorSurfaceSupport {

inline bool isPointerRefreshEvent(QEvent::Type type)
{
    switch (type) {
    case QEvent::Enter:
    case QEvent::Leave:
    case QEvent::MouseMove:
    case QEvent::HoverEnter:
    case QEvent::HoverLeave:
    case QEvent::HoverMove:
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::FocusIn:
    case QEvent::WindowActivate:
        return true;
    default:
        return false;
    }
}

inline CursorStyleSpec currentCursorSpecForWindow(const QWindow* window)
{
    if (!window) {
        return CursorStyleSpec::fromShape(Qt::ArrowCursor);
    }
    return CursorStyleSpec::fromCursor(window->cursor());
}

inline QString defaultOwnerId(const QString& group)
{
    return QStringLiteral("%1.window").arg(group);
}

inline QString registerManagedSurface(QWindow* window, const QString& group)
{
    auto& authority = CursorAuthority::instance();
    return authority.registerWindowSurface(
        window,
        group,
        authority.defaultManagedModeForWindow(window, group));
}

inline void syncWindowSurface(QWindow* window, const QString& surfaceId,
                              const QString& ownerId, CursorRequestSource source,
                              const CursorStyleSpec* explicitStyle = nullptr)
{
    if (!window || surfaceId.isEmpty() || ownerId.isEmpty()) {
        return;
    }

    auto& authority = CursorAuthority::instance();
    const CursorStyleSpec legacyStyle = currentCursorSpecForWindow(window);
    const CursorStyleSpec requestStyle = explicitStyle ? *explicitStyle : legacyStyle;
    authority.submitRequest(surfaceId, ownerId, source, requestStyle);
    authority.recordLegacyApplied(surfaceId, legacyStyle);

    if (authority.surfaceMode(surfaceId) == CursorSurfaceMode::Authority) {
        CursorPlatformApplier::applyWindowCursor(window, authority.resolvedStyle(surfaceId));
    }
}

inline void clearWindowSurface(const QString& surfaceId, const QString& ownerId)
{
    if (surfaceId.isEmpty() || ownerId.isEmpty()) {
        return;
    }
    CursorAuthority::instance().clearRequest(surfaceId, ownerId);
}

inline void restoreWidgetCursorIfPointerOver(QWidget* widget)
{
    if (!widget || !widget->isVisible()) {
        return;
    }

    const QPoint globalPos = QCursor::pos();
    const QRect widgetBounds(widget->mapToGlobal(QPoint(0, 0)), widget->size());
    if (!widgetBounds.contains(globalPos)) {
        return;
    }

    if (QWidget* popup = QApplication::activePopupWidget();
        popup && popup->isVisible() && popup->frameGeometry().contains(globalPos)) {
        return;
    }

    QWindow* hostWindow = widget->windowHandle();
    for (QWindow* window : QGuiApplication::topLevelWindows()) {
        if (!window || !window->isVisible() || window == hostWindow) {
            continue;
        }

        if (QRect(window->position(), window->size()).contains(globalPos)) {
            return;
        }
    }

    const QPoint localPos = widget->mapFromGlobal(globalPos);
    QMouseEvent moveEvent(
        QEvent::MouseMove,
        QPointF(localPos),
        QPointF(localPos),
        QPointF(globalPos),
        Qt::NoButton,
        Qt::NoButton,
        Qt::NoModifier);
    QApplication::sendEvent(widget, &moveEvent);
    CursorManager::instance().reapplyCursorForWidget(widget);
}

}  // namespace CursorSurfaceSupport

#endif // CURSORSURFACESUPPORT_H
