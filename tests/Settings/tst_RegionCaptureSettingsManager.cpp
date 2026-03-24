#include <QtTest/QtTest>

#include "settings/RegionCaptureSettingsManager.h"
#include "settings/Settings.h"

class tst_RegionCaptureSettingsManager : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testSingletonInstance();
    void testDefaultValueEnabled();
    void testMagnifierDefaultValueEnabled();
    void testSetDisabledRoundtrip();
    void testSetEnabledRoundtrip();
    void testSetMagnifierDisabledRoundtrip();
    void testSetMagnifierEnabledRoundtrip();

private:
    void clearSettings();
};

void tst_RegionCaptureSettingsManager::init()
{
    clearSettings();
}

void tst_RegionCaptureSettingsManager::cleanup()
{
    clearSettings();
}

void tst_RegionCaptureSettingsManager::clearSettings()
{
    auto settings = SnapTray::getSettings();
    settings.remove("regionCapture/showShortcutHints");
    settings.remove("regionCapture/showMagnifier");
    settings.sync();
}

void tst_RegionCaptureSettingsManager::testSingletonInstance()
{
    auto& instance1 = RegionCaptureSettingsManager::instance();
    auto& instance2 = RegionCaptureSettingsManager::instance();
    QCOMPARE(&instance1, &instance2);
}

void tst_RegionCaptureSettingsManager::testDefaultValueEnabled()
{
    QCOMPARE(RegionCaptureSettingsManager::instance().isShortcutHintsEnabled(), true);
}

void tst_RegionCaptureSettingsManager::testMagnifierDefaultValueEnabled()
{
    QCOMPARE(RegionCaptureSettingsManager::instance().isMagnifierEnabled(), true);
}

void tst_RegionCaptureSettingsManager::testSetDisabledRoundtrip()
{
    auto& manager = RegionCaptureSettingsManager::instance();
    manager.setShortcutHintsEnabled(false);
    QCOMPARE(manager.isShortcutHintsEnabled(), false);
}

void tst_RegionCaptureSettingsManager::testSetEnabledRoundtrip()
{
    auto& manager = RegionCaptureSettingsManager::instance();
    manager.setShortcutHintsEnabled(false);
    QCOMPARE(manager.isShortcutHintsEnabled(), false);

    manager.setShortcutHintsEnabled(true);
    QCOMPARE(manager.isShortcutHintsEnabled(), true);
}

void tst_RegionCaptureSettingsManager::testSetMagnifierDisabledRoundtrip()
{
    auto& manager = RegionCaptureSettingsManager::instance();
    manager.setMagnifierEnabled(false);
    QCOMPARE(manager.isMagnifierEnabled(), false);
}

void tst_RegionCaptureSettingsManager::testSetMagnifierEnabledRoundtrip()
{
    auto& manager = RegionCaptureSettingsManager::instance();
    manager.setMagnifierEnabled(false);
    QCOMPARE(manager.isMagnifierEnabled(), false);

    manager.setMagnifierEnabled(true);
    QCOMPARE(manager.isMagnifierEnabled(), true);
}

QTEST_MAIN(tst_RegionCaptureSettingsManager)
#include "tst_RegionCaptureSettingsManager.moc"
