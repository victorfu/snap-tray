#include <QtTest>

#include <QTemporaryDir>
#include <QTemporaryFile>

#include <utility>

#include "settings/FileSettingsManager.h"
#include "settings/Settings.h"

class tst_FileSettingsManager : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testLoadFilenameTemplate_MigratesLegacy();
    void testLoadFilenameTemplate_UsesStoredValue();
    void testSaveLoadFilenameTemplate_Roundtrip();
    void testLoadFilenameTemplate_DefaultDateFormatWhenMissing();

    void testUseLastScreenshotSaveLocation_DefaultAndRoundtrip();
    void testResolveManualScreenshotSaveDirectory_IgnoresRememberedWhenDisabled();
    void testResolveManualScreenshotSaveDirectory_UsesExistingRememberedDirectory();
    void testResolveManualScreenshotSaveDirectory_PreservesTrailingWhitespace();
    void testResolveManualScreenshotSaveDirectory_FallsBackForInvalidRememberedDirectory_data();
    void testResolveManualScreenshotSaveDirectory_FallsBackForInvalidRememberedDirectory();
    void testRememberManualScreenshotSaveDirectory_StoresAbsoluteParent();
    void testRememberManualScreenshotSaveDirectory_IgnoresEmptyAndInvalidParent();
    void testDisablingLastScreenshotSaveLocation_PreservesRememberedDirectory();

private:
    struct SavedSetting
    {
        QString key;
        bool existed = false;
        QVariant value;
    };

    void snapshotFileSettings();
    void restoreFileSettings();
    void clearFileSettings();

    QList<SavedSetting> m_savedSettings;
};

void tst_FileSettingsManager::init()
{
    snapshotFileSettings();
    clearFileSettings();
}

void tst_FileSettingsManager::cleanup()
{
    restoreFileSettings();
}

void tst_FileSettingsManager::snapshotFileSettings()
{
    static const QStringList keys = {
        QStringLiteral("files/filenameTemplate"),
        QStringLiteral("files/filenamePrefix"),
        QStringLiteral("files/dateFormat"),
        QStringLiteral("files/screenshotPath"),
        QStringLiteral("files/useLastScreenshotSaveLocation"),
        QStringLiteral("files/lastScreenshotSaveDirectory"),
    };

    auto settings = SnapTray::getSettings();
    m_savedSettings.clear();
    m_savedSettings.reserve(keys.size());
    for (const QString& key : keys) {
        m_savedSettings.append({key, settings.contains(key), settings.value(key)});
    }
}

void tst_FileSettingsManager::restoreFileSettings()
{
    auto settings = SnapTray::getSettings();
    for (const SavedSetting& savedSetting : std::as_const(m_savedSettings)) {
        if (savedSetting.existed) {
            settings.setValue(savedSetting.key, savedSetting.value);
        } else {
            settings.remove(savedSetting.key);
        }
    }
    settings.sync();
    m_savedSettings.clear();
}

void tst_FileSettingsManager::clearFileSettings()
{
    auto settings = SnapTray::getSettings();
    settings.remove("files/filenameTemplate");
    settings.remove("files/filenamePrefix");
    settings.remove("files/dateFormat");
    settings.remove("files/screenshotPath");
    settings.remove("files/useLastScreenshotSaveLocation");
    settings.remove("files/lastScreenshotSaveDirectory");
    settings.sync();
}

void tst_FileSettingsManager::testLoadFilenameTemplate_MigratesLegacy()
{
    auto settings = SnapTray::getSettings();
    settings.setValue("files/filenamePrefix", "ProjectX");
    settings.setValue("files/dateFormat", "yyyy-MM-dd_HH-mm-ss");
    settings.remove("files/filenameTemplate");
    settings.sync();

    const QString templ = FileSettingsManager::instance().loadFilenameTemplate();
    QCOMPARE(templ, QString("{prefix}_{type}_{yyyy-MM-dd_HH-mm-ss}.{ext}"));
    QCOMPARE(settings.value("files/filenameTemplate").toString(), templ);
}

