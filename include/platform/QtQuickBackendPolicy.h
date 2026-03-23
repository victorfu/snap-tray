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

const char* qtQuickGraphicsBackendPolicyName(QtQuickGraphicsBackendPolicy policy);

} // namespace SnapTray
