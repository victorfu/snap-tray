#include <QtTest/QtTest>
#include <QSettings>
#include "beautify/BeautifySettings.h"
#include "settings/BeautifySettingsManager.h"
#include "settings/Settings.h"

class tst_BeautifySettings : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // BeautifySettings struct defaults
    void testDefaultSettings_Values();

    // Preset tests
    void testPresets_Count();
    void testPresets_HaveValidColors();
    void testPresets_HaveNames();

    // Singleton
    void testSingletonInstance();

    // BackgroundType tests
    void testLoadBackgroundType_DefaultValue();
    void testSaveLoadBackgroundType_Roundtrip();
    void testLoadBackgroundType_InvalidValue();

    // BackgroundColor tests
    void testLoadBackgroundColor_DefaultValue();
    void testSaveLoadBackgroundColor_Roundtrip();

    // GradientStartColor tests
    void testLoadGradientStartColor_DefaultValue();
    void testSaveLoadGradientStartColor_Roundtrip();

    // GradientEndColor tests
    void testLoadGradientEndColor_DefaultValue();
    void testSaveLoadGradientEndColor_Roundtrip();

    // GradientAngle tests
    void testLoadGradientAngle_DefaultValue();
    void testSaveLoadGradientAngle_Roundtrip();
    void testLoadGradientAngle_ClampMin();
    void testLoadGradientAngle_ClampMax();

    // Padding tests
    void testLoadPadding_DefaultValue();
    void testSaveLoadPadding_Roundtrip();
    void testLoadPadding_ClampMin();
    void testLoadPadding_ClampMax();

    // CornerRadius tests
    void testLoadCornerRadius_DefaultValue();
    void testSaveLoadCornerRadius_Roundtrip();
    void testLoadCornerRadius_ClampMin();
    void testLoadCornerRadius_ClampMax();

    // ShadowEnabled tests
    void testLoadShadowEnabled_DefaultValue();
    void testSaveLoadShadowEnabled_Roundtrip();

    // ShadowBlur tests
    void testLoadShadowBlur_DefaultValue();
    void testSaveLoadShadowBlur_Roundtrip();
    void testLoadShadowBlur_ClampMin();
    void testLoadShadowBlur_ClampMax();

    // ShadowOffset tests
    void testLoadShadowOffsetX_DefaultValue();
    void testLoadShadowOffsetY_DefaultValue();
    void testSaveLoadShadowOffset_Roundtrip();
    void testLoadShadowOffset_ClampMin();
    void testLoadShadowOffset_ClampMax();

    // AspectRatio tests
    void testLoadAspectRatio_DefaultValue();
    void testSaveLoadAspectRatio_Roundtrip();
    void testLoadAspectRatio_InvalidValue();

    // Full settings roundtrip
    void testSaveLoadSettings_Roundtrip();

private:
    void clearAllTestSettings();
};

void tst_BeautifySettings::init()
{
    clearAllTestSettings();
}

void tst_BeautifySettings::cleanup()
{
    clearAllTestSettings();
}

void tst_BeautifySettings::clearAllTestSettings()
{
    auto settings = SnapTray::getSettings();
    settings.remove("beautify/backgroundType");
    settings.remove("beautify/backgroundColor");
    settings.remove("beautify/gradientStartColor");
    settings.remove("beautify/gradientEndColor");
    settings.remove("beautify/gradientAngle");
    settings.remove("beautify/padding");
    settings.remove("beautify/cornerRadius");
    settings.remove("beautify/shadowEnabled");
    settings.remove("beautify/shadowBlur");
    settings.remove("beautify/shadowOffsetX");
    settings.remove("beautify/shadowOffsetY");
    settings.remove("beautify/shadowColor");
    settings.remove("beautify/aspectRatio");
    settings.sync();
}

// ============================================================================
// BeautifySettings Struct Defaults
// ============================================================================

void tst_BeautifySettings::testDefaultSettings_Values()
{
    BeautifySettings s;
    QCOMPARE(s.backgroundType, BeautifyBackgroundType::LinearGradient);
    QCOMPARE(s.gradientAngle, 135);
    QCOMPARE(s.padding, 64);
    QCOMPARE(s.cornerRadius, 12);
    QCOMPARE(s.shadowEnabled, true);
    QCOMPARE(s.shadowBlur, 40);
    QCOMPARE(s.shadowOffsetX, 0);
    QCOMPARE(s.shadowOffsetY, 8);
    QCOMPARE(s.aspectRatio, BeautifyAspectRatio::Auto);
}

// ============================================================================
// Preset Tests
// ============================================================================

void tst_BeautifySettings::testPresets_Count()
{
    auto presets = beautifyPresets();
    QCOMPARE(presets.size(), 8);
}

