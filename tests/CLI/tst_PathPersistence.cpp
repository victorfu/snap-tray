#include <QtTest>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QUuid>

#include "platform/PathEnvUtils_win.h"

class tst_PathPersistence : public QObject
{
    Q_OBJECT
private slots:
    void persistenceAndReadback();
    void writeFailure();
    void registryRoundTrip();
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
    const QString key = "HKEY_CURRENT_USER\\Software\\SnapTrayTests\\PathPersistence-"
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
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

QTEST_GUILESS_MAIN(tst_PathPersistence)
#include "tst_PathPersistence.moc"
