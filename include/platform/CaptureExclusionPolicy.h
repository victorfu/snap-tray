#pragma once

#include <QOperatingSystemVersion>
#include <QtGlobal>

namespace SnapTray {
quint32 windowsCaptureAffinity(bool excluded, const QOperatingSystemVersion& version);
bool requiresVisibleRecordingControls(const QOperatingSystemVersion& version);
}
