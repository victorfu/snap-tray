#include <QtTest/QtTest>

#include "qml/BeautifyPanelBackend.h"
#include "settings/Settings.h"

#include <QSignalSpy>

namespace {

void clearBeautifySettings()
{
    auto settings = SnapTray::getSettings();
    settings.remove(QStringLiteral("beautify/backgroundType"));
    settings.remove(QStringLiteral("beautify/backgroundColor"));
    settings.remove(QStringLiteral("beautify/gradientStartColor"));
    settings.remove(QStringLiteral("beautify/gradientEndColor"));
    settings.remove(QStringLiteral("beautify/gradientAngle"));
    settings.remove(QStringLiteral("beautify/padding"));
    settings.remove(QStringLiteral("beautify/cornerRadius"));
    settings.remove(QStringLiteral("beautify/shadowEnabled"));
    settings.remove(QStringLiteral("beautify/shadowBlur"));
    settings.remove(QStringLiteral("beautify/shadowOffsetX"));
    settings.remove(QStringLiteral("beautify/shadowOffsetY"));
    settings.remove(QStringLiteral("beautify/shadowColor"));
    settings.remove(QStringLiteral("beautify/aspectRatio"));
    settings.sync();
}

} // namespace

class tst_BeautifyPanelBackend : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testDefaultState_UsesBeautifyDefaults();
    void testApplyPreset_UpdatesGradientState();
    void testPreviewDebounce_CoalescesRapidChanges();
    void testRequestClose_EmitsSignal();
    void testStyles_DefaultIsStandard();
    void testApplyStyle_SetsInsetOnly();
    void testCurrentStyle_CustomWhenNoneMatch();
    void testCurrentPreset_FollowsBackground();
    void testChosenStateSurvivesSaveAndReload();
};

void tst_BeautifyPanelBackend::init()
{
    clearBeautifySettings();
}

void tst_BeautifyPanelBackend::cleanup()
{
    clearBeautifySettings();
}

void tst_BeautifyPanelBackend::testDefaultState_UsesBeautifyDefaults()
{
    SnapTray::BeautifyPanelBackend backend;
    QTest::qWait(60);

    QCOMPARE(backend.backgroundType(), static_cast<int>(BeautifyBackgroundType::LinearGradient));
    QCOMPARE(backend.primaryColor(), QColor(0, 180, 219));
    QCOMPARE(backend.secondaryColor(), QColor(0, 131, 176));
    QVERIFY(backend.secondaryColorVisible());
    QCOMPARE(backend.padding(), 64);
    QCOMPARE(backend.cornerRadius(), 12);
    QCOMPARE(backend.aspectRatio(), static_cast<int>(BeautifyAspectRatio::Auto));
    QVERIFY(backend.shadowEnabled());
    QCOMPARE(backend.shadowBlur(), 40);
    QCOMPARE(backend.backgroundTypeModel().size(), 3);
    QCOMPARE(backend.aspectRatioModel().size(), 6);
    QCOMPARE(backend.presetModel().size(), 8);
}

void tst_BeautifyPanelBackend::testApplyPreset_UpdatesGradientState()
{
    SnapTray::BeautifyPanelBackend backend;
    QTest::qWait(60);

    QSignalSpy settingsSpy(&backend, &SnapTray::BeautifyPanelBackend::settingsStateChanged);
    backend.applyPreset(1);

    QCOMPARE(backend.backgroundType(), static_cast<int>(BeautifyBackgroundType::LinearGradient));
    QCOMPARE(backend.primaryColor(), QColor(255, 154, 0));
    QCOMPARE(backend.secondaryColor(), QColor(255, 87, 51));
    QVERIFY(settingsSpy.count() >= 1);

    const QVariantMap preset = backend.presetModel().at(1).toMap();
    QCOMPARE(preset.value(QStringLiteral("name")).toString(), QStringLiteral("Sunset"));
}

void tst_BeautifyPanelBackend::testPreviewDebounce_CoalescesRapidChanges()
{
    SnapTray::BeautifyPanelBackend backend;
    QSignalSpy previewSpy(&backend, &SnapTray::BeautifyPanelBackend::previewRevisionChanged);

    QTRY_VERIFY(previewSpy.count() >= 1);
    previewSpy.clear();

    backend.setPadding(80);
    backend.setCornerRadius(24);
    backend.setShadowBlur(28);

    QTRY_COMPARE(previewSpy.count(), 1);
    QTest::qWait(80);
    QCOMPARE(previewSpy.count(), 1);
}

