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
    void testFullScreenCaptureActionDefaultValue();
    void testFullScreenCaptureActionRoundtrip();
    void testFullScreenCaptureActionInvalidStoredValueFallsBack();
    void testDelayedCaptureSecondsDefaultValue();
    void testDelayedCaptureSecondsRoundtrip();
    void testDelayedCaptureSecondsInvalidValuesFallBack();
    void testDelayedCapturePresetsAreValid();

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
    settings.remove("regionCapture/fullScreenCaptureAction");
    settings.remove("regionCapture/delayedCaptureSeconds");
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

void tst_RegionCaptureSettingsManager::testFullScreenCaptureActionDefaultValue()
{
    QCOMPARE(
        RegionCaptureSettingsManager::instance().fullScreenCaptureAction(),
        RegionCaptureSettingsManager::FullScreenCaptureAction::CopyToClipboard);
}

void tst_RegionCaptureSettingsManager::testFullScreenCaptureActionRoundtrip()
{
    using Action = RegionCaptureSettingsManager::FullScreenCaptureAction;
    auto settings = SnapTray::getSettings();
    auto& manager = RegionCaptureSettingsManager::instance();

    manager.setFullScreenCaptureAction(Action::Edit);
    QCOMPARE(manager.fullScreenCaptureAction(), Action::Edit);
    QCOMPARE(settings.value("regionCapture/fullScreenCaptureAction").toInt(), 2);

    manager.setFullScreenCaptureAction(Action::Save);
    QCOMPARE(manager.fullScreenCaptureAction(), Action::Save);
    QCOMPARE(settings.value("regionCapture/fullScreenCaptureAction").toInt(), 1);
}

void tst_RegionCaptureSettingsManager::testFullScreenCaptureActionInvalidStoredValueFallsBack()
{
    auto settings = SnapTray::getSettings();
    settings.setValue("regionCapture/fullScreenCaptureAction", 42);
    QCOMPARE(
        RegionCaptureSettingsManager::instance().fullScreenCaptureAction(),
        RegionCaptureSettingsManager::kDefaultFullScreenCaptureAction);

    settings.setValue("regionCapture/fullScreenCaptureAction", QStringLiteral("bogus"));
    QCOMPARE(
        RegionCaptureSettingsManager::instance().fullScreenCaptureAction(),
        RegionCaptureSettingsManager::kDefaultFullScreenCaptureAction);
}

void tst_RegionCaptureSettingsManager::testDelayedCaptureSecondsDefaultValue()
{
    QCOMPARE(RegionCaptureSettingsManager::instance().delayedCaptureSeconds(),
             RegionCaptureSettingsManager::kDefaultDelayedCaptureSeconds);
    QCOMPARE(RegionCaptureSettingsManager::kDefaultDelayedCaptureSeconds, 3);
}

void tst_RegionCaptureSettingsManager::testDelayedCaptureSecondsRoundtrip()
{
    auto settings = SnapTray::getSettings();
    auto& manager = RegionCaptureSettingsManager::instance();

    manager.setDelayedCaptureSeconds(10);
    QCOMPARE(manager.delayedCaptureSeconds(), 10);
    QCOMPARE(settings.value("regionCapture/delayedCaptureSeconds").toInt(), 10);

    manager.setDelayedCaptureSeconds(5);
    QCOMPARE(manager.delayedCaptureSeconds(), 5);
    QCOMPARE(settings.value("regionCapture/delayedCaptureSeconds").toInt(), 5);
}

void tst_RegionCaptureSettingsManager::testDelayedCaptureSecondsInvalidValuesFallBack()
{
    auto settings = SnapTray::getSettings();
    auto& manager = RegionCaptureSettingsManager::instance();

    settings.setValue("regionCapture/delayedCaptureSeconds", 0);
    QCOMPARE(manager.delayedCaptureSeconds(),
             RegionCaptureSettingsManager::kDefaultDelayedCaptureSeconds);

    settings.setValue("regionCapture/delayedCaptureSeconds", -5);
    QCOMPARE(manager.delayedCaptureSeconds(),
             RegionCaptureSettingsManager::kDefaultDelayedCaptureSeconds);

    settings.setValue("regionCapture/delayedCaptureSeconds",
                      RegionCaptureSettingsManager::kMaxDelayedCaptureSeconds + 1);
    QCOMPARE(manager.delayedCaptureSeconds(),
             RegionCaptureSettingsManager::kDefaultDelayedCaptureSeconds);

    settings.setValue("regionCapture/delayedCaptureSeconds", QStringLiteral("bogus"));
    QCOMPARE(manager.delayedCaptureSeconds(),
             RegionCaptureSettingsManager::kDefaultDelayedCaptureSeconds);

    // Writing an invalid delay stores the default rather than the bad value.
    manager.setDelayedCaptureSeconds(0);
    QCOMPARE(settings.value("regionCapture/delayedCaptureSeconds").toInt(),
             RegionCaptureSettingsManager::kDefaultDelayedCaptureSeconds);
}

void tst_RegionCaptureSettingsManager::testDelayedCapturePresetsAreValid()
{
    const auto& presets = RegionCaptureSettingsManager::kDelayedCapturePresetSeconds;
    QCOMPARE(int(presets.size()), 3);
    QCOMPARE(presets[0], 3);
    QCOMPARE(presets[1], 5);
    QCOMPARE(presets[2], 10);
    for (int seconds : presets) {
        QVERIFY(RegionCaptureSettingsManager::isValidDelayedCaptureSeconds(seconds));
    }
    QVERIFY(!RegionCaptureSettingsManager::isValidDelayedCaptureSeconds(0));
    QVERIFY(!RegionCaptureSettingsManager::isValidDelayedCaptureSeconds(
        RegionCaptureSettingsManager::kMaxDelayedCaptureSeconds + 1));
}

QTEST_MAIN(tst_RegionCaptureSettingsManager)
#include "tst_RegionCaptureSettingsManager.moc"
