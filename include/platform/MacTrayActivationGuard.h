#pragma once

namespace SnapTray {

// Call on the GUI thread after QApplication loads the Cocoa platform plugin,
// before showing a tray icon. Safe to call more than once.
bool installMacTrayActivationGuard();

} // namespace SnapTray
