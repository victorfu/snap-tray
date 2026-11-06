#include <QtTest/QtTest>
#include <QSettings>
#include "beautify/BeautifyRenderer.h"
#include "beautify/BeautifySettings.h"
#include "beautify/FrameStyle.h"
#include <QJsonDocument>
#include <QPainter>
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
    void testSaveLoadBorder_Roundtrip();

    // Shared styles (#269)
    void testFrameStyle_PixelRoundTripAtSize();
    void testFrameStyle_JsonRoundTrip();
    void testFrameStyles_SaveLoad();
    void testFrameStyles_MigrateLegacyStyles();
    void testFrameStyles_MigratedLookUnchangedAt1440();
    void testFrameStyles_EmptyListDoesNotRestoreLegacy();
    void testFrameStyles_DamagedListKeptBeforeOverwrite();

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
    settings.remove("beautify/borderEnabled");
    settings.remove("beautify/borderWidth");
    settings.remove("beautify/borderColor");
    settings.remove("beautify/customStyles");
    settings.remove("beautify/frameStyles");
    settings.remove("beautify/frameStyles.corrupt");
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

void tst_BeautifySettings::testSaveLoadBorder_Roundtrip()
{
    BeautifySettings s;
    QVERIFY(!BeautifySettingsManager::instance().loadSettings().borderEnabled);
    s.borderEnabled = true;
    s.borderWidth = 5;
    s.borderColor = QColor(10, 20, 30, 200);
    BeautifySettingsManager::instance().saveSettings(s);
    const BeautifySettings loaded = BeautifySettingsManager::instance().loadSettings();
    QVERIFY(loaded.borderEnabled);
    QCOMPARE(loaded.borderWidth, 5);
    QCOMPARE(loaded.borderColor, QColor(10, 20, 30, 200));
}

