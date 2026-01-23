#include <QtTest/QtTest>
#include <QSettings>
#include "settings/PinWindowSettingsManager.h"
#include "settings/Settings.h"

/**
 * @brief Unit tests for PinWindowSettingsManager singleton class.
 *
 * Tests settings persistence including:
 * - Singleton pattern
 * - Default opacity settings
 * - Opacity step settings
 * - Zoom step settings
 * - Shadow enabled settings
 * - Max cache files settings
 * - Value clamping/validation
 */
class tst_PinWindowSettingsManager : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Singleton tests
    void testSingletonInstance();

    // Default opacity tests
    void testLoadDefaultOpacity_DefaultValue();
    void testSaveLoadDefaultOpacity_Roundtrip();
    void testLoadDefaultOpacity_ClampMin();
    void testLoadDefaultOpacity_ClampMax();
    void testDefaultOpacity_BoundaryValues();

    // Opacity step tests
    void testLoadOpacityStep_DefaultValue();
    void testSaveLoadOpacityStep_Roundtrip();
    void testLoadOpacityStep_ClampMin();
    void testLoadOpacityStep_ClampMax();

    // Zoom step tests
    void testLoadZoomStep_DefaultValue();
    void testSaveLoadZoomStep_Roundtrip();
    void testLoadZoomStep_ClampMin();
    void testLoadZoomStep_ClampMax();

    // Shadow enabled tests
    void testLoadShadowEnabled_DefaultValue();
    void testSaveLoadShadowEnabled_Roundtrip();
    void testSaveLoadShadowEnabled_True();
    void testSaveLoadShadowEnabled_False();

    // Max cache files tests
    void testLoadMaxCacheFiles_DefaultValue();
    void testSaveLoadMaxCacheFiles_Roundtrip();
    void testLoadMaxCacheFiles_ClampMin();
    void testLoadMaxCacheFiles_ClampMax();
    void testMaxCacheFiles_ValidRange();

    // Constants tests
    void testConstants_DefaultValues();

private:
    void clearAllTestSettings();
};

void tst_PinWindowSettingsManager::init()
{
    clearAllTestSettings();
}

void tst_PinWindowSettingsManager::cleanup()
{
    clearAllTestSettings();
}

void tst_PinWindowSettingsManager::clearAllTestSettings()
{
    auto settings = SnapTray::getSettings();
    settings.remove("pinWindow/defaultOpacity");
    settings.remove("pinWindow/opacityStep");
    settings.remove("pinWindow/zoomStep");
    settings.remove("pinWindow/shadowEnabled");
    settings.remove("pinWindow/maxCacheFiles");
    settings.sync();
}

// ============================================================================
// Singleton Tests
// ============================================================================

void tst_PinWindowSettingsManager::testSingletonInstance()
{
    PinWindowSettingsManager& instance1 = PinWindowSettingsManager::instance();
    PinWindowSettingsManager& instance2 = PinWindowSettingsManager::instance();

    QCOMPARE(&instance1, &instance2);
}

// ============================================================================
// Default Opacity Tests
// ============================================================================

void tst_PinWindowSettingsManager::testLoadDefaultOpacity_DefaultValue()
{
    qreal opacity = PinWindowSettingsManager::instance().loadDefaultOpacity();
    QCOMPARE(opacity, PinWindowSettingsManager::kDefaultOpacity);
    QCOMPARE(opacity, 1.0);
}

void tst_PinWindowSettingsManager::testSaveLoadDefaultOpacity_Roundtrip()
{
    PinWindowSettingsManager& manager = PinWindowSettingsManager::instance();

    manager.saveDefaultOpacity(0.75);
    QCOMPARE(manager.loadDefaultOpacity(), 0.75);

    manager.saveDefaultOpacity(0.5);
    QCOMPARE(manager.loadDefaultOpacity(), 0.5);
}

void tst_PinWindowSettingsManager::testLoadDefaultOpacity_ClampMin()
{
    auto settings = SnapTray::getSettings();
    settings.setValue("pinWindow/defaultOpacity", 0.05);  // Below minimum
    settings.sync();

    qreal opacity = PinWindowSettingsManager::instance().loadDefaultOpacity();
    QCOMPARE(opacity, 0.1);  // Clamped to minimum
}

