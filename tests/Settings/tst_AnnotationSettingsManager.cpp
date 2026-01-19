#include <QtTest>
#include <QSettings>
#include <QColor>
#include "settings/AnnotationSettingsManager.h"
#include "settings/Settings.h"

/**
 * @brief Unit tests for AnnotationSettingsManager singleton class.
 *
 * Tests settings persistence including:
 * - Singleton pattern
 * - Color, width, and various tool settings
 * - Step badge size with enum validation
 * - Aspect ratio settings with legacy migration
 * - Mosaic blur type settings
 */
class tst_AnnotationSettingsManager : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Singleton tests
    void testSingletonInstance();

    // Color settings tests
    void testLoadColor_DefaultValue();
    void testSaveLoadColor_Roundtrip();
    void testSaveLoadColor_NamedColors();
    void testSaveLoadColor_ARGB();

    // Width settings tests
    void testLoadWidth_DefaultValue();
    void testSaveLoadWidth_Roundtrip();
    void testSaveLoadWidth_BoundaryValues();

    // Step badge size tests
    void testLoadStepBadgeSize_DefaultValue();
    void testSaveLoadStepBadgeSize_Roundtrip();
    void testSaveLoadStepBadgeSize_AllValues();
    void testLoadStepBadgeSize_InvalidValue();

    // Corner radius tests
    void testLoadCornerRadius_DefaultValue();
    void testSaveLoadCornerRadius_Roundtrip();

    // Aspect ratio locked tests
    void testLoadAspectRatioLocked_DefaultValue();
    void testSaveLoadAspectRatioLocked_Roundtrip();

    // Aspect ratio dimensions tests
    void testLoadAspectRatioWidth_DefaultValue();
    void testLoadAspectRatioHeight_DefaultValue();
    void testSaveLoadAspectRatio_Roundtrip();
    void testSaveAspectRatio_IgnoresInvalidValues();

    // Mosaic blur type tests
    void testLoadMosaicBlurType_DefaultValue();
    void testSaveLoadMosaicBlurType_Roundtrip();
    void testSaveLoadMosaicBlurType_AllValues();
    void testLoadMosaicBlurType_InvalidValue();

private:
    void clearAllTestSettings();
    void clearSetting(const char* key);
};

void tst_AnnotationSettingsManager::init()
{
    clearAllTestSettings();
}

void tst_AnnotationSettingsManager::cleanup()
{
    clearAllTestSettings();
}

void tst_AnnotationSettingsManager::clearAllTestSettings()
{
    auto settings = SnapTray::getSettings();
    settings.remove("annotationColor");
    settings.remove("annotationWidth");
    settings.remove("stepBadgeSize");
    settings.remove("polylineMode");
    settings.remove("cornerRadius");
    settings.remove("aspectRatioLocked");
    settings.remove("aspectRatioMode");
    settings.remove("aspectRatioWidth");
    settings.remove("aspectRatioHeight");
    settings.remove("mosaicBlurType");
    settings.sync();
}

void tst_AnnotationSettingsManager::clearSetting(const char* key)
{
    auto settings = SnapTray::getSettings();
    settings.remove(key);
    settings.sync();
}

// ============================================================================
// Singleton tests
// ============================================================================

void tst_AnnotationSettingsManager::testSingletonInstance()
{
    AnnotationSettingsManager& instance1 = AnnotationSettingsManager::instance();
    AnnotationSettingsManager& instance2 = AnnotationSettingsManager::instance();

    QCOMPARE(&instance1, &instance2);
}

// ============================================================================
// Color settings tests
// ============================================================================

void tst_AnnotationSettingsManager::testLoadColor_DefaultValue()
{
    QColor color = AnnotationSettingsManager::instance().loadColor();
    QCOMPARE(color, Qt::red);
}

void tst_AnnotationSettingsManager::testSaveLoadColor_Roundtrip()
{
    AnnotationSettingsManager& manager = AnnotationSettingsManager::instance();

    QColor testColor(0, 128, 255);
    manager.saveColor(testColor);

    QColor loadedColor = manager.loadColor();
    QCOMPARE(loadedColor, testColor);
}

void tst_AnnotationSettingsManager::testSaveLoadColor_NamedColors()
{
    AnnotationSettingsManager& manager = AnnotationSettingsManager::instance();

    // Test various named colors
    QList<QColor> colors = {Qt::blue, Qt::green, Qt::yellow, Qt::cyan, Qt::magenta};

    for (const QColor& color : colors) {
        manager.saveColor(color);
        QCOMPARE(manager.loadColor(), color);
    }
}

void tst_AnnotationSettingsManager::testSaveLoadColor_ARGB()
{
    AnnotationSettingsManager& manager = AnnotationSettingsManager::instance();

    // Test color with alpha channel
    QColor semiTransparent(255, 0, 0, 128);
    manager.saveColor(semiTransparent);

    QColor loaded = manager.loadColor();
    QCOMPARE(loaded.red(), 255);
    QCOMPARE(loaded.green(), 0);
    QCOMPARE(loaded.blue(), 0);
    QCOMPARE(loaded.alpha(), 128);
}

