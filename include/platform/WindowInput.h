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

// Reads the native screen position of a left-button release before Qt dispatch.
#ifdef Q_OS_WIN
std::optional<QPoint> nativeLeftButtonReleasePosition(const QByteArray& eventType, void* message);
#else
inline std::optional<QPoint> nativeLeftButtonReleasePosition(const QByteArray&, void*)
{
    return std::nullopt;
}
#endif

// Keeps a frameless window's logical grab point under the native cursor, or
// under a supplied position in native desktop pixels.
// Returns false when the caller should use its Qt drag handling.
#ifdef Q_OS_WIN
bool moveFramelessWindowWithNativeCursor(QWidget* widget, const QPointF& grabOffset,
                                         std::optional<QPoint> nativePosition = std::nullopt);
#else
inline bool moveFramelessWindowWithNativeCursor(QWidget*, const QPointF&,
                                                std::optional<QPoint> = std::nullopt)
{
    return false;
}
#endif
}