namespace {

// A look from before #269, as the Beautify panel saved it.
BeautifySettings legacyLook()
{
    BeautifySettings s;
    s.backgroundType = BeautifyBackgroundType::Solid;
    s.backgroundColor = QColor(12, 34, 56);
    s.padding = 90;
    s.cornerRadius = 17;
    s.shadowEnabled = true;
    s.shadowBlur = 33;
    s.shadowOffsetX = -7;
    s.shadowOffsetY = 11;
    s.shadowColor = QColor(0, 0, 0, 120);
    s.aspectRatio = BeautifyAspectRatio::Wide_16_9;
    return s;
}

QString legacyStylesJson(const BeautifySettings& s)
{
    const QJsonArray array{QJsonObject{
        {QStringLiteral("name"), QStringLiteral("Docs")},
        {QStringLiteral("backgroundType"), static_cast<int>(s.backgroundType)},
        {QStringLiteral("backgroundColor"), s.backgroundColor.name(QColor::HexArgb)},
        {QStringLiteral("gradientStartColor"), s.gradientStartColor.name(QColor::HexArgb)},
        {QStringLiteral("gradientEndColor"), s.gradientEndColor.name(QColor::HexArgb)},
        {QStringLiteral("gradientAngle"), s.gradientAngle},
        {QStringLiteral("padding"), s.padding},
        {QStringLiteral("cornerRadius"), s.cornerRadius},
        {QStringLiteral("shadowEnabled"), s.shadowEnabled},
        {QStringLiteral("shadowBlur"), s.shadowBlur},
        {QStringLiteral("shadowOffsetX"), s.shadowOffsetX},
        {QStringLiteral("shadowOffsetY"), s.shadowOffsetY},
        {QStringLiteral("shadowColor"), s.shadowColor.name(QColor::HexArgb)},
        {QStringLiteral("aspectRatio"), static_cast<int>(s.aspectRatio)},
    }};
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

void storeLegacyStyles(const BeautifySettings& s)
{
    auto settings = SnapTray::getSettings();
    settings.setValue("beautify/customStyles", legacyStylesJson(s));
    settings.sync();
}

void comparePixels(const BeautifySettings& a, const BeautifySettings& b)
{
    QCOMPARE(a.backgroundType, b.backgroundType);
    QCOMPARE(a.backgroundColor, b.backgroundColor);
    QCOMPARE(a.gradientStartColor, b.gradientStartColor);
    QCOMPARE(a.gradientEndColor, b.gradientEndColor);
    QCOMPARE(a.gradientAngle, b.gradientAngle);
    QCOMPARE(a.padding, b.padding);
    QCOMPARE(a.cornerRadius, b.cornerRadius);
    QCOMPARE(a.shadowEnabled, b.shadowEnabled);
    QCOMPARE(a.shadowBlur, b.shadowBlur);
    QCOMPARE(a.shadowOffsetX, b.shadowOffsetX);
    QCOMPARE(a.shadowOffsetY, b.shadowOffsetY);
    QCOMPARE(a.shadowColor, b.shadowColor);
    QCOMPARE(a.aspectRatio, b.aspectRatio);
}

} // namespace

void tst_BeautifySettings::testFrameStyle_PixelRoundTripAtSize()
{
    // Saved from a picture and applied to one of the same size: the same pixels.
    for (const int side : {200, 720, 1080, 1440, 2160}) {
        const BeautifySettings look = legacyLook();
        comparePixels(SnapTray::FrameStyle::fromBeautify(look, side).toBeautify(side), look);
    }
    // Twice the picture, twice the sizes.
    const SnapTray::FrameStyle style = SnapTray::FrameStyle::fromBeautify(legacyLook(), 720);
    QCOMPARE(style.toBeautify(1440).padding, 180);
    QCOMPARE(style.toBeautify(1440).shadowOffsetX, -14);
}

void tst_BeautifySettings::testFrameStyle_JsonRoundTrip()
{
    SnapTray::FrameStyle style;
    style.background = SnapTray::FrameStyle::Background::RadialGradient;
    style.gradientStart = QColor(1, 2, 3, 4);
    style.gradientAngle = 45;
    style.padding = 0.123456;
    style.shadowOffsetX = -0.01;
    style.borderEnabled = true;
    style.borderWidth = 0.002;
    style.borderColor = QColor(9, 8, 7, 6);
    style.aspectRatio = BeautifyAspectRatio::Tall_9_16;
    style.deviceFrame = QStringLiteral("browser"); // reserved: kept as is
    QCOMPARE(SnapTray::FrameStyle::fromJson(style.toJson()), style);
    // Missing keys keep the defaults.
    QCOMPARE(SnapTray::FrameStyle::fromJson(QJsonObject()), SnapTray::FrameStyle());

    // Saved from a tiny screenshot, padding is wider than the picture
    // (100x40 with 64 px padding is 1.6) and still reads back exactly.
    BeautifySettings tiny;
    tiny.padding = 64;
    const SnapTray::FrameStyle small = SnapTray::FrameStyle::fromBeautify(tiny, 40);
    QCOMPARE(SnapTray::FrameStyle::fromJson(small.toJson()), small);
    QCOMPARE(SnapTray::FrameStyle::fromJson(small.toJson()).toBeautify(40).padding, 64);
    // Damaged values are still kept within bounds.
    const auto damaged = SnapTray::FrameStyle::fromJson({{QStringLiteral("padding"), 1e9}});
    QCOMPARE(damaged.padding, SnapTray::FrameStyle::kMaxSizeFraction);
}

void tst_BeautifySettings::testFrameStyles_SaveLoad()
{
    QVERIFY(BeautifySettingsManager::instance().loadFrameStyles().isEmpty());
    SnapTray::FrameStyle style;
    style.padding = 0.07;
    BeautifySettingsManager::instance().saveFrameStyles({{QStringLiteral(" Blog "), style},
                                                        {QStringLiteral("   "), style}});
    const auto loaded = BeautifySettingsManager::instance().loadFrameStyles();
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.at(0).name, QStringLiteral("Blog"));
    QCOMPARE(loaded.at(0).style, style);
}

