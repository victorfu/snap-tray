#pragma once

#include <QRegion>

#include <optional>

class QWindow;
class QWidget;

namespace SnapTray {
// Includes native click-through state, which need not appear in Qt window flags.
// GUI thread only; used when deciding which UI rectangles receive clicks.
bool windowAcceptsPointerInput(QWindow* window);
// Exposed input area in Qt global logical coordinates, accounting for windows above it.
QRegion windowPointerInputRegion(QWindow* window);

// Moves a frameless window by its widget-local logical grab offset. When given
// a widget-local event position, use that instead of sampling the live cursor.
// Returns false when the caller should use its Qt drag handling.
#ifdef Q_OS_WIN
bool moveFramelessWindowWithNativeCursor(QWidget* widget, const QPointF& grabOffset,
                                         std::optional<QPointF> eventPosition = std::nullopt);
#else
inline bool moveFramelessWindowWithNativeCursor(QWidget*, const QPointF&,
                                                std::optional<QPointF> = std::nullopt)
{
    return false;
}
#endif
}