void tst_BeautifySettings::testPresets_HaveValidColors()
{
    auto presets = beautifyPresets();
    for (const auto& preset : presets) {
        QVERIFY(preset.gradientStart.isValid());
        QVERIFY(preset.gradientEnd.isValid());
        QVERIFY(preset.gradientAngle >= 0 && preset.gradientAngle <= 360);
    }
}

void tst_BeautifySettings::testPresets_HaveNames()
{
    auto presets = beautifyPresets();
    for (const auto& preset : presets) {
        QVERIFY(!preset.name.isEmpty());
    }
}

// ============================================================================
// Singleton Test
// ============================================================================

void tst_BeautifySettings::testSingletonInstance()
{
    BeautifySettingsManager& a = BeautifySettingsManager::instance();
    BeautifySettingsManager& b = BeautifySettingsManager::instance();
    QCOMPARE(&a, &b);
}

// ============================================================================
// BackgroundType Tests
// ============================================================================

void tst_BeautifySettings::testLoadBackgroundType_DefaultValue()
{
    auto type = BeautifySettingsManager::instance().loadBackgroundType();
    QCOMPARE(type, BeautifyBackgroundType::LinearGradient);
}

void tst_BeautifySettings::testSaveLoadBackgroundType_Roundtrip()
{
    auto& mgr = BeautifySettingsManager::instance();
    mgr.saveBackgroundType(BeautifyBackgroundType::Solid);
    QCOMPARE(mgr.loadBackgroundType(), BeautifyBackgroundType::Solid);

    mgr.saveBackgroundType(BeautifyBackgroundType::RadialGradient);
    QCOMPARE(mgr.loadBackgroundType(), BeautifyBackgroundType::RadialGradient);
}

void tst_BeautifySettings::testLoadBackgroundType_InvalidValue()
{
    auto settings = SnapTray::getSettings();
    settings.setValue("beautify/backgroundType", 99);
    settings.sync();

    auto type = BeautifySettingsManager::instance().loadBackgroundType();
    QCOMPARE(type, BeautifyBackgroundType::LinearGradient);
}

// ============================================================================
// BackgroundColor Tests
// ============================================================================

void tst_BeautifySettings::testLoadBackgroundColor_DefaultValue()
{
    QColor color = BeautifySettingsManager::instance().loadBackgroundColor();
    QCOMPARE(color, QColor(0, 180, 219));
}

void tst_BeautifySettings::testSaveLoadBackgroundColor_Roundtrip()
{
    auto& mgr = BeautifySettingsManager::instance();
    QColor testColor(255, 0, 128);
    mgr.saveBackgroundColor(testColor);
    QCOMPARE(mgr.loadBackgroundColor(), testColor);
}

// ============================================================================
// GradientStartColor Tests
// ============================================================================

void tst_BeautifySettings::testLoadGradientStartColor_DefaultValue()
{
    QColor color = BeautifySettingsManager::instance().loadGradientStartColor();
    QCOMPARE(color, QColor(0, 180, 219));
}

void tst_BeautifySettings::testSaveLoadGradientStartColor_Roundtrip()
{
    auto& mgr = BeautifySettingsManager::instance();
    QColor testColor(10, 20, 30);
    mgr.saveGradientStartColor(testColor);
    QCOMPARE(mgr.loadGradientStartColor(), testColor);
}

// ============================================================================
// GradientEndColor Tests
// ============================================================================

void tst_BeautifySettings::testLoadGradientEndColor_DefaultValue()
{
    QColor color = BeautifySettingsManager::instance().loadGradientEndColor();
    QCOMPARE(color, QColor(0, 131, 176));
}

void tst_BeautifySettings::testSaveLoadGradientEndColor_Roundtrip()
{
    auto& mgr = BeautifySettingsManager::instance();
    QColor testColor(50, 100, 150);
    mgr.saveGradientEndColor(testColor);
    QCOMPARE(mgr.loadGradientEndColor(), testColor);
}

// ============================================================================
// GradientAngle Tests
// ============================================================================

void tst_BeautifySettings::testLoadGradientAngle_DefaultValue()
{
    QCOMPARE(BeautifySettingsManager::instance().loadGradientAngle(), 135);
}

void tst_BeautifySettings::testSaveLoadGradientAngle_Roundtrip()
{
    auto& mgr = BeautifySettingsManager::instance();
    mgr.saveGradientAngle(45);
    QCOMPARE(mgr.loadGradientAngle(), 45);

    mgr.saveGradientAngle(270);
    QCOMPARE(mgr.loadGradientAngle(), 270);
}