void tst_BeautifySettings::testFrameStyles_MigrateLegacyStyles()
{
    storeLegacyStyles(legacyLook());
    const auto styles = BeautifySettingsManager::instance().loadFrameStyles();
    QCOMPARE(styles.size(), 1);
    QCOMPARE(styles.at(0).name, QStringLiteral("Docs"));
    // At the reference size the pixel values come back exactly.
    comparePixels(styles.at(0).style.toBeautify(SnapTray::FrameStyle::kReferenceShortSide), legacyLook());
    QCOMPARE(styles.at(0).style.padding, 90.0 / 1440.0);

    // Saving writes the shared format; the old key is left for older versions.
    BeautifySettingsManager::instance().saveFrameStyles(styles);
    auto settings = SnapTray::getSettings();
    QVERIFY(settings.contains("beautify/frameStyles"));
    QCOMPARE(settings.value("beautify/customStyles").toString(), legacyStylesJson(legacyLook()));
    QCOMPARE(BeautifySettingsManager::instance().loadFrameStyles().at(0).style, styles.at(0).style);
}

void tst_BeautifySettings::testFrameStyles_MigratedLookUnchangedAt1440()
{
    // Acceptance (#269): within 1 px of the old look on a 1440 px short side.
    const BeautifySettings legacy = legacyLook();
    storeLegacyStyles(legacy);
    const BeautifySettings migrated = BeautifySettingsManager::instance().loadFrameStyles().at(0).style.toBeautify(
        SnapTray::FrameStyle::kReferenceShortSide);

    QImage picture(1920, 1440, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&picture);
    painter.fillRect(picture.rect(), QColor(200, 60, 40));
    painter.fillRect(QRect(300, 200, 800, 600), QColor(30, 160, 220));
    painter.end();
    const QPixmap source = QPixmap::fromImage(picture);

    const QImage before = BeautifyRenderer::applyToPixmap(source, legacy).toImage();
    const QImage after = BeautifyRenderer::applyToPixmap(source, migrated).toImage();
    QCOMPARE(after.size(), before.size());
    QCOMPARE(after, before);
}

void tst_BeautifySettings::testFrameStyles_DamagedListKeptBeforeOverwrite()
{
    auto& manager = BeautifySettingsManager::instance();
    auto settings = SnapTray::getSettings();
    const QString damaged = QStringLiteral(R"([{"name":"Blog","style":{"padding":0.1)");
    for (const QString& text : {damaged, QStringLiteral(R"({"name":"Blog"})")}) {
        settings.setValue("beautify/frameStyles", text);
        settings.remove("beautify/frameStyles.corrupt");
        settings.sync();
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("ignoring damaged.*beautify/frameStyles"));
        QVERIFY(manager.loadFrameStyles().isEmpty());

        // Saving after that keeps the damaged text instead of losing it.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("ignoring damaged.*beautify/frameStyles"));
        manager.saveFrameStyles({{QStringLiteral("New"), SnapTray::FrameStyle()}});
        QCOMPARE(settings.value("beautify/frameStyles.corrupt").toString(), text);
        QCOMPARE(manager.loadFrameStyles().size(), 1);
        // A save over a valid list leaves the copy alone.
        manager.saveFrameStyles({});
        QCOMPARE(settings.value("beautify/frameStyles.corrupt").toString(), text);
    }

    // An entry without its look is skipped, not read as the default look.
    settings.setValue("beautify/frameStyles",
                      QStringLiteral(R"([{"name":"Lost"},{"name":"Kept","style":{"padding":0.1}}])"));
    settings.sync();
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("skipping saved style.*Lost"));
    const auto styles = manager.loadFrameStyles();
    QCOMPARE(styles.size(), 1);
    QCOMPARE(styles.at(0).name, QStringLiteral("Kept"));
}

void tst_BeautifySettings::testFrameStyles_EmptyListDoesNotRestoreLegacy()
{
    storeLegacyStyles(legacyLook());
    QCOMPARE(BeautifySettingsManager::instance().loadFrameStyles().size(), 1);
    BeautifySettingsManager::instance().saveFrameStyles({}); // the user deleted it
    QVERIFY(BeautifySettingsManager::instance().loadFrameStyles().isEmpty());
}

QTEST_MAIN(tst_BeautifySettings)
#include "tst_BeautifySettings.moc"
