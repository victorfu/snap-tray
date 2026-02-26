#include <QtTest/QtTest>
#include <QMap>
#include <QUuid>
#include <QVariant>

#include "settings/Settings.h"
#include "version.h"

namespace {

constexpr auto kProbeKey = "tests/settingsStorageLocation/probe";
constexpr auto kLegacyProbeValue = "legacy";
constexpr auto kLegacyOrganizationName = "Victor Fu";

bool isDebugSettingsNamespace()
{
    return QString::fromLatin1(SNAPTRAY_APP_BUNDLE_ID).endsWith(QStringLiteral(".debug"));
}

QString normalizeSettingsLocation(QString location)
{
    location.replace('\\', '/');
    while (location.endsWith('/')) {
        location.chop(1);
    }
    return location.toLower();
}

QSettings legacySettingsStore()
{
    return QSettings(QString::fromLatin1(kLegacyOrganizationName),
                     QString::fromLatin1(SnapTray::kApplicationName));
}

QStringList legacyMigrationSources()
{
#if defined(Q_OS_WIN)
    return SnapTray::windowsLegacySettingsPaths();
#elif defined(Q_OS_MACOS)
    return SnapTray::macLegacySettingsPaths();
#else
    return {};
#endif
}

#if defined(Q_OS_WIN)
QSettings platformSettingsStore()
{
    return QSettings(SnapTray::windowsSettingsPath(), QSettings::NativeFormat);
}
#elif defined(Q_OS_MACOS)
QSettings platformSettingsStore()
{
    return QSettings(SnapTray::macSettingsPath(), QSettings::NativeFormat);
}
#endif

QSettings settingsStoreUnderTest()
{
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    return platformSettingsStore();
#else
    return SnapTray::getSettings();
#endif
}

class ScopedSettingsBackup
{
public:
    explicit ScopedSettingsBackup(QSettings& settings)
        : m_settings(settings)
    {
        const QStringList keys = m_settings.allKeys();
        for (const QString& key : keys) {
            m_values.insert(key, m_settings.value(key));
        }
    }

    ~ScopedSettingsBackup()
    {
        if (m_settings.status() != QSettings::NoError) {
            return;
        }

        m_settings.clear();
        for (auto it = m_values.cbegin(); it != m_values.cend(); ++it) {
            m_settings.setValue(it.key(), it.value());
        }
        m_settings.sync();
    }

private:
    QSettings& m_settings;
    QMap<QString, QVariant> m_values;
};

} // namespace

class tst_SettingsStorageLocation : public QObject
{
    Q_OBJECT

private slots:
    void testLegacySettingsMigration();
    void testLegacySourceCoverage();
    void testStorageLocation();
    void testRoundtripSetReadRemove();
};

void tst_SettingsStorageLocation::testLegacySettingsMigration()
{
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    auto legacy = legacySettingsStore();
    auto platform = platformSettingsStore();
    if (legacy.status() != QSettings::NoError || platform.status() != QSettings::NoError) {
        QSKIP("Settings stores are not writable in this environment");
    }

    ScopedSettingsBackup legacyBackup(legacy);
    ScopedSettingsBackup platformBackup(platform);

    legacy.clear();
    platform.clear();
    legacy.sync();
    platform.sync();
    if (legacy.status() != QSettings::NoError || platform.status() != QSettings::NoError) {
        QSKIP("Failed to prepare isolated settings stores");
    }

    legacy.setValue(kProbeKey, QString::fromLatin1(kLegacyProbeValue));
    legacy.sync();
    QVERIFY(legacy.contains(kProbeKey));

    platform.remove(QString::fromLatin1(SnapTray::kSettingsMigrationVersionKey));
    platform.sync();

    SnapTray::migrateLegacySettingsIfNeeded();

    auto settings = platformSettingsStore();
    QCOMPARE(settings.value(kProbeKey).toString(), QString::fromLatin1(kLegacyProbeValue));

    legacy.sync();
    if (settings.status() == QSettings::NoError) {
        QVERIFY(!legacy.contains(kProbeKey));
    } else {
        QSKIP("Platform settings store is not writable; cleanup assertion skipped");
    }
#else
    QSKIP("Legacy-to-platform settings migration applies only to Windows/macOS");
#endif
}

