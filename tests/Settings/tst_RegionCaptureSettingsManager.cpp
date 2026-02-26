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
    void testShortcutHintsDefaults();
    void testShortcutHintsRoundtrip();

    void testScrollAutomationDefaults();
    void testScrollAutomationRoundtrip();
    void testScrollUiDefaults();
    void testScrollUiRoundtrip();
    void testScrollUiClamp();

    void testScrollDefaults();
    void testScrollNumericRoundtrip();
    void testThresholdClamp();
    void testNumericClamp();

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
    settings.remove("regionCapture");
    settings.sync();
}

void tst_RegionCaptureSettingsManager::testSingletonInstance()
{
    auto& instance1 = RegionCaptureSettingsManager::instance();
    auto& instance2 = RegionCaptureSettingsManager::instance();
    QCOMPARE(&instance1, &instance2);
}

void tst_RegionCaptureSettingsManager::testShortcutHintsDefaults()
{
    QCOMPARE(RegionCaptureSettingsManager::instance().isShortcutHintsEnabled(),
             RegionCaptureSettingsManager::kDefaultShortcutHintsEnabled);
}

void tst_RegionCaptureSettingsManager::testShortcutHintsRoundtrip()
{
    auto& manager = RegionCaptureSettingsManager::instance();
    manager.setShortcutHintsEnabled(false);
    QCOMPARE(manager.isShortcutHintsEnabled(), false);

    manager.setShortcutHintsEnabled(true);
    QCOMPARE(manager.isShortcutHintsEnabled(), true);
}

void tst_RegionCaptureSettingsManager::testScrollAutomationDefaults()
{
    QCOMPARE(RegionCaptureSettingsManager::instance().isScrollAutomationEnabled(),
             RegionCaptureSettingsManager::kDefaultScrollAutomationEnabled);
}

void tst_RegionCaptureSettingsManager::testScrollAutomationRoundtrip()
{
    auto& manager = RegionCaptureSettingsManager::instance();
    manager.setScrollAutomationEnabled(false);
    QCOMPARE(manager.isScrollAutomationEnabled(), false);

    manager.setScrollAutomationEnabled(true);
    QCOMPARE(manager.isScrollAutomationEnabled(), true);
}

void tst_RegionCaptureSettingsManager::testScrollUiDefaults()
{
    auto& manager = RegionCaptureSettingsManager::instance();
    QCOMPARE(manager.loadScrollAutoStartMode(),
             RegionCaptureSettingsManager::kDefaultScrollAutoStartMode);
    QCOMPARE(manager.loadScrollInlinePreviewCollapsed(),
             RegionCaptureSettingsManager::kDefaultScrollInlinePreviewCollapsed);
}

void tst_RegionCaptureSettingsManager::testScrollUiRoundtrip()
{
    auto& manager = RegionCaptureSettingsManager::instance();
    manager.saveScrollAutoStartMode(RegionCaptureSettingsManager::ScrollAutoStartMode::AutoProbe);
    manager.saveScrollInlinePreviewCollapsed(false);
    QCOMPARE(manager.loadScrollAutoStartMode(),
             RegionCaptureSettingsManager::ScrollAutoStartMode::AutoProbe);
    QCOMPARE(manager.loadScrollInlinePreviewCollapsed(), false);
}

void tst_RegionCaptureSettingsManager::testScrollUiClamp()
{
    auto settings = SnapTray::getSettings();
    settings.setValue("regionCapture/scrollAutoStartMode", 99);
    settings.sync();

    auto &manager = RegionCaptureSettingsManager::instance();
    QCOMPARE(manager.loadScrollAutoStartMode(),
             RegionCaptureSettingsManager::ScrollAutoStartMode::AutoProbe);

    settings.setValue("regionCapture/scrollAutoStartMode", -3);
    settings.sync();
    QCOMPARE(manager.loadScrollAutoStartMode(),
             RegionCaptureSettingsManager::ScrollAutoStartMode::ManualFirst);
}

