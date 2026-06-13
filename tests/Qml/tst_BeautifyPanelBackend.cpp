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

QTEST_MAIN(tst_BeautifyPanelBackend)
#include "tst_BeautifyPanelBackend.moc"
