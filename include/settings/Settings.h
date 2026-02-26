#pragma once

#include <QDir>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStringList>
#include <mutex>
#if defined(Q_OS_WIN)
#include <windows.h>
#endif
#include "version.h"

namespace SnapTray {

inline constexpr const char* kOrganizationName = "SnapTray";
inline constexpr const char* kLegacyOrganizationName = "Victor Fu";
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
inline constexpr const char* kSettingsCleanupVersionKey = "__meta/settingsCleanupVersion";
inline constexpr int kSettingsCleanupVersion = 6;

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
inline QString windowsReleaseSettingsPath()
{
    return QStringLiteral("HKEY_CURRENT_USER\\Software\\SnapTray");
}

inline QString windowsDebugSettingsPath()
{
    return QStringLiteral("HKEY_CURRENT_USER\\Software\\SnapTray-Debug");
}

inline QSettings platformSettingsStore()
{
    return QSettings(windowsSettingsPath(), QSettings::NativeFormat);
}

inline QStringList windowsLegacySettingsPaths()
{
    if (isDebugSettingsNamespace()) {
        return {
            QStringLiteral("HKEY_CURRENT_USER\\Software\\Victor Fu\\SnapTray-Debug"),
            QStringLiteral("HKEY_CURRENT_USER\\Software\\Victor Fu\\SnapTray"),
            QStringLiteral("HKEY_CURRENT_USER\\Software\\SnapTray-Debug"),
            QStringLiteral("HKEY_CURRENT_USER\\Software\\SnapTray\\Debug")
        };
    }
    return {
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Victor Fu\\SnapTray"),
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Victor Fu\\SnapTray-Debug"),
        QStringLiteral("HKEY_CURRENT_USER\\Software\\SnapTray-Debug"),
        QStringLiteral("HKEY_CURRENT_USER\\Software\\SnapTray\\Debug")
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
            base + QStringLiteral("com.victor-fu.SnapTray-Debug.plist"),
            base + QStringLiteral("com.victorfu.snaptray.plist"),
            base + QStringLiteral("com.victor-fu.SnapTray.plist")
        };
    }
    return {
        base + QStringLiteral("com.victorfu.snaptray.plist"),
        base + QStringLiteral("com.victor-fu.SnapTray.plist"),
        base + QStringLiteral("com.victorfu.snaptray.debug.plist"),
        base + QStringLiteral("com.victor-fu.SnapTray-Debug.plist")
    };
}
#endif

#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
inline QSettings legacyOrganizationSettingsStore()
{
    return QSettings(QString::fromLatin1(kLegacyOrganizationName),
                     QString::fromLatin1(kApplicationName));
}

inline void logSettingsMigrationInfo(const QString& message)
{
    Q_UNUSED(message);
}

inline void logSettingsMigrationWarning(const QString& message)
{
    qWarning().noquote() << QStringLiteral("[SettingsMigration] %1").arg(message);
}

inline bool shouldPreserveWindowsNamespaceStore(const QString& settingsPath)
{
#if defined(Q_OS_WIN)
    const QString normalizedPath = normalizeSettingsLocation(settingsPath);
    const QString normalizedReleasePath = normalizeSettingsLocation(windowsReleaseSettingsPath());
    const QString normalizedDebugPath = normalizeSettingsLocation(windowsDebugSettingsPath());
    return normalizedPath == normalizedReleasePath
        || normalizedPath == normalizedDebugPath;
#else
    Q_UNUSED(settingsPath);
    return false;
#endif
}

inline bool mergeSettingsIfMissing(QSettings& target, QSettings& source, int& migratedKeyCount)
{
    migratedKeyCount = 0;
    if (isSameSettingsStore(target, source)) {
        return true;
    }
    const QString migrationKey = QString::fromLatin1(kSettingsMigrationVersionKey);
    const QStringList sourceKeys = source.allKeys();
    if (source.status() != QSettings::NoError) {
        logSettingsMigrationWarning(
            QStringLiteral("Failed to read legacy settings store: %1").arg(source.fileName()));
        return false;
    }

    for (const QString& key : sourceKeys) {
        if (key == migrationKey) {
            continue;
        }
        if (!target.contains(key)) {
            target.setValue(key, source.value(key));
            ++migratedKeyCount;
        }
    }
    if (source.status() != QSettings::NoError) {
        logSettingsMigrationWarning(
            QStringLiteral("Failed while reading legacy settings store: %1").arg(source.fileName()));
        return false;
    }
    if (!sourceKeys.isEmpty()) {
        logSettingsMigrationInfo(
            QStringLiteral("Scanned legacy store %1 (keys=%2, migrated=%3)")
                .arg(source.fileName())
                .arg(sourceKeys.size())
                .arg(migratedKeyCount));
    }
    return true;
}

