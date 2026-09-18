#pragma once

#include <QOperatingSystemVersion>

namespace SnapTray {

enum class QtQuickGraphicsBackendPolicy {
    PlatformDefault,
    Software
};

QtQuickGraphicsBackendPolicy selectQtQuickGraphicsBackendPolicy(
    const QOperatingSystemVersion& version);

// Includes platform policy for Linux, which has no QOperatingSystemVersion type.
QtQuickGraphicsBackendPolicy currentQtQuickGraphicsBackendPolicy();

void applyQtQuickGraphicsBackendPolicy(QtQuickGraphicsBackendPolicy policy);

} // namespace SnapTray
