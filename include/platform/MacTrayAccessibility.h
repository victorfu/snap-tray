#pragma once

#include <QString>

namespace SnapTray {

// QSystemTrayIcon gives its macOS status item a tooltip but no accessibility
// label, so VoiceOver announced only "status menu" (#248). Sets the label of
// this application's status item buttons; call after the tray icon is shown
// and whenever its status changes. Returns false when no status item exists.
bool setMacTrayAccessibilityLabel(const QString& label);

// The label of this application's first status item button (for tests).
QString macTrayAccessibilityLabel();

} // namespace SnapTray
