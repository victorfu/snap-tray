#include <QtTest>
#include <QChronoTimer>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include "capture/CaptureFrameTiming.h"
#include "capture/DesktopCapturePolicy.h"

class TestCaptureFrameTiming : public QObject
{
    Q_OBJECT
private slots:
    void presentationTimesAndRecovery();
    void testCadence_data();
    void testCadence();
    void testInvalidRates();
};

void TestCaptureFrameTiming::testCadence_data()
{
    QTest::addColumn<int>("fps");
    QTest::addColumn<qint64>("nanoseconds");
    QTest::newRow("10") << 10 << qint64(100000000);
    QTest::newRow("15") << 15 << qint64(66666666);
    QTest::newRow("24") << 24 << qint64(41666666);
    QTest::newRow("30") << 30 << qint64(33333333);
    QTest::newRow("60") << 60 << qint64(16666666);
}

void TestCaptureFrameTiming::testCadence()
{
    QFETCH(int, fps);
    QFETCH(qint64, nanoseconds);
    const auto interval = SnapTray::captureFrameInterval(fps);
    QCOMPARE(interval.count(), nanoseconds);
    QChronoTimer timer;
    timer.setTimerType(Qt::PreciseTimer);
    timer.setInterval(interval);
    QElapsedTimer clock;
    QVector<qint64> timestamps;
    connect(&timer, &QChronoTimer::timeout, [&] { timestamps.append(clock.nsecsElapsed()); });
    QEventLoop loop;
    QTimer::singleShot(800, &loop, &QEventLoop::quit);
    clock.start();
    timer.start();
    loop.exec();
    timer.stop();
    const qreal expectedCount = clock.elapsed() * fps / 1000.0;
    QVERIFY(qAbs(timestamps.size() - expectedCount) <= qMax(2.0, expectedCount * 0.25));
    QVERIFY(timestamps.size() >= 2);
    const qreal meanInterval = qreal(timestamps.last() - timestamps.first()) / (timestamps.size() - 1);
    QVERIFY(qAbs(meanInterval - nanoseconds) < nanoseconds * 0.25);
}

void TestCaptureFrameTiming::testInvalidRates()
{
    QCOMPARE(SnapTray::captureFrameInterval(0).count(), qint64(0));
    QCOMPARE(SnapTray::captureFrameInterval(-10).count(), qint64(0));
}

QTEST_GUILESS_MAIN(TestCaptureFrameTiming)
#include "tst_CaptureFrameTiming.moc"

void TestCaptureFrameTiming::presentationTimesAndRecovery()
{
    constexpr qint64 counter = 1000000000000000LL;
    constexpr qint64 now = 50000000000LL;
    QCOMPARE(SnapTray::desktopPresentationTimeNs(counter - 166667, counter, 10000000, now), now - 16666700);
    QCOMPARE(SnapTray::desktopPresentationTimeNs(0, counter, 10000000, now), now);
    QCOMPARE(SnapTray::desktopPresentationTimeNs(counter + 1, counter, 10000000, now), now);
    QCOMPARE(SnapTray::desktopPresentationTimeNs(counter, counter, 0, now), now);
    QVERIFY(!SnapTray::desktopImageChanged(0, 1));
    QVERIFY(!SnapTray::desktopImageChanged(123, 0));
    QVERIFY(SnapTray::desktopImageChanged(123, 2));
    const qint64 delays[]{1000000000, 2000000000, 4000000000, 8000000000, 8000000000};
    for (int i = 0; i < 5; ++i) QCOMPARE(SnapTray::desktopRecoveryDelayNs(i), delays[i]);
}
