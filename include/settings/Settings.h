#pragma once

#include <QDir>
#include <QSettings>
#include <QStringList>
#include <mutex>
#include "version.h"

namespace SnapTray {

inline constexpr const char* kOrganizationName = "Victor Fu";
inline constexpr const char* kApplicationName = SNAPTRAY_APP_NAME;

// Hotkey settings keys
inline constexpr const char* kSettingsKeyHotkey = "hotkey";
inline constexpr const char* kSettingsKeyScreenCanvasHotkey = "screenCanvasHotkey";
inline constexpr const char* kSettingsKeyPasteHotkey = "pasteHotkey";
inline constexpr const char* kSettingsKeyQuickPinHotkey = "quickPinHotkey";
inline constexpr const char* kSettingsKeyPinFromImageHotkey = "pinFromImageHotkey";
inline constexpr const char* kSettingsKeyPinHistoryHotkey = "pinHistoryHotkey";
inline constexpr const char* kSettingsKeyTogglePinsVisibilityHotkey = "togglePinsVisibilityHotkey";
inline constexpr const char* kSettingsKeyRecordFullScreenHotkey = "recordFullScreenHotkey";

// Hotkey default values
inline constexpr const char* kDefaultHotkey = "F2";
inline constexpr const char* kDefaultScreenCanvasHotkey = "Ctrl+F2";
inline constexpr const char* kDefaultPasteHotkey = "F3";
inline constexpr const char* kDefaultQuickPinHotkey = "Shift+F2";
inline constexpr const char* kDefaultPinFromImageHotkey = "";  // No default
inline constexpr const char* kDefaultPinHistoryHotkey = "";  // No default
inline constexpr const char* kDefaultTogglePinsVisibilityHotkey = "";  // No default
inline constexpr const char* kDefaultRecordFullScreenHotkey = "";  // No default
inline constexpr const char* kSettingsMigrationVersionKey = "__meta/settingsMigrationVersion";
inline constexpr int kSettingsMigrationVersion = 2;

inline bool isDebugSettingsNamespace()
{
    return QString::fromLatin1(SNAPTRAY_APP_BUNDLE_ID).endsWith(QStringLiteral(".debug"));
}

inline QString windowsSettingsPath()
{
    if (isDebugSettingsNamespace()) {
        return QStringLiteral("HKEY_CURRENT_USER\\Software\\SnapTray-Debug");
    }
    return QStringLiteral("HKEY_CURRENT_USER\\Software\\SnapTray");
}

inline QString macSettingsPath()
{
    const QString fileName = isDebugSettingsNamespace()
        ? QStringLiteral("cc.snaptray.debug.plist")
        : QStringLiteral("cc.snaptray.plist");
    return QDir::homePath() + QStringLiteral("/Library/Preferences/") + fileName;
}

inline QString normalizeSettingsLocation(QString location)
{
    location.replace('\\', '/');
    while (location.endsWith('/')) {
        location.chop(1);
    }
    return location.toLower();
}

inline bool isSameSettingsStore(const QSettings& lhs, const QSettings& rhs)
{
    const QString lhsLocation = normalizeSettingsLocation(lhs.fileName());
    const QString rhsLocation = normalizeSettingsLocation(rhs.fileName());
    return !lhsLocation.isEmpty() && lhsLocation == rhsLocation;
}

#if defined(Q_OS_WIN)
inline QSettings platformSettingsStore()
{
    return QSettings(windowsSettingsPath(), QSettings::NativeFormat);
}

inline QStringList windowsLegacySettingsPaths()
{
    if (isDebugSettingsNamespace()) {
        return {
            QStringLiteral("HKEY_CURRENT_USER\\Software\\Victor Fu\\SnapTray-Debug"),
            // Temporary key used while testing channel-specific stores.
            QStringLiteral("HKEY_CURRENT_USER\\Software\\SnapTray\\Debug")
        };
    }
    return {
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Victor Fu\\SnapTray")
    };
}
#elif defined(Q_OS_MACOS)
inline QSettings platformSettingsStore()
{
    return QSettings(macSettingsPath(), QSettings::NativeFormat);
}

inline QStringList macLegacySettingsPaths()
{
    const QString base = QDir::homePath() + QStringLiteral("/Library/Preferences/");
    if (isDebugSettingsNamespace()) {
        return {
            base + QStringLiteral("com.victorfu.snaptray.debug.plist"),
            base + QStringLiteral("com.victor-fu.SnapTray-Debug.plist")
        };
    }
    return {
        base + QStringLiteral("com.victorfu.snaptray.plist"),
        base + QStringLiteral("com.victor-fu.SnapTray.plist")
    };
}
#endif

#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
inline void mergeSettingsIfMissing(QSettings& target, QSettings& source)
{
    if (isSameSettingsStore(target, source)) {
        return;
    }

    const QString migrationKey = QString::fromLatin1(kSettingsMigrationVersionKey);
    const QStringList sourceKeys = source.allKeys();
    for (const QString& key : sourceKeys) {
        if (key == migrationKey) {
            continue;
        }
        if (!target.contains(key)) {
            target.setValue(key, source.value(key));
        }
    }
}

inline void clearSettingsStoreIfSeparate(QSettings& target, QSettings& source)
{
    if (isSameSettingsStore(target, source)) {
        return;
    }
    if (source.status() != QSettings::NoError) {
        return;
    }

    source.clear();
    source.sync();
}

inline void migrateLegacySettingsIfNeeded()
{
    QSettings platformSettings = platformSettingsStore();
    const int migrationVersion = platformSettings.value(kSettingsMigrationVersionKey, 0).toInt();
    if (migrationVersion >= kSettingsMigrationVersion) {
        return;
    }

#if defined(Q_OS_WIN)
    const QStringList legacyPaths = windowsLegacySettingsPaths();
#else
    const QStringList legacyPaths = macLegacySettingsPaths();
#endif
    for (const QString& path : legacyPaths) {
        QSettings legacySettings(path, QSettings::NativeFormat);
        legacySettings.setFallbacksEnabled(false);
        mergeSettingsIfMissing(platformSettings, legacySettings);
    }

    QSettings organizationSettings(kOrganizationName, kApplicationName);
    organizationSettings.setFallbacksEnabled(false);
    mergeSettingsIfMissing(platformSettings, organizationSettings);

    platformSettings.setValue(kSettingsMigrationVersionKey, kSettingsMigrationVersion);
    platformSettings.sync();
    if (platformSettings.status() != QSettings::NoError) {
        return;
    }

#if defined(Q_OS_WIN)
    for (const QString& path : windowsLegacySettingsPaths()) {
        QSettings legacySettings(path, QSettings::NativeFormat);
        legacySettings.setFallbacksEnabled(false);
        clearSettingsStoreIfSeparate(platformSettings, legacySettings);
    }
#else
    for (const QString& path : macLegacySettingsPaths()) {
        QSettings legacySettings(path, QSettings::NativeFormat);
        legacySettings.setFallbacksEnabled(false);
        clearSettingsStoreIfSeparate(platformSettings, legacySettings);
    }
#endif

    QSettings legacyOrganizationSettings(kOrganizationName, kApplicationName);
    legacyOrganizationSettings.setFallbacksEnabled(false);
    clearSettingsStoreIfSeparate(platformSettings, legacyOrganizationSettings);
}

inline void ensureSettingsMigration()
{
    static std::once_flag migrationOnce;
    std::call_once(migrationOnce, [] {
        migrateLegacySettingsIfNeeded();
    });
}
#endif

inline QSettings getSettings()
{
#if defined(Q_OS_WIN)
    ensureSettingsMigration();
    return platformSettingsStore();
#elif defined(Q_OS_MACOS)
    ensureSettingsMigration();
    return platformSettingsStore();
#else
    return QSettings(kOrganizationName, kApplicationName);
#endif
}

} // namespace SnapTray
