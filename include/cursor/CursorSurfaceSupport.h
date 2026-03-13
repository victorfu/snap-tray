#ifndef CURSORSURFACESUPPORT_H
#define CURSORSURFACESUPPORT_H

#include <QEvent>
#include <QWindow>

#include "cursor/CursorAuthority.h"
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
    case QEvent::FocusOut:
    case QEvent::WindowActivate:
    case QEvent::WindowDeactivate:
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
                              const QString& ownerId, CursorRequestSource source)
{
    if (!window || surfaceId.isEmpty() || ownerId.isEmpty()) {
        return;
    }

    auto& authority = CursorAuthority::instance();
    const CursorStyleSpec legacyStyle = currentCursorSpecForWindow(window);
    authority.submitRequest(surfaceId, ownerId, source, legacyStyle);
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

}  // namespace CursorSurfaceSupport

#endif // CURSORSURFACESUPPORT_H
