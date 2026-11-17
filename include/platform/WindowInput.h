#pragma once

class QWindow;

namespace SnapTray {
// Includes native click-through state, which need not appear in Qt window flags.
// GUI thread only; used when deciding which UI rectangles receive clicks.
bool windowAcceptsPointerInput(QWindow* window);
}