// ============================================================================
// Width settings tests
// ============================================================================

void tst_AnnotationSettingsManager::testLoadWidth_DefaultValue()
{
    int width = AnnotationSettingsManager::instance().loadWidth();
    QCOMPARE(width, AnnotationSettingsManager::kDefaultWidth);
    QCOMPARE(width, 3);
}

void tst_AnnotationSettingsManager::testSaveLoadWidth_Roundtrip()
{
    AnnotationSettingsManager& manager = AnnotationSettingsManager::instance();

    manager.saveWidth(10);
    QCOMPARE(manager.loadWidth(), 10);

    manager.saveWidth(1);
    QCOMPARE(manager.loadWidth(), 1);
}

void tst_AnnotationSettingsManager::testSaveLoadWidth_BoundaryValues()
{
    AnnotationSettingsManager& manager = AnnotationSettingsManager::instance();

    // Test minimum practical value
    manager.saveWidth(1);
    QCOMPARE(manager.loadWidth(), 1);

    // Test large value
    manager.saveWidth(100);
    QCOMPARE(manager.loadWidth(), 100);
}

// ============================================================================
// Step badge size tests
// ============================================================================

void tst_AnnotationSettingsManager::testLoadStepBadgeSize_DefaultValue()
{
    StepBadgeSize size = AnnotationSettingsManager::instance().loadStepBadgeSize();
    QCOMPARE(size, AnnotationSettingsManager::kDefaultStepBadgeSize);
    QCOMPARE(size, StepBadgeSize::Medium);
}

void tst_AnnotationSettingsManager::testSaveLoadStepBadgeSize_Roundtrip()
{
    AnnotationSettingsManager& manager = AnnotationSettingsManager::instance();

    manager.saveStepBadgeSize(StepBadgeSize::Large);
    QCOMPARE(manager.loadStepBadgeSize(), StepBadgeSize::Large);

    manager.saveStepBadgeSize(StepBadgeSize::Small);
    QCOMPARE(manager.loadStepBadgeSize(), StepBadgeSize::Small);
}

void tst_AnnotationSettingsManager::testSaveLoadStepBadgeSize_AllValues()
{
    AnnotationSettingsManager& manager = AnnotationSettingsManager::instance();

    manager.saveStepBadgeSize(StepBadgeSize::Small);
    QCOMPARE(manager.loadStepBadgeSize(), StepBadgeSize::Small);
    QCOMPARE(static_cast<int>(manager.loadStepBadgeSize()), 0);

    manager.saveStepBadgeSize(StepBadgeSize::Medium);
    QCOMPARE(manager.loadStepBadgeSize(), StepBadgeSize::Medium);
    QCOMPARE(static_cast<int>(manager.loadStepBadgeSize()), 1);

    manager.saveStepBadgeSize(StepBadgeSize::Large);
    QCOMPARE(manager.loadStepBadgeSize(), StepBadgeSize::Large);
    QCOMPARE(static_cast<int>(manager.loadStepBadgeSize()), 2);
}

void tst_AnnotationSettingsManager::testLoadStepBadgeSize_InvalidValue()
{
    // Directly write an invalid value to QSettings
    auto settings = SnapTray::getSettings();
    settings.setValue("stepBadgeSize", 99);  // Invalid value
    settings.sync();

    StepBadgeSize size = AnnotationSettingsManager::instance().loadStepBadgeSize();
    QCOMPARE(size, AnnotationSettingsManager::kDefaultStepBadgeSize);
}

// ============================================================================
// Corner radius tests
// ============================================================================

void tst_AnnotationSettingsManager::testLoadCornerRadius_DefaultValue()
{
    int radius = AnnotationSettingsManager::instance().loadCornerRadius();
    QCOMPARE(radius, AnnotationSettingsManager::kDefaultCornerRadius);
    QCOMPARE(radius, 0);
}

void tst_AnnotationSettingsManager::testSaveLoadCornerRadius_Roundtrip()
{
    AnnotationSettingsManager& manager = AnnotationSettingsManager::instance();

    manager.saveCornerRadius(15);
    QCOMPARE(manager.loadCornerRadius(), 15);

    manager.saveCornerRadius(0);
    QCOMPARE(manager.loadCornerRadius(), 0);
}

// ============================================================================
// Aspect ratio locked tests
// ============================================================================

void tst_AnnotationSettingsManager::testLoadAspectRatioLocked_DefaultValue()
{
    bool locked = AnnotationSettingsManager::instance().loadAspectRatioLocked();
    QCOMPARE(locked, AnnotationSettingsManager::kDefaultAspectRatioLocked);
    QCOMPARE(locked, false);
}

