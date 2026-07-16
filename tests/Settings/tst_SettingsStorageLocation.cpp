#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QUuid>
#include <utility>

#include "settings/Settings.h"
#include "version.h"

namespace {

constexpr auto kProbeKey = "tests/settingsStorageLocation/probe";
constexpr auto kLegacyProbeValue = "legacy";
constexpr auto kSiblingProbeKey = "unrelated/probe";

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
    location = location.toLower();
#if defined(Q_OS_WIN)
    if (location.startsWith(QStringLiteral("/hkey_"))) {
        location.remove(0, 1);
    }
#endif
    return location;
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

#if defined(Q_OS_WIN)
class ScopedRegistryTree
{
public:
    explicit ScopedRegistryTree(QString subKey)
        : m_subKey(std::move(subKey))
    {
    }

    ~ScopedRegistryTree()
    {
        const std::wstring nativeSubKey = m_subKey.toStdWString();
        RegDeleteTreeW(HKEY_CURRENT_USER, nativeSubKey.c_str());
        RegDeleteKeyW(HKEY_CURRENT_USER, nativeSubKey.c_str());

        QString ancestor = m_subKey;
        while (ancestor.startsWith(
            QStringLiteral("Software\\SnapTrayTests\\"), Qt::CaseInsensitive)) {
            ancestor.truncate(ancestor.lastIndexOf(QLatin1Char('\\')));
            const std::wstring nativeAncestor = ancestor.toStdWString();
            const LONG result = RegDeleteKeyW(HKEY_CURRENT_USER, nativeAncestor.c_str());
            if (result != ERROR_SUCCESS && !SnapTray::isMissingWindowsRegistryPath(result)) {
                break;
            }
        }
    }

private:
    QString m_subKey;
};
#endif

} // namespace

class tst_SettingsStorageLocation : public QObject
{
    Q_OBJECT

private slots:
    void testLegacySettingsMigration();
    void testLegacySourceCoverage();
    void testWindowsLegacyCleanupPreservesSiblingKeys();
    void testWindowsNamespaceStoresAreNotCleanupTargets();
    void testStorageLocation();
    void testRoundtripSetReadRemove();
};

void tst_SettingsStorageLocation::testLegacySettingsMigration()
{
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    QTemporaryDir testDirectory;
    QVERIFY(testDirectory.isValid());

    QSettings legacy(
        testDirectory.filePath(QStringLiteral("legacy.ini")), QSettings::IniFormat);
    QSettings platform(
        testDirectory.filePath(QStringLiteral("platform.ini")), QSettings::IniFormat);

    legacy.setValue(kProbeKey, QString::fromLatin1(kLegacyProbeValue));
    legacy.sync();

    int migratedKeyCount = 0;
    QVERIFY(SnapTray::mergeSettingsIfMissing(platform, legacy, migratedKeyCount));
    QCOMPARE(migratedKeyCount, 1);
    platform.sync();
    QCOMPARE(platform.value(kProbeKey).toString(), QString::fromLatin1(kLegacyProbeValue));

    QVERIFY(SnapTray::clearSettingsStoreIfSeparate(platform, legacy));
    QVERIFY(!legacy.contains(kProbeKey));
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

void tst_SettingsStorageLocation::testWindowsLegacyCleanupPreservesSiblingKeys()
{
#if defined(Q_OS_WIN)
    const QString testId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString testRootSubKey
        = QStringLiteral("Software\\SnapTrayTests\\SettingsStorageLocationCleanup\\%1")
              .arg(testId);
    const QString vendorSubKey = testRootSubKey + QStringLiteral("\\Victor Fu");
    const QString vendorPath = QStringLiteral("HKEY_CURRENT_USER\\") + vendorSubKey;

    ScopedRegistryTree cleanup(testRootSubKey);
    QSettings releaseLegacy(
        vendorPath + QStringLiteral("\\SnapTray"), QSettings::NativeFormat);
    QSettings debugLegacy(
        vendorPath + QStringLiteral("\\SnapTray-Debug"), QSettings::NativeFormat);
    QSettings sibling(vendorPath + QStringLiteral("\\UnrelatedProduct"), QSettings::NativeFormat);
    releaseLegacy.setValue(kProbeKey, QStringLiteral("release"));
    debugLegacy.setValue(kProbeKey, QStringLiteral("debug"));
    sibling.setValue(kSiblingProbeKey, QStringLiteral("keep"));
    releaseLegacy.sync();
    debugLegacy.sync();
    sibling.sync();

    QVERIFY(SnapTray::removeWindowsLegacyApplicationKeys(HKEY_CURRENT_USER, vendorSubKey));

    QSettings releaseAfterCleanup(
        vendorPath + QStringLiteral("\\SnapTray"), QSettings::NativeFormat);
    QSettings debugAfterCleanup(
        vendorPath + QStringLiteral("\\SnapTray-Debug"), QSettings::NativeFormat);
    QSettings siblingAfterCleanup(
        vendorPath + QStringLiteral("\\UnrelatedProduct"), QSettings::NativeFormat);
    QVERIFY(!releaseAfterCleanup.contains(kProbeKey));
    QVERIFY(!debugAfterCleanup.contains(kProbeKey));
    QCOMPARE(siblingAfterCleanup.value(kSiblingProbeKey).toString(), QStringLiteral("keep"));
#else
    QSKIP("Windows registry cleanup applies only to Windows");
#endif
}

void tst_SettingsStorageLocation::testWindowsNamespaceStoresAreNotCleanupTargets()
{
#if defined(Q_OS_WIN)
    QVERIFY(SnapTray::shouldPreserveWindowsNamespaceStore(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\SnapTray")));
    QVERIFY(SnapTray::shouldPreserveWindowsNamespaceStore(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\SnapTray-Debug")));
    QVERIFY(!SnapTray::shouldPreserveWindowsNamespaceStore(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\SnapTray\\Debug")));
#else
    QSKIP("Windows namespace cleanup protection applies only to Windows");
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
    QTemporaryDir testDirectory;
    QVERIFY(testDirectory.isValid());
    QSettings settings(
        testDirectory.filePath(QStringLiteral("roundtrip.ini")), QSettings::IniFormat);
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
