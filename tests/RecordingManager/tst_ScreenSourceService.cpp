#include <QtTest/QtTest>

#include "capture/ICaptureEngine.h"
#include "recording/ScreenSourceService.h"

#include <QGuiApplication>
#include <QScreen>

class TestRecordingScreenSourceService : public QObject
{
    Q_OBJECT

private slots:
    void testAvailableSourcesMatchScreens();
    void testAvailableSourcesPopulateRequiredFields();
    void testResolutionTextUsesDevicePixelRatio();
    void testResolutionTextIsTheScreensOwnPixels();
};

void TestRecordingScreenSourceService::testAvailableSourcesMatchScreens()
{
    const QList<QScreen*> screens = QGuiApplication::screens();
    if (screens.isEmpty()) {
        QSKIP("No screens available in the test environment.");
    }

    const QVector<SnapTray::ScreenSource> sources =
        SnapTray::ScreenSourceService::availableSources(false);

    QCOMPARE(sources.size(), screens.size());
}

void TestRecordingScreenSourceService::testAvailableSourcesPopulateRequiredFields()
{
    const QVector<SnapTray::ScreenSource> sources =
        SnapTray::ScreenSourceService::availableSources(false);

    if (sources.isEmpty()) {
        QSKIP("No screens available in the test environment.");
    }

    for (const SnapTray::ScreenSource& source : sources) {
        QVERIFY2(!source.id.trimmed().isEmpty(), "Expected a stable screen id.");
        QVERIFY2(!source.name.trimmed().isEmpty(), "Expected a non-empty display name.");
        QVERIFY2(source.geometry.isValid() && !source.geometry.isEmpty(),
                 "Expected a valid screen geometry.");
        QVERIFY2(!source.resolutionText.trimmed().isEmpty(),
                 "Expected a non-empty resolution label.");
        QVERIFY2(source.screen, "Expected the source to retain a valid QScreen pointer.");
    }
}

void TestRecordingScreenSourceService::testResolutionTextUsesDevicePixelRatio()
{
    QCOMPARE(SnapTray::ScreenSourceService::resolutionText(QRect(0, 0, 100, 50), 2.0),
             QStringLiteral("200 x 100"));
    QCOMPARE(SnapTray::ScreenSourceService::resolutionText(QRect(0, 0, 100, 50), 1.5),
             QStringLiteral("150 x 75"));
}

// #210: at 150% a 2560-pixel screen is 1707 logical pixels in Qt, so the
// logical size times the ratio said 2561. Where the platform reports the
// screen's pixels (Windows, X11), the label is exactly those.
void TestRecordingScreenSourceService::testResolutionTextIsTheScreensOwnPixels()
{
    const QVector<SnapTray::ScreenSource> sources = SnapTray::ScreenSourceService::availableSources(false);
    if (sources.isEmpty()) QSKIP("No screens available in the test environment.");
    bool checked = false;
    for (const SnapTray::ScreenSource& source : sources) {
        const QSize native = CaptureScreenInfo::fromScreen(source.screen).physicalGeometry.size();
        if (native.isEmpty()) continue;
        QCOMPARE(source.resolutionText, QStringLiteral("%1 x %2").arg(native.width()).arg(native.height()));
        checked = true;
    }
    if (!checked) QSKIP("This platform does not report the screens' own pixels.");
}

QTEST_MAIN(TestRecordingScreenSourceService)
#include "tst_ScreenSourceService.moc"