void tst_FileSettingsManager::testLoadFilenameTemplate_UsesStoredValue()
{
    auto settings = SnapTray::getSettings();
    settings.setValue("files/filenameTemplate", "{type}_{yyyyMMdd}_{ext}");
    settings.setValue("files/dateFormat", "yyMMdd");
    settings.sync();

    const QString templ = FileSettingsManager::instance().loadFilenameTemplate();
    QCOMPARE(templ, QString("{type}_{yyyyMMdd}_{ext}"));
}

void tst_FileSettingsManager::testSaveLoadFilenameTemplate_Roundtrip()
{
    auto& manager = FileSettingsManager::instance();
    manager.saveFilenameTemplate("{prefix}_{type}_{w}x{h}.{ext}");
    QCOMPARE(manager.loadFilenameTemplate(), QString("{prefix}_{type}_{w}x{h}.{ext}"));
}

void tst_FileSettingsManager::testLoadFilenameTemplate_DefaultDateFormatWhenMissing()
{
    auto settings = SnapTray::getSettings();
    settings.setValue("files/filenamePrefix", "Legacy");
    settings.setValue("files/dateFormat", "");
    settings.remove("files/filenameTemplate");
    settings.sync();

    const QString templ = FileSettingsManager::instance().loadFilenameTemplate();
    QCOMPARE(templ, QString("{prefix}_{type}_{yyyyMMdd_HHmmss}.{ext}"));
}

void tst_FileSettingsManager::testUseLastScreenshotSaveLocation_DefaultAndRoundtrip()
{
    auto& manager = FileSettingsManager::instance();
    QCOMPARE(manager.loadUseLastScreenshotSaveLocation(), false);

    manager.saveUseLastScreenshotSaveLocation(true);
    QCOMPARE(manager.loadUseLastScreenshotSaveLocation(), true);

    manager.saveUseLastScreenshotSaveLocation(false);
    QCOMPARE(manager.loadUseLastScreenshotSaveLocation(), false);
}

void tst_FileSettingsManager::testResolveManualScreenshotSaveDirectory_IgnoresRememberedWhenDisabled()
{
    QTemporaryDir screenshotDirectory;
    QTemporaryDir rememberedDirectory;
    QVERIFY(screenshotDirectory.isValid());
    QVERIFY(rememberedDirectory.isValid());

    auto settings = SnapTray::getSettings();
    settings.setValue("files/screenshotPath", screenshotDirectory.path());
    settings.setValue("files/lastScreenshotSaveDirectory", rememberedDirectory.path());

    QCOMPARE(FileSettingsManager::instance().resolveManualScreenshotSaveDirectory(false),
             screenshotDirectory.path());
}

void tst_FileSettingsManager::testResolveManualScreenshotSaveDirectory_UsesExistingRememberedDirectory()
{
    QTemporaryDir screenshotDirectory;
    QTemporaryDir rememberedDirectory;
    QVERIFY(screenshotDirectory.isValid());
    QVERIFY(rememberedDirectory.isValid());

    auto settings = SnapTray::getSettings();
    settings.setValue("files/screenshotPath", screenshotDirectory.path());
    settings.setValue("files/lastScreenshotSaveDirectory", rememberedDirectory.path());

    QCOMPARE(FileSettingsManager::instance().resolveManualScreenshotSaveDirectory(true),
             QDir::cleanPath(QFileInfo(rememberedDirectory.path()).absoluteFilePath()));
}

void tst_FileSettingsManager::testResolveManualScreenshotSaveDirectory_PreservesTrailingWhitespace()
{
#if defined(Q_OS_WIN)
    QSKIP("Windows does not support directory names ending in whitespace.");
#else
    QTemporaryDir baseDirectory;
    QVERIFY(baseDirectory.isValid());

    const QString rememberedDirectory =
        QDir(baseDirectory.path()).filePath(QStringLiteral("remembered "));
    QVERIFY(QDir().mkpath(rememberedDirectory));
    QVERIFY(QFileInfo(rememberedDirectory).isDir());

    auto& manager = FileSettingsManager::instance();
    manager.rememberManualScreenshotSaveDirectory(
        QDir(rememberedDirectory).filePath(QStringLiteral("capture.png")));

    QCOMPARE(manager.resolveManualScreenshotSaveDirectory(true),
             QDir::cleanPath(QFileInfo(rememberedDirectory).absoluteFilePath()));
#endif
}