void tst_SettingsStorageLocation::testLegacySourceCoverage()
{
#if defined(Q_OS_MACOS)
    const QStringList sources = legacyMigrationSources();
    QCOMPARE(sources.size(), 4);
    QVERIFY(sources.contains(QDir::homePath() + QStringLiteral("/Library/Preferences/com.victor-fu.SnapTray.plist")));
    QVERIFY(sources.contains(QDir::homePath() + QStringLiteral("/Library/Preferences/com.victor-fu.SnapTray-Debug.plist")));
    QVERIFY(sources.contains(QDir::homePath() + QStringLiteral("/Library/Preferences/com.victorfu.snaptray.plist")));
    QVERIFY(sources.contains(QDir::homePath() + QStringLiteral("/Library/Preferences/com.victorfu.snaptray.debug.plist")));
#elif defined(Q_OS_WIN)
    const QStringList sources = legacyMigrationSources();
    if (isDebugSettingsNamespace()) {
        QCOMPARE(sources.size(), 4);
        QVERIFY(sources.contains(QStringLiteral("HKEY_CURRENT_USER\\Software\\Victor Fu\\SnapTray-Debug")));
        QVERIFY(sources.contains(QStringLiteral("HKEY_CURRENT_USER\\Software\\Victor Fu\\SnapTray")));
        QVERIFY(sources.contains(QStringLiteral("HKEY_CURRENT_USER\\Software\\SnapTray-Debug")));
        QVERIFY(sources.contains(QStringLiteral("HKEY_CURRENT_USER\\Software\\SnapTray\\Debug")));
    } else {
        QCOMPARE(sources.size(), 4);
        QVERIFY(sources.contains(QStringLiteral("HKEY_CURRENT_USER\\Software\\Victor Fu\\SnapTray")));
        QVERIFY(sources.contains(QStringLiteral("HKEY_CURRENT_USER\\Software\\Victor Fu\\SnapTray-Debug")));
        QVERIFY(sources.contains(QStringLiteral("HKEY_CURRENT_USER\\Software\\SnapTray-Debug")));
        QVERIFY(sources.contains(QStringLiteral("HKEY_CURRENT_USER\\Software\\SnapTray\\Debug")));
    }
#else
    QSKIP("Legacy source coverage applies only to Windows/macOS");
#endif
}

void tst_SettingsStorageLocation::testStorageLocation()
{
    auto settings = settingsStoreUnderTest();
    const QString actual = normalizeSettingsLocation(settings.fileName());

#if defined(Q_OS_MACOS)
    const QString expectedSuffix = isDebugSettingsNamespace()
        ? QStringLiteral("/library/preferences/cc.snaptray.debug.plist")
        : QStringLiteral("/library/preferences/cc.snaptray.plist");
    QVERIFY2(actual.endsWith(expectedSuffix),
             qPrintable(QStringLiteral("Expected suffix: %1, actual: %2")
                            .arg(expectedSuffix, actual)));
#elif defined(Q_OS_WIN)
    const QString expected = isDebugSettingsNamespace()
        ? QStringLiteral("hkey_current_user/software/snaptray-debug")
        : QStringLiteral("hkey_current_user/software/snaptray");
    QCOMPARE(actual, expected);
#else
    QCOMPARE(settings.organizationName(), QString::fromLatin1(SnapTray::kOrganizationName));
    QCOMPARE(settings.applicationName(), QString::fromLatin1(SnapTray::kApplicationName));
    QVERIFY(!actual.isEmpty());
#endif
}

void tst_SettingsStorageLocation::testRoundtripSetReadRemove()
{
    auto settings = settingsStoreUnderTest();
    const QString probeKey = QStringLiteral("tests/settingsStorageLocation/probe/%1")
                                 .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    settings.setValue(probeKey, QStringLiteral("ok"));
    settings.sync();
    QCOMPARE(settings.value(probeKey).toString(), QStringLiteral("ok"));

    settings.remove(probeKey);
    settings.sync();
    QVERIFY(!settings.contains(probeKey));
}

QTEST_MAIN(tst_SettingsStorageLocation)
#include "tst_SettingsStorageLocation.moc"
