#include <QtTest>

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

private:
    void clearFileSettings();
};

void tst_FileSettingsManager::init()
{
    clearFileSettings();
}

void tst_FileSettingsManager::cleanup()
{
    clearFileSettings();
}

void tst_FileSettingsManager::clearFileSettings()
{
    auto settings = SnapTray::getSettings();
    settings.remove("files/filenameTemplate");
    settings.remove("files/filenamePrefix");
    settings.remove("files/dateFormat");
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

QTEST_MAIN(tst_FileSettingsManager)
#include "tst_FileSettingsManager.moc"
