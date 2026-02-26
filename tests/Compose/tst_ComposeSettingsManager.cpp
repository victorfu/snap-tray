#include <QtTest/QtTest>

#include "compose/ComposeSettingsManager.h"
#include "settings/Settings.h"

class tst_ComposeSettingsManager : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testDefaultValues();
    void testTemplateRoundtrip();
    void testCopyFormatRoundtrip();
    void testMetadataToggleRoundtrip();
    void testScreenshotMaxWidthClamp();

private:
    void clearComposeSettings();
};

void tst_ComposeSettingsManager::init()
{
    clearComposeSettings();
}

void tst_ComposeSettingsManager::cleanup()
{
    clearComposeSettings();
}

void tst_ComposeSettingsManager::clearComposeSettings()
{
    auto settings = SnapTray::getSettings();
    settings.remove(QStringLiteral("compose/defaultTemplate"));
    settings.remove(QStringLiteral("compose/defaultCopyFormat"));
    settings.remove(QStringLiteral("compose/autoIncludeMetadata"));
    settings.remove(QStringLiteral("compose/screenshotMaxWidth"));
    settings.sync();
}

void tst_ComposeSettingsManager::testDefaultValues()
{
    auto& manager = ComposeSettingsManager::instance();

    QCOMPARE(manager.defaultTemplate(), QStringLiteral("free"));
    QCOMPARE(manager.defaultCopyFormat(), QStringLiteral("html"));
    QCOMPARE(manager.autoIncludeMetadata(), true);
    QCOMPARE(manager.screenshotMaxWidth(), 600);
}

void tst_ComposeSettingsManager::testTemplateRoundtrip()
{
    auto& manager = ComposeSettingsManager::instance();

    manager.setDefaultTemplate(QStringLiteral("bug_report"));
    QCOMPARE(manager.defaultTemplate(), QStringLiteral("bug_report"));
}

void tst_ComposeSettingsManager::testCopyFormatRoundtrip()
{
    auto& manager = ComposeSettingsManager::instance();

    manager.setDefaultCopyFormat(QStringLiteral("markdown"));
    QCOMPARE(manager.defaultCopyFormat(), QStringLiteral("markdown"));
}

void tst_ComposeSettingsManager::testMetadataToggleRoundtrip()
{
    auto& manager = ComposeSettingsManager::instance();

    manager.setAutoIncludeMetadata(false);
    QCOMPARE(manager.autoIncludeMetadata(), false);

    manager.setAutoIncludeMetadata(true);
    QCOMPARE(manager.autoIncludeMetadata(), true);
}

void tst_ComposeSettingsManager::testScreenshotMaxWidthClamp()
{
    auto& manager = ComposeSettingsManager::instance();

    manager.setScreenshotMaxWidth(50);
    QCOMPARE(manager.screenshotMaxWidth(), ComposeSettingsManager::kMinScreenshotWidth);

    manager.setScreenshotMaxWidth(4000);
    QCOMPARE(manager.screenshotMaxWidth(), ComposeSettingsManager::kMaxScreenshotWidth);

    manager.setScreenshotMaxWidth(720);
    QCOMPARE(manager.screenshotMaxWidth(), 720);
}

QTEST_MAIN(tst_ComposeSettingsManager)
#include "tst_ComposeSettingsManager.moc"
