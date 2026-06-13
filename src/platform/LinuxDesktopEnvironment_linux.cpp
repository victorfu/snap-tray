#include "platform/LinuxDesktopEnvironment.h"

#include <QList>
#include <QtGlobal>

#include <X11/Xlib.h>
#include <X11/Xresource.h>

namespace {

bool isPositiveInteger(QByteArray value)
{
    value = value.trimmed();
    bool ok = false;
    const int parsed = value.toInt(&ok);
    return ok && parsed > 0;
}

QByteArray defaultXcursorPath(QByteArray homePath)
{
    homePath = homePath.trimmed();

    QList<QByteArray> entries;
    if (!homePath.isEmpty()) {
        entries << homePath + "/.local/share/icons";
        entries << homePath + "/.icons";
    }
    entries << "/usr/share/icons";
    entries << "/usr/share/pixmaps";
    return entries.join(':');
}

QByteArray rootResourceManagerString()
{
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        return {};
    }

    const char* resources = XResourceManagerString(display);
    QByteArray result = resources ? QByteArray(resources) : QByteArray();
    XCloseDisplay(display);
    return result;
}

bool isMissing(const QByteArray& value)
{
    return value.trimmed().isEmpty();
}

} // namespace

namespace SnapTray {

QByteArray xResourceValue(const QByteArray& resourceManager,
                          const char* resourceName,
                          const char* resourceClass)
{
    if (resourceManager.isEmpty() || !resourceName || !resourceClass) {
        return {};
    }

    QByteArray mutableResources = resourceManager;
    XrmInitialize();
    XrmDatabase database = XrmGetStringDatabase(mutableResources.data());
    if (!database) {
        return {};
    }

    char* valueType = nullptr;
    XrmValue value;
    QByteArray result;
    if (XrmGetResource(database, resourceName, resourceClass, &valueType, &value) &&
        value.addr && value.size > 0) {
        result = QByteArray(static_cast<const char*>(value.addr), static_cast<int>(value.size));
        while (result.endsWith('\0')) {
            result.chop(1);
        }
        result = result.trimmed();
    }

    XrmDestroyDatabase(database);
    return result;
}

LinuxXcursorEnvironment resolveLinuxXcursorEnvironment(
    const LinuxXcursorEnvironment& current,
    const QByteArray& resourceManager,
    const QByteArray& homePath)
{
    LinuxXcursorEnvironment resolved = current;

    if (isMissing(resolved.size)) {
        const QByteArray size =
            xResourceValue(resourceManager, "Xcursor.size", "Xcursor.Size");
        if (isPositiveInteger(size)) {
            resolved.size = size.trimmed();
        }
    }

    if (isMissing(resolved.theme)) {
        resolved.theme =
            xResourceValue(resourceManager, "Xcursor.theme", "Xcursor.Theme");
    }

    if (isMissing(resolved.path)) {
        resolved.path = defaultXcursorPath(homePath);
    }

    return resolved;
}

void applyLinuxDesktopEnvironment()
{
    LinuxXcursorEnvironment current;
    current.size = qgetenv("XCURSOR_SIZE");
    current.theme = qgetenv("XCURSOR_THEME");
    current.path = qgetenv("XCURSOR_PATH");

    const LinuxXcursorEnvironment resolved = resolveLinuxXcursorEnvironment(
        current,
        rootResourceManagerString(),
        qgetenv("HOME"));

    if (isMissing(current.size) && !resolved.size.isEmpty()) {
        qputenv("XCURSOR_SIZE", resolved.size);
    }
    if (isMissing(current.theme) && !resolved.theme.isEmpty()) {
        qputenv("XCURSOR_THEME", resolved.theme);
    }
    if (isMissing(current.path) && !resolved.path.isEmpty()) {
        qputenv("XCURSOR_PATH", resolved.path);
    }
}

} // namespace SnapTray