void tst_BeautifyPanelBackend::testRequestClose_EmitsSignal()
{
    SnapTray::BeautifyPanelBackend backend;
    QSignalSpy closeSpy(&backend, &SnapTray::BeautifyPanelBackend::closeRequested);
    QVERIFY(closeSpy.isValid());

    backend.requestClose();

    QCOMPARE(closeSpy.count(), 1);
}

void tst_BeautifyPanelBackend::testStyles_DefaultIsStandard()
{
    // #227: the default look is the "Standard" style, so a fresh panel shows it chosen.
    SnapTray::BeautifyPanelBackend backend;
    const QVariantList styles = backend.styleModel();
    QCOMPARE(styles.size(), 4);
    QStringList names;
    for (const QVariant& style : styles) {
        names << style.toMap().value(QStringLiteral("name")).toString();
    }
    QCOMPARE(names, (QStringList{QStringLiteral("Compact"), QStringLiteral("Standard"),
                                 QStringLiteral("Spacious"), QStringLiteral("Flat")}));
    QCOMPARE(backend.currentStyle(), 1);
    QCOMPARE(backend.currentPreset(), 0); // Ocean, the default gradient
}

void tst_BeautifyPanelBackend::testApplyStyle_SetsInsetOnly()
{
    SnapTray::BeautifyPanelBackend backend;
    backend.applyPreset(4); // Midnight
    backend.setAspectRatio(static_cast<int>(BeautifyAspectRatio::Wide_16_9));
    QSignalSpy settingsSpy(&backend, &SnapTray::BeautifyPanelBackend::settingsStateChanged);

    backend.applyStyle(2); // Spacious
    QCOMPARE(settingsSpy.count(), 1);
    QCOMPARE(backend.padding(), 128);
    QCOMPARE(backend.cornerRadius(), 16);
    QVERIFY(backend.shadowEnabled());
    QCOMPARE(backend.shadowBlur(), 60);
    QCOMPARE(backend.currentStyle(), 2);
    // Background and ratio stay as they were.
    QCOMPARE(backend.currentPreset(), 4);
    QCOMPARE(backend.aspectRatio(), static_cast<int>(BeautifyAspectRatio::Wide_16_9));

    backend.applyStyle(3); // Flat: no shadow, square corners
    QCOMPARE(backend.cornerRadius(), 0);
    QVERIFY(!backend.shadowEnabled());
    QCOMPARE(backend.currentStyle(), 3);

    backend.applyStyle(3); // already chosen: no change signal
    QCOMPARE(settingsSpy.count(), 2);
    backend.applyStyle(-1);
    backend.applyStyle(4);
    QCOMPARE(settingsSpy.count(), 2);
    QCOMPARE(backend.currentStyle(), 3);
}

void tst_BeautifyPanelBackend::testCurrentStyle_CustomWhenNoneMatch()
{
    SnapTray::BeautifyPanelBackend backend;
    backend.applyStyle(0);
    QCOMPARE(backend.currentStyle(), 0);
    backend.setPadding(33);
    QCOMPARE(backend.currentStyle(), -1);
    backend.setPadding(32);
    QCOMPARE(backend.currentStyle(), 0);
    backend.setShadowBlur(25);
    QCOMPARE(backend.currentStyle(), -1);

    // Flat ignores the blur value, which is hidden while the shadow is off.
    backend.applyStyle(3);
    backend.setShadowBlur(90);
    QCOMPARE(backend.currentStyle(), 3);
}

void tst_BeautifyPanelBackend::testCurrentPreset_FollowsBackground()
{
    SnapTray::BeautifyPanelBackend backend;
    backend.applyPreset(6);
    QCOMPARE(backend.currentPreset(), 6);
    backend.setBackgroundType(static_cast<int>(BeautifyBackgroundType::RadialGradient));
    QCOMPARE(backend.currentPreset(), -1); // same colours, not the linear swatch
    backend.setBackgroundType(static_cast<int>(BeautifyBackgroundType::Solid));
    QCOMPARE(backend.currentPreset(), -1);
    backend.applyPreset(2);
    QCOMPARE(backend.backgroundType(), static_cast<int>(BeautifyBackgroundType::LinearGradient));
    QCOMPARE(backend.currentPreset(), 2);
}

void tst_BeautifyPanelBackend::testChosenStateSurvivesSaveAndReload()
{
    // Copy/Save store the look; the next panel must still show it chosen.
    {
        SnapTray::BeautifyPanelBackend backend;
        backend.applyPreset(5);
        backend.applyStyle(0);
        backend.requestCopy();
    }
    SnapTray::BeautifyPanelBackend reopened;
    QCOMPARE(reopened.currentPreset(), 5);
    QCOMPARE(reopened.currentStyle(), 0);
}

QTEST_MAIN(tst_BeautifyPanelBackend)
#include "tst_BeautifyPanelBackend.moc"
