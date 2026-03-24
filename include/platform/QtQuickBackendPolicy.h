#pragma once

#include <QOperatingSystemVersion>

namespace SnapTray {

enum class QtQuickGraphicsBackendPolicy {
    PlatformDefault,
    Software
};

QtQuickGraphicsBackendPolicy selectQtQuickGraphicsBackendPolicy(
    const QOperatingSystemVersion& version);

QtQuickGraphicsBackendPolicy currentQtQuickGraphicsBackendPolicy();

void applyQtQuickGraphicsBackendPolicy(QtQuickGraphicsBackendPolicy policy);

} // namespace SnapTray
