#include <QtTest/QtTest>

#include "qml/BeautifyPanelBackend.h"
#include "settings/BeautifySettingsManager.h"
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
    settings.remove(QStringLiteral("beautify/customStyles"));
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
    void testCustomStyle_SaveApplyDelete();
    void testCustomStyle_SameNameUpdates();
    void testCustomStyle_LimitAndNames();
    void testCustomStyle_ManagerClampsBadData();
    void testCustomStyle_Rename();
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

void tst_BeautifyPanelBackend::testCustomStyle_SaveApplyDelete()
{
    // #227: a saved style keeps the whole look and survives a new panel.
    {
        SnapTray::BeautifyPanelBackend backend;
        QCOMPARE(backend.customStyleModel().size(), 0);
        QCOMPARE(backend.currentCustomStyle(), -1);
        backend.setBackgroundType(static_cast<int>(BeautifyBackgroundType::Solid));
        backend.setPadding(90);
        backend.setAspectRatio(static_cast<int>(BeautifyAspectRatio::Square_1_1));
        QSignalSpy changed(&backend, &SnapTray::BeautifyPanelBackend::customStylesChanged);
        QVERIFY(backend.saveCustomStyle(QStringLiteral("  Docs  ")));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(backend.currentCustomStyle(), 0);
        QCOMPARE(backend.customStyleModel().at(0).toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("Docs"));
        QVERIFY(!backend.saveCustomStyle(QStringLiteral("   ")));
    }

    SnapTray::BeautifyPanelBackend backend; // loads the last look and the saved style
    QCOMPARE(backend.customStyleModel().size(), 1);
    backend.applyStyle(1); // Standard: no longer the saved look
    backend.setBackgroundType(static_cast<int>(BeautifyBackgroundType::LinearGradient));
    QCOMPARE(backend.currentCustomStyle(), -1);

    backend.applyCustomStyle(0);
    QCOMPARE(backend.currentCustomStyle(), 0);
    QCOMPARE(backend.padding(), 90);
    QCOMPARE(backend.backgroundType(), static_cast<int>(BeautifyBackgroundType::Solid));
    QCOMPARE(backend.aspectRatio(), static_cast<int>(BeautifyAspectRatio::Square_1_1));

    backend.deleteCustomStyle(5); // out of range: nothing
    QCOMPARE(backend.customStyleModel().size(), 1);
    backend.deleteCustomStyle(0);
    QCOMPARE(backend.customStyleModel().size(), 0);
    QCOMPARE(backend.currentCustomStyle(), -1);
    QVERIFY(BeautifySettingsManager::instance().loadCustomStyles().isEmpty());
}

void tst_BeautifyPanelBackend::testCustomStyle_SameNameUpdates()
{
    SnapTray::BeautifyPanelBackend backend;
    backend.setPadding(40);
    QVERIFY(backend.saveCustomStyle(QStringLiteral("Blog")));
    backend.setPadding(150);
    QVERIFY(backend.saveCustomStyle(QStringLiteral("blog"))); // same name, any case: update
    QCOMPARE(backend.customStyleModel().size(), 1);
    QCOMPARE(backend.customStyleModel().at(0).toMap().value(QStringLiteral("padding")).toInt(), 150);
    QCOMPARE(backend.currentCustomStyle(), 0);
}

void tst_BeautifyPanelBackend::testCustomStyle_LimitAndNames()
{
    SnapTray::BeautifyPanelBackend backend;
    QCOMPARE(backend.suggestedCustomStyleName(), QStringLiteral("My style 1"));
    for (int i = 0; i < backend.maxCustomStyles(); ++i) {
        backend.setPadding(20 + i);
        QVERIFY(backend.saveCustomStyle(backend.suggestedCustomStyleName()));
    }
    QVERIFY(backend.customStylesFull());
    QCOMPARE(backend.suggestedCustomStyleName(), QStringLiteral("My style %1").arg(backend.maxCustomStyles() + 1));
    backend.setPadding(199);
    QVERIFY(!backend.saveCustomStyle(QStringLiteral("One too many")));
    QCOMPARE(backend.customStyleModel().size(), backend.maxCustomStyles());
    backend.deleteCustomStyle(0);
    QVERIFY(!backend.customStylesFull());
    QCOMPARE(backend.suggestedCustomStyleName(), QStringLiteral("My style 1")); // the freed name
}