void tst_FileSettingsManager::testResolveManualScreenshotSaveDirectory_FallsBackForInvalidRememberedDirectory_data()
{
    QTest::addColumn<QString>("rememberedPathKind");

    QTest::newRow("empty") << QStringLiteral("empty");
    QTest::newRow("regular-file") << QStringLiteral("regular-file");
    QTest::newRow("missing-directory") << QStringLiteral("missing-directory");
}

void tst_FileSettingsManager::testResolveManualScreenshotSaveDirectory_FallsBackForInvalidRememberedDirectory()
{
    QFETCH(QString, rememberedPathKind);

    QTemporaryDir baseDirectory;
    QTemporaryDir screenshotDirectory;
    QVERIFY(baseDirectory.isValid());
    QVERIFY(screenshotDirectory.isValid());

    QString rememberedPath;
    QTemporaryFile regularFile(baseDirectory.filePath(QStringLiteral("remembered-XXXXXX")));
    if (rememberedPathKind == QStringLiteral("regular-file")) {
        QVERIFY(regularFile.open());
        rememberedPath = regularFile.fileName();
    } else if (rememberedPathKind == QStringLiteral("missing-directory")) {
        rememberedPath = baseDirectory.filePath(QStringLiteral("does-not-exist"));
    }

    auto settings = SnapTray::getSettings();
    settings.setValue("files/screenshotPath", screenshotDirectory.path());
    settings.setValue("files/lastScreenshotSaveDirectory", rememberedPath);

    QCOMPARE(FileSettingsManager::instance().resolveManualScreenshotSaveDirectory(true),
             screenshotDirectory.path());
}

void tst_FileSettingsManager::testRememberManualScreenshotSaveDirectory_StoresAbsoluteParent()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString relativeDirectory = QDir::current().relativeFilePath(directory.path());
    const QString savedFilePath = QDir(relativeDirectory).filePath(QStringLiteral("capture.png"));
    FileSettingsManager::instance().rememberManualScreenshotSaveDirectory(savedFilePath);

    auto settings = SnapTray::getSettings();
    QCOMPARE(settings.value("files/lastScreenshotSaveDirectory").toString(),
             QDir::cleanPath(QFileInfo(directory.path()).absoluteFilePath()));
}

void tst_FileSettingsManager::testRememberManualScreenshotSaveDirectory_IgnoresEmptyAndInvalidParent()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    auto settings = SnapTray::getSettings();
    const QString originalDirectory = directory.path();
    settings.setValue("files/lastScreenshotSaveDirectory", originalDirectory);

    auto& manager = FileSettingsManager::instance();
    manager.rememberManualScreenshotSaveDirectory(QString());
    QCOMPARE(settings.value("files/lastScreenshotSaveDirectory").toString(), originalDirectory);

    const QString missingParentPath =
        directory.filePath(QStringLiteral("missing/parent/capture.png"));
    manager.rememberManualScreenshotSaveDirectory(missingParentPath);
    QCOMPARE(settings.value("files/lastScreenshotSaveDirectory").toString(), originalDirectory);
}

void tst_FileSettingsManager::testDisablingLastScreenshotSaveLocation_PreservesRememberedDirectory()
{
    QTemporaryDir screenshotDirectory;
    QTemporaryDir rememberedDirectory;
    QVERIFY(screenshotDirectory.isValid());
    QVERIFY(rememberedDirectory.isValid());

    auto& manager = FileSettingsManager::instance();
    manager.saveScreenshotPath(screenshotDirectory.path());
    manager.rememberManualScreenshotSaveDirectory(
        QDir(rememberedDirectory.path()).filePath(QStringLiteral("capture.png")));

    manager.saveUseLastScreenshotSaveLocation(false);
    QCOMPARE(manager.resolveManualScreenshotSaveDirectory(
                 manager.loadUseLastScreenshotSaveLocation()),
             screenshotDirectory.path());

    manager.saveUseLastScreenshotSaveLocation(true);
    QCOMPARE(manager.resolveManualScreenshotSaveDirectory(
                 manager.loadUseLastScreenshotSaveLocation()),
             QDir::cleanPath(QFileInfo(rememberedDirectory.path()).absoluteFilePath()));
}

QTEST_MAIN(tst_FileSettingsManager)
#include "tst_FileSettingsManager.moc"