void tst_BeautifySettings::testLoadGradientAngle_ClampMin()
{
    auto settings = SnapTray::getSettings();
    settings.setValue("beautify/gradientAngle", -10);
    settings.sync();
    QCOMPARE(BeautifySettingsManager::instance().loadGradientAngle(), 0);
}

void tst_BeautifySettings::testLoadGradientAngle_ClampMax()
{
    auto settings = SnapTray::getSettings();
    settings.setValue("beautify/gradientAngle", 500);
    settings.sync();
    QCOMPARE(BeautifySettingsManager::instance().loadGradientAngle(), 360);
}

// ============================================================================
// Padding Tests
// ============================================================================

void tst_BeautifySettings::testLoadPadding_DefaultValue()
{
    QCOMPARE(BeautifySettingsManager::instance().loadPadding(), 64);
}

void tst_BeautifySettings::testSaveLoadPadding_Roundtrip()
{
    auto& mgr = BeautifySettingsManager::instance();
    mgr.savePadding(100);
    QCOMPARE(mgr.loadPadding(), 100);
}

void tst_BeautifySettings::testLoadPadding_ClampMin()
{
    auto settings = SnapTray::getSettings();
    settings.setValue("beautify/padding", 5);
    settings.sync();
    QCOMPARE(BeautifySettingsManager::instance().loadPadding(), 16);
}

void tst_BeautifySettings::testLoadPadding_ClampMax()
{
    auto settings = SnapTray::getSettings();
    settings.setValue("beautify/padding", 999);
    settings.sync();
    QCOMPARE(BeautifySettingsManager::instance().loadPadding(), 200);
}

// ============================================================================
// CornerRadius Tests
// ============================================================================

void tst_BeautifySettings::testLoadCornerRadius_DefaultValue()
{
    QCOMPARE(BeautifySettingsManager::instance().loadCornerRadius(), 12);
}

void tst_BeautifySettings::testSaveLoadCornerRadius_Roundtrip()
{
    auto& mgr = BeautifySettingsManager::instance();
    mgr.saveCornerRadius(20);
    QCOMPARE(mgr.loadCornerRadius(), 20);
}

void tst_BeautifySettings::testLoadCornerRadius_ClampMin()
{
    auto settings = SnapTray::getSettings();
    settings.setValue("beautify/cornerRadius", -5);
    settings.sync();
    QCOMPARE(BeautifySettingsManager::instance().loadCornerRadius(), 0);
}

void tst_BeautifySettings::testLoadCornerRadius_ClampMax()
{
    auto settings = SnapTray::getSettings();
    settings.setValue("beautify/cornerRadius", 100);
    settings.sync();
    QCOMPARE(BeautifySettingsManager::instance().loadCornerRadius(), 40);
}

// ============================================================================
// ShadowEnabled Tests
// ============================================================================

void tst_BeautifySettings::testLoadShadowEnabled_DefaultValue()
{
    QCOMPARE(BeautifySettingsManager::instance().loadShadowEnabled(), true);
}

void tst_BeautifySettings::testSaveLoadShadowEnabled_Roundtrip()
{
    auto& mgr = BeautifySettingsManager::instance();
    mgr.saveShadowEnabled(false);
    QCOMPARE(mgr.loadShadowEnabled(), false);
    mgr.saveShadowEnabled(true);
    QCOMPARE(mgr.loadShadowEnabled(), true);
}

// ============================================================================
// ShadowBlur Tests
// ============================================================================

void tst_BeautifySettings::testLoadShadowBlur_DefaultValue()
{
    QCOMPARE(BeautifySettingsManager::instance().loadShadowBlur(), 40);
}

void tst_BeautifySettings::testSaveLoadShadowBlur_Roundtrip()
{
    auto& mgr = BeautifySettingsManager::instance();
    mgr.saveShadowBlur(60);
    QCOMPARE(mgr.loadShadowBlur(), 60);
}

void tst_BeautifySettings::testLoadShadowBlur_ClampMin()
{
    auto settings = SnapTray::getSettings();
    settings.setValue("beautify/shadowBlur", -10);
    settings.sync();
    QCOMPARE(BeautifySettingsManager::instance().loadShadowBlur(), 0);
}

void tst_BeautifySettings::testLoadShadowBlur_ClampMax()
{
    auto settings = SnapTray::getSettings();
    settings.setValue("beautify/shadowBlur", 200);
    settings.sync();
    QCOMPARE(BeautifySettingsManager::instance().loadShadowBlur(), 100);
}

// ============================================================================
// ShadowOffset Tests
// ============================================================================

void tst_BeautifySettings::testLoadShadowOffsetX_DefaultValue()
{
    QCOMPARE(BeautifySettingsManager::instance().loadShadowOffsetX(), 0);
}

void tst_BeautifySettings::testLoadShadowOffsetY_DefaultValue()
{
    QCOMPARE(BeautifySettingsManager::instance().loadShadowOffsetY(), 8);
}