inline bool clearSettingsStoreIfSeparate(QSettings& target, QSettings& source)
{
    const QString sourcePath = source.fileName();
    if (isSameSettingsStore(target, source)) {
        return true;
    }
#if defined(Q_OS_WIN)
    if (shouldPreserveWindowsNamespaceStore(sourcePath)) {
        logSettingsMigrationInfo(
            QStringLiteral("Skipping cleanup for active namespace store: %1").arg(sourcePath));
        return true;
    }
#endif
    if (source.status() != QSettings::NoError) {
        logSettingsMigrationWarning(
            QStringLiteral("Skipping cleanup due to source store error: %1").arg(sourcePath));
        return false;
    }

    const QStringList keysBeforeCleanup = source.allKeys();
    source.clear();
    source.sync();
    if (source.status() != QSettings::NoError) {
        logSettingsMigrationWarning(
            QStringLiteral("Failed to clear legacy settings store: %1").arg(sourcePath));
        return false;
    }
    logSettingsMigrationInfo(
        QStringLiteral("Cleared legacy settings store %1 (previous keys=%2)")
            .arg(sourcePath)
            .arg(keysBeforeCleanup.size()));

#if defined(Q_OS_MACOS)
    const QString legacyFile = sourcePath;
    if (legacyFile.endsWith(QStringLiteral(".plist"), Qt::CaseInsensitive)) {
        QSettings verifyStore(legacyFile, QSettings::NativeFormat);
        verifyStore.setFallbacksEnabled(false);
        if (verifyStore.allKeys().isEmpty()) {
            if (!QFile::remove(legacyFile)) {
                if (QFileInfo::exists(legacyFile)) {
                    logSettingsMigrationWarning(
                        QStringLiteral("Failed to delete legacy plist: %1").arg(legacyFile));
                    return false;
                }
            } else {
                logSettingsMigrationInfo(
                    QStringLiteral("Deleted legacy plist: %1").arg(legacyFile));
            }
        } else {
            logSettingsMigrationWarning(
                QStringLiteral("Legacy plist still has keys after cleanup: %1").arg(legacyFile));
            return false;
        }
    }
#endif

    return true;
}

#if defined(Q_OS_WIN)
inline bool removeWindowsLegacyVendorKey()
{
    const QString vendorPath = QStringLiteral("HKEY_CURRENT_USER\\Software\\Victor Fu");
    LONG deleteTreeResult = RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Victor Fu");
    if (deleteTreeResult != ERROR_SUCCESS
        && deleteTreeResult != ERROR_FILE_NOT_FOUND
        && deleteTreeResult != ERROR_PATH_NOT_FOUND) {
        logSettingsMigrationWarning(
            QStringLiteral("Failed to delete legacy vendor tree: %1 (error=%2)")
                .arg(vendorPath)
                .arg(deleteTreeResult));
        return false;
    }

    LONG deleteKeyResult = RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\Victor Fu");
    if (deleteKeyResult != ERROR_SUCCESS
        && deleteKeyResult != ERROR_FILE_NOT_FOUND
        && deleteKeyResult != ERROR_PATH_NOT_FOUND) {
        logSettingsMigrationWarning(
            QStringLiteral("Failed to delete legacy vendor key: %1 (error=%2)")
                .arg(vendorPath)
                .arg(deleteKeyResult));
        return false;
    }

    HKEY verifyKey = nullptr;
    const LONG openResult = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Victor Fu", 0, KEY_READ, &verifyKey);
    if (openResult != ERROR_FILE_NOT_FOUND && openResult != ERROR_PATH_NOT_FOUND) {
        if (openResult == ERROR_SUCCESS && verifyKey != nullptr) {
            RegCloseKey(verifyKey);
        }
        logSettingsMigrationWarning(
            QStringLiteral("Legacy vendor key still exists after forced removal: %1 (error=%2)")
                .arg(vendorPath)
                .arg(openResult));
        return false;
    }

    logSettingsMigrationInfo(QStringLiteral("Removed legacy vendor key tree: %1").arg(vendorPath));
    return true;
}
#endif