void tst_PinWindowSettingsManager::testLoadDefaultOpacity_ClampMax()
{
    auto settings = SnapTray::getSettings();
    settings.setValue("pinWindow/defaultOpacity", 1.5);  // Above maximum
    settings.sync();

    qreal opacity = PinWindowSettingsManager::instance().loadDefaultOpacity();
    QCOMPARE(opacity, 1.0);  // Clamped to maximum
}

void tst_PinWindowSettingsManager::testDefaultOpacity_BoundaryValues()
{
    PinWindowSettingsManager& manager = PinWindowSettingsManager::instance();

    // Test minimum valid value
    manager.saveDefaultOpacity(0.1);
    QCOMPARE(manager.loadDefaultOpacity(), 0.1);

    // Test maximum valid value
    manager.saveDefaultOpacity(1.0);
    QCOMPARE(manager.loadDefaultOpacity(), 1.0);

    // Test middle value
    manager.saveDefaultOpacity(0.55);
    QCOMPARE(manager.loadDefaultOpacity(), 0.55);
}

// ============================================================================
// Opacity Step Tests
// ============================================================================

void tst_PinWindowSettingsManager::testLoadOpacityStep_DefaultValue()
{
    qreal step = PinWindowSettingsManager::instance().loadOpacityStep();
    QCOMPARE(step, PinWindowSettingsManager::kDefaultOpacityStep);
    QCOMPARE(step, 0.05);
}

void tst_PinWindowSettingsManager::testSaveLoadOpacityStep_Roundtrip()
{
    PinWindowSettingsManager& manager = PinWindowSettingsManager::instance();

    manager.saveOpacityStep(0.10);
    QCOMPARE(manager.loadOpacityStep(), 0.10);

    manager.saveOpacityStep(0.02);
    QCOMPARE(manager.loadOpacityStep(), 0.02);
}

void tst_PinWindowSettingsManager::testLoadOpacityStep_ClampMin()
{
    auto settings = SnapTray::getSettings();
    settings.setValue("pinWindow/opacityStep", 0.001);  // Below minimum
    settings.sync();

    qreal step = PinWindowSettingsManager::instance().loadOpacityStep();
    QCOMPARE(step, 0.01);  // Clamped to minimum
}

void tst_PinWindowSettingsManager::testLoadOpacityStep_ClampMax()
{
    auto settings = SnapTray::getSettings();
    settings.setValue("pinWindow/opacityStep", 0.5);  // Above maximum
    settings.sync();

    qreal step = PinWindowSettingsManager::instance().loadOpacityStep();
    QCOMPARE(step, 0.20);  // Clamped to maximum
}

// ============================================================================
// Zoom Step Tests
// ============================================================================

void tst_PinWindowSettingsManager::testLoadZoomStep_DefaultValue()
{
    qreal step = PinWindowSettingsManager::instance().loadZoomStep();
    QCOMPARE(step, PinWindowSettingsManager::kDefaultZoomStep);
    QCOMPARE(step, 0.05);
}

void tst_PinWindowSettingsManager::testSaveLoadZoomStep_Roundtrip()
{
    PinWindowSettingsManager& manager = PinWindowSettingsManager::instance();

    manager.saveZoomStep(0.15);
    QCOMPARE(manager.loadZoomStep(), 0.15);

    manager.saveZoomStep(0.03);
    QCOMPARE(manager.loadZoomStep(), 0.03);
}

void tst_PinWindowSettingsManager::testLoadZoomStep_ClampMin()
{
    auto settings = SnapTray::getSettings();
    settings.setValue("pinWindow/zoomStep", 0.005);  // Below minimum
    settings.sync();

    qreal step = PinWindowSettingsManager::instance().loadZoomStep();
    QCOMPARE(step, 0.01);  // Clamped to minimum
}

void tst_PinWindowSettingsManager::testLoadZoomStep_ClampMax()
{
    auto settings = SnapTray::getSettings();
    settings.setValue("pinWindow/zoomStep", 0.50);  // Above maximum
    settings.sync();

    qreal step = PinWindowSettingsManager::instance().loadZoomStep();
    QCOMPARE(step, 0.20);  // Clamped to maximum
}

// ============================================================================
// Shadow Enabled Tests
// ============================================================================