void tst_BeautifySettings::testSaveLoadShadowOffset_Roundtrip()
{
    auto& mgr = BeautifySettingsManager::instance();
    mgr.saveShadowOffsetX(10);
    QCOMPARE(mgr.loadShadowOffsetX(), 10);
    mgr.saveShadowOffsetY(-5);
    QCOMPARE(mgr.loadShadowOffsetY(), -5);
}

void tst_BeautifySettings::testLoadShadowOffset_ClampMin()
{
    auto settings = SnapTray::getSettings();
    settings.setValue("beautify/shadowOffsetX", -100);
    settings.setValue("beautify/shadowOffsetY", -100);
    settings.sync();
    QCOMPARE(BeautifySettingsManager::instance().loadShadowOffsetX(), -50);
    QCOMPARE(BeautifySettingsManager::instance().loadShadowOffsetY(), -50);
}

void tst_BeautifySettings::testLoadShadowOffset_ClampMax()
{
    auto settings = SnapTray::getSettings();
    settings.setValue("beautify/shadowOffsetX", 100);
    settings.setValue("beautify/shadowOffsetY", 100);
    settings.sync();
    QCOMPARE(BeautifySettingsManager::instance().loadShadowOffsetX(), 50);
    QCOMPARE(BeautifySettingsManager::instance().loadShadowOffsetY(), 50);
}

// ============================================================================
// AspectRatio Tests
// ============================================================================

void tst_BeautifySettings::testLoadAspectRatio_DefaultValue()
{
    QCOMPARE(BeautifySettingsManager::instance().loadAspectRatio(), BeautifyAspectRatio::Auto);
}

void tst_BeautifySettings::testSaveLoadAspectRatio_Roundtrip()
{
    auto& mgr = BeautifySettingsManager::instance();
    mgr.saveAspectRatio(BeautifyAspectRatio::Wide_16_9);
    QCOMPARE(mgr.loadAspectRatio(), BeautifyAspectRatio::Wide_16_9);

    mgr.saveAspectRatio(BeautifyAspectRatio::Square_1_1);
    QCOMPARE(mgr.loadAspectRatio(), BeautifyAspectRatio::Square_1_1);
}

void tst_BeautifySettings::testLoadAspectRatio_InvalidValue()
{
    auto settings = SnapTray::getSettings();
    settings.setValue("beautify/aspectRatio", 99);
    settings.sync();
    QCOMPARE(BeautifySettingsManager::instance().loadAspectRatio(), BeautifyAspectRatio::Auto);
}

// ============================================================================
// Full Settings Roundtrip
// ============================================================================

void tst_BeautifySettings::testSaveLoadSettings_Roundtrip()
{
    auto& mgr = BeautifySettingsManager::instance();

    BeautifySettings custom;
    custom.backgroundType = BeautifyBackgroundType::RadialGradient;
    custom.backgroundColor = QColor(255, 0, 0);
    custom.gradientStartColor = QColor(0, 255, 0);
    custom.gradientEndColor = QColor(0, 0, 255);
    custom.gradientAngle = 45;
    custom.padding = 100;
    custom.cornerRadius = 20;
    custom.shadowEnabled = false;
    custom.shadowBlur = 60;
    custom.shadowOffsetX = 10;
    custom.shadowOffsetY = -10;
    custom.shadowColor = QColor(0, 0, 0, 120);
    custom.aspectRatio = BeautifyAspectRatio::Wide_16_9;

    mgr.saveSettings(custom);
    BeautifySettings loaded = mgr.loadSettings();

    QCOMPARE(loaded.backgroundType, custom.backgroundType);
    QCOMPARE(loaded.backgroundColor, custom.backgroundColor);
    QCOMPARE(loaded.gradientStartColor, custom.gradientStartColor);
    QCOMPARE(loaded.gradientEndColor, custom.gradientEndColor);
    QCOMPARE(loaded.gradientAngle, custom.gradientAngle);
    QCOMPARE(loaded.padding, custom.padding);
    QCOMPARE(loaded.cornerRadius, custom.cornerRadius);
    QCOMPARE(loaded.shadowEnabled, custom.shadowEnabled);
    QCOMPARE(loaded.shadowBlur, custom.shadowBlur);
    QCOMPARE(loaded.shadowOffsetX, custom.shadowOffsetX);
    QCOMPARE(loaded.shadowOffsetY, custom.shadowOffsetY);
    QCOMPARE(loaded.shadowColor, custom.shadowColor);
    QCOMPARE(loaded.aspectRatio, custom.aspectRatio);
}

QTEST_MAIN(tst_BeautifySettings)
#include "tst_BeautifySettings.moc"
