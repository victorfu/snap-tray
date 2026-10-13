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
    void testCursorCompanionStyleDefaultValue();
    void testSetDisabledRoundtrip();
    void testSetEnabledRoundtrip();
    void testSetMagnifierDisabledRoundtrip();
    void testSetMagnifierEnabledRoundtrip();
    void testSetCursorCompanionStyleBeaverRoundtrip();
    void testQuickCaptureActionDefaultValue();
    void testQuickCaptureActionRoundtrip();
    void testQuickCaptureActionInvalidStoredValueFallsBack();

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
    settings.remove("regionCapture/cursorCompanionStyle");
    settings.remove("regionCapture/showShortcutHints");
    settings.remove("regionCapture/quickCaptureAction");
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
    QCOMPARE(RegionCaptureSettingsManager::instance().isMagnifierEnabled(), false);
}

void tst_RegionCaptureSettingsManager::testCursorCompanionStyleDefaultValue()
{
    QCOMPARE(
        RegionCaptureSettingsManager::instance().cursorCompanionStyle(),
        RegionCaptureSettingsManager::CursorCompanionStyle::Beaver);
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
    auto settings = SnapTray::getSettings();
    auto& manager = RegionCaptureSettingsManager::instance();
    manager.setMagnifierEnabled(false);
    QCOMPARE(manager.isMagnifierEnabled(), false);
    QCOMPARE(settings.value("regionCapture/cursorCompanionStyle").toInt(), 0);
}

void tst_RegionCaptureSettingsManager::testSetMagnifierEnabledRoundtrip()
{
    auto settings = SnapTray::getSettings();
    auto& manager = RegionCaptureSettingsManager::instance();
    manager.setMagnifierEnabled(false);
    QCOMPARE(manager.isMagnifierEnabled(), false);

    manager.setMagnifierEnabled(true);
    QCOMPARE(manager.isMagnifierEnabled(), true);
    QCOMPARE(settings.value("regionCapture/cursorCompanionStyle").toInt(), 1);
}

void tst_RegionCaptureSettingsManager::testSetCursorCompanionStyleBeaverRoundtrip()
{
    auto settings = SnapTray::getSettings();
    auto& manager = RegionCaptureSettingsManager::instance();
    manager.setCursorCompanionStyle(
        RegionCaptureSettingsManager::CursorCompanionStyle::Beaver);

    QCOMPARE(
        manager.cursorCompanionStyle(),
        RegionCaptureSettingsManager::CursorCompanionStyle::Beaver);
    QCOMPARE(manager.isMagnifierEnabled(), false);
    QCOMPARE(settings.value("regionCapture/cursorCompanionStyle").toInt(), 2);
}

void tst_RegionCaptureSettingsManager::testQuickCaptureActionDefaultValue()
{
    QCOMPARE(
        RegionCaptureSettingsManager::instance().quickCaptureAction(),
        RegionCaptureSettingsManager::QuickCaptureAction::CopyToClipboard);
}

void tst_RegionCaptureSettingsManager::testQuickCaptureActionRoundtrip()
{
    using Action = RegionCaptureSettingsManager::QuickCaptureAction;
    auto settings = SnapTray::getSettings();
    auto& manager = RegionCaptureSettingsManager::instance();

    manager.setQuickCaptureAction(Action::Edit);
    QCOMPARE(manager.quickCaptureAction(), Action::Edit);
    QCOMPARE(settings.value("regionCapture/quickCaptureAction").toInt(), 2);

    manager.setQuickCaptureAction(Action::Save);
    QCOMPARE(manager.quickCaptureAction(), Action::Save);
    QCOMPARE(settings.value("regionCapture/quickCaptureAction").toInt(), 1);
}

void tst_RegionCaptureSettingsManager::testQuickCaptureActionInvalidStoredValueFallsBack()
{
    auto settings = SnapTray::getSettings();
    settings.setValue("regionCapture/quickCaptureAction", 42);
    QCOMPARE(
        RegionCaptureSettingsManager::instance().quickCaptureAction(),
        RegionCaptureSettingsManager::kDefaultQuickCaptureAction);

    settings.setValue("regionCapture/quickCaptureAction", QStringLiteral("bogus"));
    QCOMPARE(
        RegionCaptureSettingsManager::instance().quickCaptureAction(),
        RegionCaptureSettingsManager::kDefaultQuickCaptureAction);
}

QTEST_MAIN(tst_RegionCaptureSettingsManager)
#include "tst_RegionCaptureSettingsManager.moc"