void tst_PinWindowSettingsManager::testLoadShadowEnabled_DefaultValue()
{
    bool enabled = PinWindowSettingsManager::instance().loadShadowEnabled();
    QCOMPARE(enabled, PinWindowSettingsManager::kDefaultShadowEnabled);
    QCOMPARE(enabled, true);
}

void tst_PinWindowSettingsManager::testSaveLoadShadowEnabled_Roundtrip()
{
    PinWindowSettingsManager& manager = PinWindowSettingsManager::instance();

    manager.saveShadowEnabled(false);
    QCOMPARE(manager.loadShadowEnabled(), false);

    manager.saveShadowEnabled(true);
    QCOMPARE(manager.loadShadowEnabled(), true);
}

void tst_PinWindowSettingsManager::testSaveLoadShadowEnabled_True()
{
    PinWindowSettingsManager& manager = PinWindowSettingsManager::instance();
    manager.saveShadowEnabled(true);
    QVERIFY(manager.loadShadowEnabled());
}

void tst_PinWindowSettingsManager::testSaveLoadShadowEnabled_False()
{
    PinWindowSettingsManager& manager = PinWindowSettingsManager::instance();
    manager.saveShadowEnabled(false);
    QVERIFY(!manager.loadShadowEnabled());
}

// ============================================================================
// Max Cache Files Tests
// ============================================================================

void tst_PinWindowSettingsManager::testLoadMaxCacheFiles_DefaultValue()
{
    int maxFiles = PinWindowSettingsManager::instance().loadMaxCacheFiles();
    QCOMPARE(maxFiles, PinWindowSettingsManager::kDefaultMaxCacheFiles);
    QCOMPARE(maxFiles, 20);
}

void tst_PinWindowSettingsManager::testSaveLoadMaxCacheFiles_Roundtrip()
{
    PinWindowSettingsManager& manager = PinWindowSettingsManager::instance();

    manager.saveMaxCacheFiles(50);
    QCOMPARE(manager.loadMaxCacheFiles(), 50);

    manager.saveMaxCacheFiles(100);
    QCOMPARE(manager.loadMaxCacheFiles(), 100);
}

void tst_PinWindowSettingsManager::testLoadMaxCacheFiles_ClampMin()
{
    auto settings = SnapTray::getSettings();
    settings.setValue("pinWindow/maxCacheFiles", 2);  // Below minimum
    settings.sync();

    int maxFiles = PinWindowSettingsManager::instance().loadMaxCacheFiles();
    QCOMPARE(maxFiles, 5);  // Clamped to minimum
}

void tst_PinWindowSettingsManager::testLoadMaxCacheFiles_ClampMax()
{
    auto settings = SnapTray::getSettings();
    settings.setValue("pinWindow/maxCacheFiles", 500);  // Above maximum
    settings.sync();

    int maxFiles = PinWindowSettingsManager::instance().loadMaxCacheFiles();
    QCOMPARE(maxFiles, 200);  // Clamped to maximum
}

void tst_PinWindowSettingsManager::testMaxCacheFiles_ValidRange()
{
    PinWindowSettingsManager& manager = PinWindowSettingsManager::instance();

    // Test minimum valid value
    manager.saveMaxCacheFiles(5);
    QCOMPARE(manager.loadMaxCacheFiles(), 5);

    // Test maximum valid value
    manager.saveMaxCacheFiles(200);
    QCOMPARE(manager.loadMaxCacheFiles(), 200);

    // Test middle value
    manager.saveMaxCacheFiles(75);
    QCOMPARE(manager.loadMaxCacheFiles(), 75);
}

// ============================================================================
// Constants Tests
// ============================================================================

void tst_PinWindowSettingsManager::testConstants_DefaultValues()
{
    // Verify constants are defined correctly
    QCOMPARE(PinWindowSettingsManager::kDefaultOpacity, 1.0);
    QCOMPARE(PinWindowSettingsManager::kDefaultOpacityStep, 0.05);
    QCOMPARE(PinWindowSettingsManager::kDefaultZoomStep, 0.05);
    QCOMPARE(PinWindowSettingsManager::kDefaultShadowEnabled, true);
    QCOMPARE(PinWindowSettingsManager::kDefaultMaxCacheFiles, 20);
}

QTEST_MAIN(tst_PinWindowSettingsManager)
#include "tst_PinWindowSettingsManager.moc"
