#include "AutoLaunchManager.h"
#include <QCoreApplication>
#include <QSettings>

static const char* RUN_KEY = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const char* APP_NAME = "SnapTray";

bool AutoLaunchManager::isEnabled()
{
    QSettings settings(RUN_KEY, QSettings::NativeFormat);
    return settings.contains(APP_NAME);
}

bool AutoLaunchManager::setEnabled(bool enabled)
{
    QSettings settings(RUN_KEY, QSettings::NativeFormat);

    if (enabled) {
        QString appPath = QCoreApplication::applicationFilePath();
        // Convert to Windows path format with quotes for paths with spaces
        appPath = appPath.replace("/", "\\");
        settings.setValue(APP_NAME, QString("\"%1\"").arg(appPath));
    } else {
        settings.remove(APP_NAME);
    }

    return true;
}