void tst_BeautifyPanelBackend::testCustomStyle_ManagerClampsBadData()
{
    auto settings = SnapTray::getSettings();
    settings.setValue(QStringLiteral("beautify/customStyles"),
                      QStringLiteral(R"([{"name":"Wild","padding":9999,"cornerRadius":-3,"backgroundType":7,)"
                                     R"("gradientStartColor":"not a colour","aspectRatio":42},{"name":"   "},"junk"])"));
    settings.sync();
    const auto styles = BeautifySettingsManager::instance().loadCustomStyles();
    QCOMPARE(styles.size(), 1);
    QCOMPARE(styles.at(0).name, QStringLiteral("Wild"));
    QCOMPARE(styles.at(0).look.padding, 200);
    QCOMPARE(styles.at(0).look.cornerRadius, 0);
    QCOMPARE(styles.at(0).look.backgroundType, BeautifyBackgroundType::RadialGradient);
    QCOMPARE(styles.at(0).look.gradientStartColor, BeautifySettings{}.gradientStartColor);
    QCOMPARE(styles.at(0).look.aspectRatio, BeautifyAspectRatio::Twitter_2_1);

    settings.setValue(QStringLiteral("beautify/customStyles"), QStringLiteral("{broken"));
    settings.sync();
    QVERIFY(BeautifySettingsManager::instance().loadCustomStyles().isEmpty());
}

void tst_BeautifyPanelBackend::testCustomStyle_Rename()
{
    {
        SnapTray::BeautifyPanelBackend backend;
        backend.setPadding(40);
        QVERIFY(backend.saveCustomStyle(QStringLiteral("Blog")));
        backend.setPadding(150);
        QVERIFY(backend.saveCustomStyle(QStringLiteral("Slides")));
        QSignalSpy changed(&backend, &SnapTray::BeautifyPanelBackend::customStylesChanged);

        QVERIFY(backend.renameCustomStyle(0, QStringLiteral("  Blog posts ")));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(backend.customStyleModel().at(0).toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("Blog posts"));
        // The look is untouched: still Blog's padding.
        QCOMPARE(backend.customStyleModel().at(0).toMap().value(QStringLiteral("padding")).toInt(), 40);

        QVERIFY(!backend.renameCustomStyle(0, QStringLiteral("slides"))); // another style's name, any case
        QVERIFY(backend.customStyleNameTaken(QStringLiteral("SLIDES"), 0));
        QVERIFY(!backend.customStyleNameTaken(QStringLiteral("slides"), 1)); // its own name
        QVERIFY(!backend.renameCustomStyle(0, QStringLiteral("   ")));
        QVERIFY(!backend.renameCustomStyle(7, QStringLiteral("Nope")));
        QVERIFY(backend.renameCustomStyle(1, QStringLiteral("SLIDES"))); // only the case changes
        QCOMPARE(changed.count(), 2);
        QVERIFY(backend.renameCustomStyle(1, QStringLiteral("SLIDES"))); // unchanged: no write
        QCOMPARE(changed.count(), 2);
    }
    const auto stored = BeautifySettingsManager::instance().loadCustomStyles();
    QCOMPARE(stored.size(), 2);
    QCOMPARE(stored.at(0).name, QStringLiteral("Blog posts"));
    QCOMPARE(stored.at(1).name, QStringLiteral("SLIDES"));
}

QTEST_MAIN(tst_BeautifyPanelBackend)
#include "tst_BeautifyPanelBackend.moc"