inline void migrateLegacySettingsIfNeeded()
{
    QSettings platformSettings = platformSettingsStore();
    const int migrationVersion = platformSettings.value(kSettingsMigrationVersionKey, 0).toInt();
    const int cleanupVersion = platformSettings.value(kSettingsCleanupVersionKey, 0).toInt();
    const bool needsMigration = migrationVersion < kSettingsMigrationVersion;
    const bool needsCleanup = cleanupVersion < kSettingsCleanupVersion;
    if (!needsMigration && !needsCleanup) {
        logSettingsMigrationInfo(
            QStringLiteral("Migration already up-to-date for %1 (migrationVersion=%2, cleanupVersion=%3)")
                .arg(platformSettings.fileName())
                .arg(migrationVersion)
                .arg(cleanupVersion));
        return;
    }

#if defined(Q_OS_WIN)
    const QStringList legacyPaths = windowsLegacySettingsPaths();
#else
    const QStringList legacyPaths = macLegacySettingsPaths();
#endif
    logSettingsMigrationInfo(
        QStringLiteral("Starting migration for target store %1 (needsMigration=%2, needsCleanup=%3)")
            .arg(platformSettings.fileName())
            .arg(needsMigration ? QStringLiteral("true") : QStringLiteral("false"))
            .arg(needsCleanup ? QStringLiteral("true") : QStringLiteral("false")));

    if (needsMigration) {
        int totalMigratedKeys = 0;
        bool migrationSucceeded = true;
        for (const QString& path : legacyPaths) {
            QSettings legacySettings(path, QSettings::NativeFormat);
            legacySettings.setFallbacksEnabled(false);
            int migratedFromStore = 0;
            const bool merged = mergeSettingsIfMissing(platformSettings, legacySettings, migratedFromStore);
            totalMigratedKeys += migratedFromStore;
            migrationSucceeded = merged && migrationSucceeded;
        }

        QSettings legacyOrganizationSettings = legacyOrganizationSettingsStore();
        legacyOrganizationSettings.setFallbacksEnabled(false);
        int migratedFromOrganizationStore = 0;
        const bool mergedLegacyOrganizationStore = mergeSettingsIfMissing(
            platformSettings, legacyOrganizationSettings, migratedFromOrganizationStore);
        totalMigratedKeys += migratedFromOrganizationStore;
        migrationSucceeded = mergedLegacyOrganizationStore && migrationSucceeded;

        logSettingsMigrationInfo(
            QStringLiteral("Migration copied %1 keys in total").arg(totalMigratedKeys));
        if (migrationSucceeded) {
            platformSettings.setValue(kSettingsMigrationVersionKey, kSettingsMigrationVersion);
        } else {
            logSettingsMigrationWarning(
                QStringLiteral("Migration finished with source read errors; will retry on next launch"));
        }
    }
    platformSettings.sync();
    if (platformSettings.status() != QSettings::NoError) {
        logSettingsMigrationWarning(
            QStringLiteral("Aborting cleanup because target store sync failed: %1")
                .arg(platformSettings.fileName()));
        return;
    }

    if (needsCleanup) {
        bool cleanupSucceeded = true;
#if defined(Q_OS_WIN)
        for (const QString& path : windowsLegacySettingsPaths()) {
            QSettings legacySettings(path, QSettings::NativeFormat);
            legacySettings.setFallbacksEnabled(false);
            cleanupSucceeded
                = clearSettingsStoreIfSeparate(platformSettings, legacySettings) && cleanupSucceeded;
        }
#else
        for (const QString& path : macLegacySettingsPaths()) {
            QSettings legacySettings(path, QSettings::NativeFormat);
            legacySettings.setFallbacksEnabled(false);
            cleanupSucceeded
                = clearSettingsStoreIfSeparate(platformSettings, legacySettings) && cleanupSucceeded;
        }
#endif

        QSettings legacyOrganizationSettings = legacyOrganizationSettingsStore();
        legacyOrganizationSettings.setFallbacksEnabled(false);
        cleanupSucceeded
            = clearSettingsStoreIfSeparate(platformSettings, legacyOrganizationSettings) && cleanupSucceeded;
#if defined(Q_OS_WIN)
        cleanupSucceeded = removeWindowsLegacyVendorKey() && cleanupSucceeded;
#endif

        if (cleanupSucceeded) {
            platformSettings.setValue(kSettingsCleanupVersionKey, kSettingsCleanupVersion);
            platformSettings.sync();
            if (platformSettings.status() == QSettings::NoError) {
                logSettingsMigrationInfo(QStringLiteral("Legacy cleanup completed successfully"));
            } else {
                logSettingsMigrationWarning(
                    QStringLiteral("Failed to persist cleanup completion marker"));
            }
        } else {
            logSettingsMigrationWarning(
                QStringLiteral("Legacy cleanup finished with errors; will retry on next launch"));
        }
    } else {
        platformSettings.setValue(kSettingsCleanupVersionKey, kSettingsCleanupVersion);
        platformSettings.sync();
        if (platformSettings.status() != QSettings::NoError) {
            logSettingsMigrationWarning(
                QStringLiteral("Failed to persist cleanup version marker"));
        }
    }
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
