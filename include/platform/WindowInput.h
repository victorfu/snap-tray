#pragma once

#include <QRegion>

class QWindow;

namespace SnapTray {
// Includes native click-through state, which need not appear in Qt window flags.
// GUI thread only; used when deciding which UI rectangles receive clicks.
bool windowAcceptsPointerInput(QWindow* window);
// Exposed input area in Qt global logical coordinates, accounting for windows above it.
QRegion windowPointerInputRegion(QWindow* window);
}
