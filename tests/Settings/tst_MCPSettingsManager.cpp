#include <QtTest/QtTest>

#include "settings/MCPSettingsManager.h"
#include "settings/Settings.h"

class tst_MCPSettingsManager : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testSingletonInstance();
    void testDefaultValueEnabled();
    void testSetDisabledRoundtrip();
    void testSetEnabledRoundtrip();

private:
    void clearSettings();
};

void tst_MCPSettingsManager::init()
{
    clearSettings();
}

void tst_MCPSettingsManager::cleanup()
{
    clearSettings();
}

void tst_MCPSettingsManager::clearSettings()
{
    auto settings = SnapTray::getSettings();
    settings.remove("mcp/enabled");
    settings.sync();
}

void tst_MCPSettingsManager::testSingletonInstance()
{
    auto& instance1 = MCPSettingsManager::instance();
    auto& instance2 = MCPSettingsManager::instance();
    QCOMPARE(&instance1, &instance2);
}

void tst_MCPSettingsManager::testDefaultValueEnabled()
{
    QCOMPARE(MCPSettingsManager::instance().isEnabled(), true);
}

void tst_MCPSettingsManager::testSetDisabledRoundtrip()
{
    auto& manager = MCPSettingsManager::instance();
    manager.setEnabled(false);
    QCOMPARE(manager.isEnabled(), false);
}

void tst_MCPSettingsManager::testSetEnabledRoundtrip()
{
    auto& manager = MCPSettingsManager::instance();
    manager.setEnabled(false);
    QCOMPARE(manager.isEnabled(), false);

    manager.setEnabled(true);
    QCOMPARE(manager.isEnabled(), true);
}

QTEST_MAIN(tst_MCPSettingsManager)
#include "tst_MCPSettingsManager.moc"