void tst_AnnotationSettingsManager::testSaveLoadAspectRatioLocked_Roundtrip()
{
    AnnotationSettingsManager& manager = AnnotationSettingsManager::instance();

    manager.saveAspectRatioLocked(true);
    QCOMPARE(manager.loadAspectRatioLocked(), true);

    manager.saveAspectRatioLocked(false);
    QCOMPARE(manager.loadAspectRatioLocked(), false);
}

// ============================================================================
// Aspect ratio dimensions tests
// ============================================================================

void tst_AnnotationSettingsManager::testLoadAspectRatioWidth_DefaultValue()
{
    int width = AnnotationSettingsManager::instance().loadAspectRatioWidth();
    QCOMPARE(width, AnnotationSettingsManager::kDefaultAspectRatioWidth);
    QCOMPARE(width, 1);
}

void tst_AnnotationSettingsManager::testLoadAspectRatioHeight_DefaultValue()
{
    int height = AnnotationSettingsManager::instance().loadAspectRatioHeight();
    QCOMPARE(height, AnnotationSettingsManager::kDefaultAspectRatioHeight);
    QCOMPARE(height, 1);
}

void tst_AnnotationSettingsManager::testSaveLoadAspectRatio_Roundtrip()
{
    AnnotationSettingsManager& manager = AnnotationSettingsManager::instance();

    manager.saveAspectRatio(16, 9);
    QCOMPARE(manager.loadAspectRatioWidth(), 16);
    QCOMPARE(manager.loadAspectRatioHeight(), 9);

    manager.saveAspectRatio(4, 3);
    QCOMPARE(manager.loadAspectRatioWidth(), 4);
    QCOMPARE(manager.loadAspectRatioHeight(), 3);
}

void tst_AnnotationSettingsManager::testSaveAspectRatio_IgnoresInvalidValues()
{
    AnnotationSettingsManager& manager = AnnotationSettingsManager::instance();

    // Set valid values first
    manager.saveAspectRatio(16, 9);

    // Try to save invalid values (should be ignored)
    manager.saveAspectRatio(0, 5);
    QCOMPARE(manager.loadAspectRatioWidth(), 16);  // Still 16

    manager.saveAspectRatio(5, 0);
    QCOMPARE(manager.loadAspectRatioHeight(), 9);  // Still 9

    manager.saveAspectRatio(-1, -1);
    QCOMPARE(manager.loadAspectRatioWidth(), 16);
    QCOMPARE(manager.loadAspectRatioHeight(), 9);
}

// ============================================================================
// Mosaic blur type tests
// ============================================================================

void tst_AnnotationSettingsManager::testLoadMosaicBlurType_DefaultValue()
{
    auto type = AnnotationSettingsManager::instance().loadMosaicBlurType();
    QCOMPARE(type, AnnotationSettingsManager::kDefaultMosaicBlurType);
    QCOMPARE(type, MosaicBlurTypeSection::BlurType::Pixelate);
}

void tst_AnnotationSettingsManager::testSaveLoadMosaicBlurType_Roundtrip()
{
    AnnotationSettingsManager& manager = AnnotationSettingsManager::instance();

    manager.saveMosaicBlurType(MosaicBlurTypeSection::BlurType::Gaussian);
    QCOMPARE(manager.loadMosaicBlurType(), MosaicBlurTypeSection::BlurType::Gaussian);

    manager.saveMosaicBlurType(MosaicBlurTypeSection::BlurType::Pixelate);
    QCOMPARE(manager.loadMosaicBlurType(), MosaicBlurTypeSection::BlurType::Pixelate);
}

void tst_AnnotationSettingsManager::testSaveLoadMosaicBlurType_AllValues()
{
    AnnotationSettingsManager& manager = AnnotationSettingsManager::instance();

    manager.saveMosaicBlurType(MosaicBlurTypeSection::BlurType::Pixelate);
    QCOMPARE(manager.loadMosaicBlurType(), MosaicBlurTypeSection::BlurType::Pixelate);
    QCOMPARE(static_cast<int>(manager.loadMosaicBlurType()), 0);

    manager.saveMosaicBlurType(MosaicBlurTypeSection::BlurType::Gaussian);
    QCOMPARE(manager.loadMosaicBlurType(), MosaicBlurTypeSection::BlurType::Gaussian);
    QCOMPARE(static_cast<int>(manager.loadMosaicBlurType()), 1);
}

void tst_AnnotationSettingsManager::testLoadMosaicBlurType_InvalidValue()
{
    auto settings = SnapTray::getSettings();
    settings.setValue("mosaicBlurType", 99);  // Invalid value
    settings.sync();

    auto type = AnnotationSettingsManager::instance().loadMosaicBlurType();
    QCOMPARE(type, AnnotationSettingsManager::kDefaultMosaicBlurType);
}

QTEST_MAIN(tst_AnnotationSettingsManager)
#include "tst_AnnotationSettingsManager.moc"
