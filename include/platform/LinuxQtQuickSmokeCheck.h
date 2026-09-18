#pragma once

namespace SnapTray {

// Internal packaging check, called after the normal backend policy and
// QGuiApplication initialization. Returns nonzero if rendering fails.
int runLinuxQtQuickSmokeCheck();

} // namespace SnapTray
