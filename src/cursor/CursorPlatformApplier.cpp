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

    const CursorStyleSpec spec = CursorStyleSpec::fromCursor(cursor);
#ifdef Q_OS_MACOS
    const QCursor appliedCursor = spec.styleId == CursorStyleId::Move
        ? CursorStyleCatalog::instance().cursorForStyle(spec)
        : cursor;
    widget->setCursor(appliedCursor);
#else
    widget->setCursor(cursor);
#endif
    reassertNativeStyle(spec, widget, nullptr);
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
#ifdef Q_OS_MAC
    switch (spec.styleId) {
    case CursorStyleId::Arrow:
    case CursorStyleId::Crosshair:
    case CursorStyleId::PointingHand:
    case CursorStyleId::ClosedHand:
    case CursorStyleId::TextBeam:
    case CursorStyleId::ResizeHorizontal:
    case CursorStyleId::ResizeVertical:
    case CursorStyleId::ResizeDiagonalForward:
    case CursorStyleId::ResizeDiagonalBackward:
    case CursorStyleId::Move:
    case CursorStyleId::MosaicBrush:
    case CursorStyleId::EraserBrush:
        forceNativeCursor(CursorStyleCatalog::instance().cursorForStyle(spec), widget);
        break;
    case CursorStyleId::LegacyCursor:
        if (!spec.legacyCursor.pixmap().isNull() || spec.legacyCursor.shape() == Qt::CrossCursor) {
            forceNativeCursor(spec.legacyCursor, widget);
        }
        break;
    case CursorStyleId::OpenHand:
        // Let Qt keep rendering OpenHand to preserve platform behavior.
        break;
    }
#else
    Q_UNUSED(spec)
    Q_UNUSED(widget)
#endif
}
