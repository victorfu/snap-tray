#pragma once

#include <QByteArray>

namespace SnapTray {

struct LinuxXcursorEnvironment {
    QByteArray size;
    QByteArray theme;
    QByteArray path;
};

QByteArray xResourceValue(const QByteArray& resourceManager,
                          const char* resourceName,
                          const char* resourceClass);

LinuxXcursorEnvironment resolveLinuxXcursorEnvironment(
    const LinuxXcursorEnvironment& current,
    const QByteArray& resourceManager,
    const QByteArray& homePath);

void applyLinuxDesktopEnvironment();

} // namespace SnapTray
