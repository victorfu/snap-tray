#include "cursor/CursorPlatformApplier.h"

#include <QWidget>
#include <QWindow>

#include "cursor/CursorStyleCatalog.h"
#include "platform/WindowLevel.h"

void CursorPlatformApplier::applyWidgetCursor(QWidget* widget, const QCursor& cursor)
{
    if (!widget) {
        return;
    }

    widget->setCursor(cursor);
    reassertNativeStyle(CursorStyleSpec::fromCursor(cursor), widget, nullptr);
}

void CursorPlatformApplier::applyWidgetCursor(QWidget* widget, const CursorStyleSpec& spec)
{
    if (!widget) {
        return;
    }

    const QCursor cursor = CursorStyleCatalog::instance().cursorForStyle(spec);
    applyWidgetCursor(widget, cursor);
}

void CursorPlatformApplier::applyWindowCursor(QWindow* window, const CursorStyleSpec& spec)
{
    if (!window) {
        return;
    }

    const QCursor cursor = CursorStyleCatalog::instance().cursorForStyle(spec);
    window->setCursor(cursor);
    reassertNativeStyle(spec, nullptr, window);
}

void CursorPlatformApplier::reassertNativeStyle(const CursorStyleSpec& spec, QWidget* widget, QWindow*)
{
    Q_UNUSED(spec)
    Q_UNUSED(widget)
}
