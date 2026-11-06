#include <QtTest/QtTest>

#include "beautify/FrameStyleLibrary.h"
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
    settings.remove(QStringLiteral("beautify/borderEnabled"));
    settings.remove(QStringLiteral("beautify/borderWidth"));
    settings.remove(QStringLiteral("beautify/borderColor"));
    settings.remove(QStringLiteral("beautify/customStyles"));
    settings.remove(QStringLiteral("beautify/frameStyles"));
    settings.sync();
    SnapTray::FrameStyleLibrary::instance().reload();
}

int storedPadding(const SnapTray::NamedFrameStyle& style)
{
    return style.style.toBeautify(SnapTray::FrameStyle::kReferenceShortSide).padding;
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
    void testCustomStyle_PanelsStayInSync();
    void testCustomStyle_RelativeToScreenshotSize();
    void testCustomStyle_SavedInRecordingEditorApplies();
    void testCustomStyle_BorderWidthAndColorMatch();
    void testBorder_SavedWithTheLook();
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

    // Turning the shadow off on a shadowed style is Custom.
    backend.applyStyle(1);
    backend.setShadowEnabled(false);
    QCOMPARE(backend.currentStyle(), -1);

    // Flat ignores the blur value, which is disabled while the shadow is off.
    backend.applyStyle(3);
    backend.setShadowBlur(90);
    QCOMPARE(backend.currentStyle(), 3);
    // Turning the shadow on for Flat is Custom too.
    backend.setShadowEnabled(true);
    QCOMPARE(backend.currentStyle(), -1);
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
    QVERIFY(BeautifySettingsManager::instance().loadFrameStyles().isEmpty());
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
    // Styles from before #269 are clamped like the single settings, then converted.
    const auto styles = BeautifySettingsManager::instance().loadFrameStyles();
    QCOMPARE(styles.size(), 1);
    QCOMPARE(styles.at(0).name, QStringLiteral("Wild"));
    const BeautifySettings look = styles.at(0).style.toBeautify(SnapTray::FrameStyle::kReferenceShortSide);
    QCOMPARE(look.padding, 200);
    QCOMPARE(look.cornerRadius, 0);
    QCOMPARE(look.backgroundType, BeautifyBackgroundType::RadialGradient);
    QCOMPARE(look.gradientStartColor, BeautifySettings{}.gradientStartColor);
    QCOMPARE(look.aspectRatio, BeautifyAspectRatio::Twitter_2_1);

    settings.setValue(QStringLiteral("beautify/customStyles"), QStringLiteral("{broken"));
    settings.sync();
    QVERIFY(BeautifySettingsManager::instance().loadFrameStyles().isEmpty());

    // The shared format keeps sizes within its limit and drops unnamed entries.
    settings.setValue(QStringLiteral("beautify/frameStyles"),
                      QStringLiteral(R"([{"name":"Huge","style":{"padding":5000,"cornerRadius":-1,"background":"nope",)"
                                     R"("aspectRatio":-4}},{"name":""},"junk"])"));
    settings.sync();
    const auto shared = BeautifySettingsManager::instance().loadFrameStyles();
    QCOMPARE(shared.size(), 1);
    QCOMPARE(shared.at(0).style.padding, SnapTray::FrameStyle::kMaxSizeFraction);
    QCOMPARE(shared.at(0).style.cornerRadius, 0.0);
    QCOMPARE(shared.at(0).style.background, SnapTray::FrameStyle::Background::LinearGradient);
    QCOMPARE(shared.at(0).style.aspectRatio, BeautifyAspectRatio::Auto);
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
    const auto stored = BeautifySettingsManager::instance().loadFrameStyles();
    QCOMPARE(stored.size(), 2);
    QCOMPARE(stored.at(0).name, QStringLiteral("Blog posts"));
    QCOMPARE(stored.at(1).name, QStringLiteral("SLIDES"));
}

void tst_BeautifyPanelBackend::testCustomStyle_PanelsStayInSync()
{
    // Each pin window keeps its own panel: a change in one must not be
    // overwritten by another panel's stale list.
    SnapTray::BeautifyPanelBackend first;
    SnapTray::BeautifyPanelBackend second;
    QSignalSpy secondChanged(&second, &SnapTray::BeautifyPanelBackend::customStylesChanged);

    QVERIFY(first.saveCustomStyle(QStringLiteral("Blog")));
    QCOMPARE(secondChanged.count(), 1);
    QCOMPARE(second.customStyleModel().size(), 1);

    QVERIFY(second.saveCustomStyle(QStringLiteral("Slides")));
    QCOMPARE(first.customStyleModel().size(), 2);
    QCOMPARE(BeautifySettingsManager::instance().loadFrameStyles().size(), 2);

    first.deleteCustomStyle(0);
    QVERIFY(second.renameCustomStyle(0, QStringLiteral("Talks"))); // index 0 is now Slides
    const auto stored = BeautifySettingsManager::instance().loadFrameStyles();
    QCOMPARE(stored.size(), 1);
    QCOMPARE(stored.at(0).name, QStringLiteral("Talks"));
}

