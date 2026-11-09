#include <QtTest>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QUuid>
#ifdef Q_OS_WIN
#include <QScopeGuard>
#include <qt_windows.h>
#endif

#include "platform/PathEnvUtils_win.h"

class tst_PathPersistence : public QObject
{
    Q_OBJECT
private slots:
    void persistenceAndReadback();
    void writeFailure();
    void registryRoundTrip();
    void registryPreservesType_data();
    void registryPreservesType();
};

void tst_PathPersistence::persistenceAndReadback()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QSettings store(dir.filePath("environment.ini"), QSettings::IniFormat);
    store.setValue("Path", "existing;other");
    QVERIFY(PathEnvUtils::persistPathEntries(store, {{"existing", "other", "snaptray"}, true}));
    QSettings readback(store.fileName(), store.format());
    QCOMPARE(readback.value("Path").toString(), QString("existing;other;snaptray"));
    QVERIFY(PathEnvUtils::persistPathEntries(store, {{"existing", "other", "snaptray"}, false}));
    QVERIFY(!PathEnvUtils::persistPathEntries(store, {{"missing"}, false}));
    QVERIFY(PathEnvUtils::persistPathEntries(store, {{"existing", "other"}, true}));
}

void tst_PathPersistence::writeFailure()
{
    QTemporaryDir dir;
    QFile blocker(dir.filePath("file"));
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    QSettings store(blocker.fileName() + "/environment.ini", QSettings::IniFormat);
    QVERIFY(!PathEnvUtils::persistPathEntries(store, {{"snaptray"}, true}));
    QCOMPARE(store.status(), QSettings::AccessError);
}

void tst_PathPersistence::registryRoundTrip()
{
#ifdef Q_OS_WIN
    const QString subkey = "Software\\SnapTrayPathPersistence-"
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto cleanupKey = qScopeGuard([&subkey] {
        RegDeleteKeyW(HKEY_CURRENT_USER, reinterpret_cast<LPCWSTR>(subkey.utf16()));
    });
    const QString key = "HKEY_CURRENT_USER\\" + subkey;
    QSettings store(key, QSettings::NativeFormat);
    // Use a private fixture key; never touch the user's Environment registry key.
    QVERIFY(PathEnvUtils::persistPathEntries(store, {{"C:\\Existing", "C:\\SnapTray"}, true}));
    QVERIFY(PathEnvUtils::persistPathEntries(store, {{"C:\\Existing"}, true}));
    store.clear();
    store.sync();
#else
    QSKIP("Windows registry fixture only");
#endif
}

void tst_PathPersistence::registryPreservesType_data()
{
    QTest::addColumn<int>("type");
    QTest::newRow("expandable") << 2; // REG_EXPAND_SZ
    QTest::newRow("literal") << 1;    // REG_SZ
}

void tst_PathPersistence::registryPreservesType()
{
#ifdef Q_OS_WIN
    QFETCH(int, type);
    struct Fixture {
        QString subkey = "Software\\SnapTrayPathType-"
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
        HKEY key = nullptr;
        ~Fixture() {
            if (key) {
                RegCloseKey(key);
                RegDeleteKeyW(HKEY_CURRENT_USER, reinterpret_cast<LPCWSTR>(subkey.utf16()));
            }
        }
    } fixture;
    QCOMPARE(RegCreateKeyExW(HKEY_CURRENT_USER, reinterpret_cast<LPCWSTR>(fixture.subkey.utf16()),
                            0, nullptr, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, nullptr,
                            &fixture.key, nullptr), LONG(ERROR_SUCCESS));
    const QString original = QStringLiteral("%USERPROFILE%\\bin;C:\\Existing");
    QCOMPARE(RegSetValueExW(fixture.key, L"Path", 0, type,
                           reinterpret_cast<const BYTE*>(original.utf16()),
                           static_cast<DWORD>((original.size() + 1) * sizeof(wchar_t))), LONG(ERROR_SUCCESS));
    QSettings store("HKEY_CURRENT_USER\\" + fixture.subkey, QSettings::NativeFormat);
    const QString appDir = QStringLiteral("C:\\SnapTray");
    const auto installed = PathEnvUtils::installPathEntry(PathEnvUtils::splitPathEntries(original), appDir);
    QVERIFY(PathEnvUtils::persistPathEntries(store, installed));
    DWORD actualType = 0;
    wchar_t data[256] = {};
    DWORD bytes = sizeof(data);
    QCOMPARE(RegQueryValueExW(fixture.key, L"Path", nullptr, &actualType,
                             reinterpret_cast<BYTE*>(data), &bytes), LONG(ERROR_SUCCESS));
    QCOMPARE(actualType, DWORD(type));
    QCOMPARE(QString::fromWCharArray(data), original + ';' + appDir);

    QVERIFY(PathEnvUtils::persistPathEntries(store, {installed.entries, false}));
    QVERIFY(PathEnvUtils::persistPathEntries(store, PathEnvUtils::uninstallPathEntry(installed.entries, appDir)));
    bytes = sizeof(data);
    QCOMPARE(RegQueryValueExW(fixture.key, L"Path", nullptr, &actualType,
                             reinterpret_cast<BYTE*>(data), &bytes), LONG(ERROR_SUCCESS));
    QCOMPARE(actualType, DWORD(type));
    QCOMPARE(QString::fromWCharArray(data), original);
#else
    QSKIP("Windows registry fixture only");
#endif
}

QTEST_GUILESS_MAIN(tst_PathPersistence)
#include "tst_PathPersistence.moc"
