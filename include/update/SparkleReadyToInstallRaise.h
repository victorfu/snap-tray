#ifndef SNAPTRAY_SPARKLEREADYTOINSTALLRAISE_H
#define SNAPTRAY_SPARKLEREADYTOINSTALLRAISE_H

// Sparkle's standard user driver only requests Dock attention when an update the
// user chose to install becomes "Ready to Install", so its window can stay behind
// the app they switched to while it downloaded. Wraps that step to order the
// window above other apps without making it key. Call after Sparkle.framework is
// loaded; repeated calls are safe.
bool installSparkleReadyToInstallRaise();

#endif // SNAPTRAY_SPARKLEREADYTOINSTALLRAISE_H
