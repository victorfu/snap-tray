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
    const QString appImagePath = qEnvironmentVariable("APPIMAGE").trimmed();
    return appImagePath.isEmpty()
        ? QCoreApplication::applicationFilePath()
        : appImagePath;
}

QString desktopExecQuotedArgument(const QString& value)
{
    QString escaped = value;
    escaped.replace('\\', QStringLiteral("\\\\"));
    escaped.replace('"', QStringLiteral("\\\""));
    escaped.replace('`', QStringLiteral("\\`"));
    escaped.replace('$', QStringLiteral("\\$"));
    return QStringLiteral("\"%1\"").arg(escaped);
}

QString desktopExecCommand()
{
    return QStringLiteral("%1 --minimized").arg(desktopExecQuotedArgument(currentExecutablePath()));
}

bool desktopEntryExists()
{
    return QFileInfo::exists(desktopFilePath());
}

SnapTray::AutoLaunchSyncState startupEntryState()
{
    QFile file(desktopFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return SnapTray::AutoLaunchSyncState::Disabled;
    }

    const QString canonicalExecutableArgument = desktopExecQuotedArgument(currentExecutablePath());
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (!line.startsWith(QStringLiteral("Exec="))) {
            continue;
        }

        return line.mid(QStringLiteral("Exec=").size()).contains(canonicalExecutableArgument)
            ? SnapTray::AutoLaunchSyncState::EnabledCurrentCanonical
            : SnapTray::AutoLaunchSyncState::EnabledCurrentLegacy;
    }

    return SnapTray::AutoLaunchSyncState::EnabledCurrentLegacy;
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
    out << "Exec=" << desktopExecCommand() << "\n";
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