void tst_RegionCaptureSettingsManager::testScrollDefaults()
{
    auto& manager = RegionCaptureSettingsManager::instance();
    QVERIFY(qAbs(manager.loadScrollGoodThreshold() - RegionCaptureSettingsManager::kDefaultScrollGoodThreshold) < 1e-6);
    QVERIFY(qAbs(manager.loadScrollPartialThreshold() - RegionCaptureSettingsManager::kDefaultScrollPartialThreshold) < 1e-6);
    QCOMPARE(manager.loadScrollMinScrollAmount(), RegionCaptureSettingsManager::kDefaultScrollMinScrollAmount);
    QCOMPARE(manager.loadScrollAutoStepDelayMs(), RegionCaptureSettingsManager::kDefaultScrollAutoStepDelayMs);
    QCOMPARE(manager.loadScrollMaxFrames(), RegionCaptureSettingsManager::kDefaultScrollMaxFrames);
    QCOMPARE(manager.loadScrollMaxOutputPixels(), RegionCaptureSettingsManager::kDefaultScrollMaxOutputPixels);
    QCOMPARE(manager.loadScrollAutoIgnoreBottomEdge(), RegionCaptureSettingsManager::kDefaultScrollAutoIgnoreBottomEdge);
    QCOMPARE(manager.loadScrollSettleStableFrames(), RegionCaptureSettingsManager::kDefaultScrollSettleStableFrames);
    QCOMPARE(manager.loadScrollSettleTimeoutMs(), RegionCaptureSettingsManager::kDefaultScrollSettleTimeoutMs);
    QCOMPARE(manager.loadScrollProbeGridDensity(), RegionCaptureSettingsManager::kDefaultScrollProbeGridDensity);
}

void tst_RegionCaptureSettingsManager::testScrollNumericRoundtrip()
{
    auto& manager = RegionCaptureSettingsManager::instance();

    manager.saveScrollGoodThreshold(0.91);
    manager.saveScrollPartialThreshold(0.77);
    manager.saveScrollMinScrollAmount(24);
    manager.saveScrollAutoStepDelayMs(220);
    manager.saveScrollMaxFrames(640);
    manager.saveScrollMaxOutputPixels(240000000);
    manager.saveScrollAutoIgnoreBottomEdge(false);
    manager.saveScrollSettleStableFrames(3);
    manager.saveScrollSettleTimeoutMs(340);
    manager.saveScrollProbeGridDensity(4);

    QVERIFY(qAbs(manager.loadScrollGoodThreshold() - 0.91) < 1e-6);
    QVERIFY(qAbs(manager.loadScrollPartialThreshold() - 0.77) < 1e-6);
    QCOMPARE(manager.loadScrollMinScrollAmount(), 24);
    QCOMPARE(manager.loadScrollAutoStepDelayMs(), 220);
    QCOMPARE(manager.loadScrollMaxFrames(), 640);
    QCOMPARE(manager.loadScrollMaxOutputPixels(), 240000000);
    QCOMPARE(manager.loadScrollAutoIgnoreBottomEdge(), false);
    QCOMPARE(manager.loadScrollSettleStableFrames(), 3);
    QCOMPARE(manager.loadScrollSettleTimeoutMs(), 340);
    QCOMPARE(manager.loadScrollProbeGridDensity(), 4);
}

void tst_RegionCaptureSettingsManager::testThresholdClamp()
{
    auto& manager = RegionCaptureSettingsManager::instance();

    manager.saveScrollGoodThreshold(5.0);
    QVERIFY(qAbs(manager.loadScrollGoodThreshold() - 0.99) < 1e-6);

    manager.saveScrollGoodThreshold(-1.0);
    QVERIFY(qAbs(manager.loadScrollGoodThreshold() - 0.50) < 1e-6);

    manager.saveScrollGoodThreshold(0.70);
    manager.saveScrollPartialThreshold(0.95);
    QVERIFY(qAbs(manager.loadScrollPartialThreshold() - 0.70) < 1e-6);

    manager.saveScrollPartialThreshold(-2.0);
    QVERIFY(qAbs(manager.loadScrollPartialThreshold() - 0.40) < 1e-6);
}

void tst_RegionCaptureSettingsManager::testNumericClamp()
{
    auto& manager = RegionCaptureSettingsManager::instance();

    manager.saveScrollMinScrollAmount(0);
    QCOMPARE(manager.loadScrollMinScrollAmount(), 1);

    manager.saveScrollAutoStepDelayMs(1);
    QCOMPARE(manager.loadScrollAutoStepDelayMs(), 40);

    manager.saveScrollMaxFrames(1);
    QCOMPARE(manager.loadScrollMaxFrames(), 10);

    manager.saveScrollMaxOutputPixels(10);
    QCOMPARE(manager.loadScrollMaxOutputPixels(), 1000000);

    manager.saveScrollSettleStableFrames(0);
    QCOMPARE(manager.loadScrollSettleStableFrames(), 1);

    manager.saveScrollSettleTimeoutMs(1);
    QCOMPARE(manager.loadScrollSettleTimeoutMs(), 80);

    manager.saveScrollProbeGridDensity(99);
    QCOMPARE(manager.loadScrollProbeGridDensity(), 5);
}

QTEST_MAIN(tst_RegionCaptureSettingsManager)
#include "tst_RegionCaptureSettingsManager.moc"
