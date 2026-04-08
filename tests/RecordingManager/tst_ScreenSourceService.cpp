#include <QtTest/QtTest>

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

QTEST_MAIN(TestRecordingScreenSourceService)
#include "tst_ScreenSourceService.moc"
