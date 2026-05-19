#include "AutoLaunchManager.h"

#include "settings/AutoLaunchSettingsManager.h"
#include "settings/AutoLaunchSyncPolicy.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

#include <optional>

namespace {

QString desktopFilePath()
{
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return QDir(configDir).filePath(QStringLiteral("autostart/SnapTray.desktop"));
}

QString currentExecutablePath()
{
    return QCoreApplication::applicationFilePath();
}

bool desktopEntryExists()
{
    return QFileInfo::exists(desktopFilePath());
}

SnapTray::AutoLaunchSyncState startupEntryState()
{
    return desktopEntryExists()
        ? SnapTray::AutoLaunchSyncState::EnabledCurrentCanonical
        : SnapTray::AutoLaunchSyncState::Disabled;
}

} // namespace

bool AutoLaunchManager::isEnabled()
{
    return desktopEntryExists();
}

bool AutoLaunchManager::setEnabled(bool enabled)
{
    const QString path = desktopFilePath();
    if (!enabled) {
        return !QFileInfo::exists(path) || QFile::remove(path);
    }

    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }

    QTextStream out(&file);
    out << "[Desktop Entry]\n";
    out << "Type=Application\n";
    out << "Name=SnapTray\n";
    out << "Exec=" << currentExecutablePath() << " --minimized\n";
    out << "Icon=snaptray\n";
    out << "Terminal=false\n";
    out << "Categories=Utility;Graphics;\n";
    return true;
}

bool AutoLaunchManager::syncWithPreference()
{
    auto& settingsManager = AutoLaunchSettingsManager::instance();
    const std::optional<bool> preferredEnabled = settingsManager.loadPreferredEnabled();
    const SnapTray::AutoLaunchSyncPlan plan =
        SnapTray::buildAutoLaunchStartupSyncPlan(preferredEnabled, startupEntryState());

    if (plan.shouldApplyChange && !setEnabled(plan.targetEnabled)) {
        return isEnabled();
    }

    return isEnabled();
}
