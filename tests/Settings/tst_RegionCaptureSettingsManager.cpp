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
    auto &instance1 = RegionCaptureSettingsManager::instance();
    auto &instance2 = RegionCaptureSettingsManager::instance();
    QCOMPARE(&instance1, &instance2);
}

void tst_RegionCaptureSettingsManager::testShortcutHintsDefaults()
{
    QCOMPARE(RegionCaptureSettingsManager::instance().isShortcutHintsEnabled(),
             RegionCaptureSettingsManager::kDefaultShortcutHintsEnabled);
}

void tst_RegionCaptureSettingsManager::testShortcutHintsRoundtrip()
{
    auto &manager = RegionCaptureSettingsManager::instance();
    manager.setShortcutHintsEnabled(false);
    QCOMPARE(manager.isShortcutHintsEnabled(), false);

    manager.setShortcutHintsEnabled(true);
    QCOMPARE(manager.isShortcutHintsEnabled(), true);
}

void tst_RegionCaptureSettingsManager::testScrollDefaults()
{
    auto &manager = RegionCaptureSettingsManager::instance();
    QVERIFY(qAbs(manager.loadScrollGoodThreshold() -
                 RegionCaptureSettingsManager::kDefaultScrollGoodThreshold) < 1e-6);
    QVERIFY(qAbs(manager.loadScrollPartialThreshold() -
                 RegionCaptureSettingsManager::kDefaultScrollPartialThreshold) < 1e-6);
    QCOMPARE(manager.loadScrollMinScrollAmount(),
             RegionCaptureSettingsManager::kDefaultScrollMinScrollAmount);
    QCOMPARE(manager.loadScrollMaxFrames(),
             RegionCaptureSettingsManager::kDefaultScrollMaxFrames);
    QCOMPARE(manager.loadScrollMaxOutputPixels(),
             RegionCaptureSettingsManager::kDefaultScrollMaxOutputPixels);
    QCOMPARE(manager.loadScrollAutoIgnoreBottomEdge(),
             RegionCaptureSettingsManager::kDefaultScrollAutoIgnoreBottomEdge);
}

void tst_RegionCaptureSettingsManager::testScrollNumericRoundtrip()
{
    auto &manager = RegionCaptureSettingsManager::instance();

    manager.saveScrollGoodThreshold(0.91);
    manager.saveScrollPartialThreshold(0.77);
    manager.saveScrollMinScrollAmount(24);
    manager.saveScrollMaxFrames(640);
    manager.saveScrollMaxOutputPixels(240000000);
    manager.saveScrollAutoIgnoreBottomEdge(false);

    QVERIFY(qAbs(manager.loadScrollGoodThreshold() - 0.91) < 1e-6);
    QVERIFY(qAbs(manager.loadScrollPartialThreshold() - 0.77) < 1e-6);
    QCOMPARE(manager.loadScrollMinScrollAmount(), 24);
    QCOMPARE(manager.loadScrollMaxFrames(), 640);
    QCOMPARE(manager.loadScrollMaxOutputPixels(), 240000000);
    QCOMPARE(manager.loadScrollAutoIgnoreBottomEdge(), false);
}

void tst_RegionCaptureSettingsManager::testThresholdClamp()
{
    auto &manager = RegionCaptureSettingsManager::instance();

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
    auto &manager = RegionCaptureSettingsManager::instance();

    manager.saveScrollMinScrollAmount(0);
    QCOMPARE(manager.loadScrollMinScrollAmount(), 1);

    manager.saveScrollMaxFrames(1);
    QCOMPARE(manager.loadScrollMaxFrames(), 10);

    manager.saveScrollMaxOutputPixels(10);
    QCOMPARE(manager.loadScrollMaxOutputPixels(), 1000000);
}

QTEST_MAIN(tst_RegionCaptureSettingsManager)
#include "tst_RegionCaptureSettingsManager.moc"
