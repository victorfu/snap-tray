#include <QtTest>
#include <QChronoTimer>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include "capture/CaptureFrameTiming.h"

class TestCaptureFrameTiming : public QObject
{
    Q_OBJECT
private slots:
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