void tst_BeautifyPanelBackend::testCustomStyle_RelativeToScreenshotSize()
{
    // #269: a style keeps sizes relative to the picture's shorter side.
    SnapTray::BeautifyPanelBackend small;
    QPixmap smallShot(1280, 720);
    smallShot.fill(Qt::white);
    small.setSourcePixmap(smallShot);
    small.setPadding(64);
    small.setCornerRadius(10);
    QVERIFY(small.saveCustomStyle(QStringLiteral("Docs")));
    QCOMPARE(small.currentCustomStyle(), 0);
    QCOMPARE(small.customStyleModel().at(0).toMap().value(QStringLiteral("padding")).toInt(), 64);
    // Stored at the reference size (1440): twice as large as on a 720 px picture.
    QCOMPARE(storedPadding(SnapTray::FrameStyleLibrary::instance().styles().at(0)), 128);

    SnapTray::BeautifyPanelBackend large;
    QPixmap largeShot(1440, 1440);
    largeShot.fill(Qt::white);
    large.setSourcePixmap(largeShot);
    large.applyCustomStyle(0);
    QCOMPARE(large.padding(), 128);
    QCOMPARE(large.cornerRadius(), 20);
    QCOMPARE(large.currentCustomStyle(), 0);

    // A 2x screenshot is measured in logical pixels.
    SnapTray::BeautifyPanelBackend retina;
    QPixmap retinaShot(2560, 1440);
    retinaShot.setDevicePixelRatio(2.0);
    retinaShot.fill(Qt::white);
    retina.setSourcePixmap(retinaShot);
    retina.applyCustomStyle(0);
    QCOMPARE(retina.padding(), 64);
}

void tst_BeautifyPanelBackend::testCustomStyle_SavedInRecordingEditorApplies()
{
    // A style saved from a recording's frame shows up and applies here.
    SnapTray::BeautifyPanelBackend backend;
    QSignalSpy changed(&backend, &SnapTray::BeautifyPanelBackend::customStylesChanged);
    SnapTray::FrameStyle style;
    style.background = SnapTray::FrameStyle::Background::Solid;
    style.solidColor = QColor(20, 30, 40);
    style.padding = 0.05;
    style.cornerRadius = 0.02;
    style.borderEnabled = true;
    QVERIFY(SnapTray::FrameStyleLibrary::instance().save(QStringLiteral("From recording"), style));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(backend.customStyleModel().size(), 1);

    QPixmap shot(1000, 800);
    shot.fill(Qt::white);
    backend.setSourcePixmap(shot);
    backend.applyCustomStyle(0);
    QCOMPARE(backend.backgroundType(), static_cast<int>(BeautifyBackgroundType::Solid));
    QCOMPARE(backend.primaryColor(), QColor(20, 30, 40));
    QCOMPARE(backend.padding(), 40);
    QCOMPARE(backend.cornerRadius(), 16);
    QVERIFY(backend.borderEnabled());
    QCOMPARE(backend.currentCustomStyle(), 0);
}

void tst_BeautifyPanelBackend::testBorder_SavedWithTheLook()
{
    {
        SnapTray::BeautifyPanelBackend backend;
        QVERIFY(!backend.borderEnabled());
        backend.setBorderEnabled(true);
        backend.requestSave(); // keeps the look for the next panel
    }
    SnapTray::BeautifyPanelBackend reopened;
    QVERIFY(reopened.borderEnabled());
    QVERIFY(reopened.saveCustomStyle(QStringLiteral("Bordered")));
    reopened.setBorderEnabled(false);
    QCOMPARE(reopened.currentCustomStyle(), -1); // the border is part of the look
}

QTEST_MAIN(tst_BeautifyPanelBackend)
void tst_BeautifyPanelBackend::testCustomStyle_BorderWidthAndColorMatch()
{
    // Two styles that differ only in their border draw differently, so the
    // one applied is the one selected.
    SnapTray::FrameStyle thin;
    thin.borderEnabled = true;
    thin.borderWidth = 2.0 / SnapTray::FrameStyle::kReferenceShortSide;
    thin.borderColor = QColor(255, 255, 255);
    SnapTray::FrameStyle red = thin;
    red.borderColor = QColor(255, 0, 0);
    SnapTray::FrameStyle wide = thin;
    wide.borderWidth = 6.0 / SnapTray::FrameStyle::kReferenceShortSide;
    auto& library = SnapTray::FrameStyleLibrary::instance();
    QVERIFY(library.save(QStringLiteral("Thin"), thin));
    QVERIFY(library.save(QStringLiteral("Red"), red));
    QVERIFY(library.save(QStringLiteral("Wide"), wide));

    SnapTray::BeautifyPanelBackend backend;
    for (int i = 0; i < 3; ++i) {
        backend.applyCustomStyle(i);
        QCOMPARE(backend.currentCustomStyle(), i);
    }
}

#include "tst_BeautifyPanelBackend.moc"
