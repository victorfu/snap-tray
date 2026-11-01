#include <QtTest>

#include "recording/RecordingHealth.h"

using SnapTray::HealthWarningPolicy;
using SnapTray::RecordingHealth;
using Action = HealthWarningPolicy::Action;

namespace {
constexpr qint64 kMs = 1000000;
}

class TestRecordingHealth : public QObject
{
    Q_OBJECT

private slots:
    void layeredCounters();
    void percentilesAndOutputFps();
    void timingMemoryIsBounded();
    void diagnosticsHaveNoMediaOrPaths();

    void singleBadWindowDoesNotWarn();
    void sustainedLossWarnsOnceThenRecovers();
    void warningsAreThrottled();
    void staticScreenIsNotLoss();
};

void TestRecordingHealth::layeredCounters()
{
    RecordingHealth health;
    health.reset(QStringLiteral("X11 shared memory"), 7);
    for (int i = 0; i < 10; ++i) health.frameOffered();
    for (int i = 0; i < 6; ++i) health.frameQueued(i + 1, (i + 1) * 100);
    health.frameDropped(RecordingHealth::FrameDrop::QueueFull);
    health.frameDropped(RecordingHealth::FrameDrop::QueueBytes);
    health.frameDropped(RecordingHealth::FrameDrop::QueueStale);
    health.frameDropped(RecordingHealth::FrameDrop::QueueStale);
    for (int i = 0; i < 5; ++i) health.frameEncoded(2 * kMs, 10 * kMs, i * 33 * kMs);
    health.frameDropped(RecordingHealth::FrameDrop::EncoderRejected);
    const auto s = health.snapshot(4 * 33 * kMs);
    QCOMPARE(s.backend, QStringLiteral("X11 shared memory"));
    QCOMPARE(s.generation, quint64(7));
    QCOMPARE(s.offered, qint64(10));
    QCOMPARE(s.queued, qint64(6));
    QCOMPARE(s.encoded, qint64(5));
    QCOMPARE(s.droppedFrames(), qint64(5));
    QCOMPARE(s.peakQueueDepth, 6);
    QCOMPARE(s.peakQueueBytes, qint64(600));
    health.reset(QStringLiteral("next"), 8);
    QCOMPARE(health.snapshot(0).offered, qint64(0));
}

void TestRecordingHealth::percentilesAndOutputFps()
{
    RecordingHealth health;
    health.reset(QString(), 1);
    // 100 frames over ~3.3 s at 30 fps; queue age 1..100 ms, encode 5 ms.
    for (int i = 0; i < 100; ++i) {
        health.frameEncoded((i + 1) * kMs, 5 * kMs, i * 33333333LL);
    }
    const auto s = health.snapshot(99 * 33333333LL);
    QCOMPARE(s.queueAgeP50Ms, qint64(51));
    QCOMPARE(s.queueAgeP95Ms, qint64(96));
    QCOMPARE(s.encodeP50Ms, qint64(5));
    QVERIFY2(qAbs(s.outputFps - 30.0) <= 1.0, qPrintable(QString::number(s.outputFps)));
    // No frames for a while: the output rate falls to zero, it is not frozen.
    QCOMPARE(health.snapshot(99 * 33333333LL + 5000 * kMs).outputFps, 0.0);
}

void TestRecordingHealth::timingMemoryIsBounded()
{
    RecordingHealth health;
    health.reset(QString(), 1);
    // An hour at 60 fps: the rings stay at kTimingSamples.
    for (qint64 i = 0; i < 60 * 3600; ++i) {
        health.frameEncoded((i % 7) * kMs, kMs, i * 16666667LL);
    }
    const auto s = health.snapshot(60LL * 3600 * 16666667LL);
    QCOMPARE(s.encoded, qint64(60 * 3600));
    QVERIFY(s.queueAgeP95Ms <= 6);
    QVERIFY2(qAbs(s.outputFps - 60.0) <= 1.0, qPrintable(QString::number(s.outputFps)));
}

void TestRecordingHealth::diagnosticsHaveNoMediaOrPaths()
{
    RecordingHealth health;
    health.reset(QStringLiteral("ScreenCaptureKit"), 3);
    health.frameOffered();
    health.audioDropped(1024);
    RecordingHealth::ClockCounters clock;
    clock.stalls = 2;
    clock.longestGapMs = 900;
    health.setClockCounters(clock);
    const QString text = RecordingHealth::formatDiagnostics(health.snapshot(0));
    QVERIFY(text.contains(QStringLiteral("ScreenCaptureKit")));
    QVERIFY(text.contains(QStringLiteral("stalls 2")));
    QVERIFY(text.contains(QStringLiteral("1024 bytes; kept as silence")));
    QVERIFY(!text.contains(QLatin1Char('/')));
    QVERIFY(!text.contains(QLatin1Char('\\')));
}

void TestRecordingHealth::singleBadWindowDoesNotWarn()
{
    HealthWarningPolicy policy;
    QCOMPARE(policy.evaluate(30, 0, 1000), Action::None);
    QCOMPARE(policy.evaluate(30, 20, 2000), Action::None);  // a hiccup
    QCOMPARE(policy.evaluate(30, 0, 3000), Action::None);
    QCOMPARE(policy.evaluate(30, 2, 4000), Action::None);   // under the ratio
    QVERIFY(!policy.isWarning());
}

void TestRecordingHealth::sustainedLossWarnsOnceThenRecovers()
{
    HealthWarningPolicy policy;
    qint64 now = 0;
    QCOMPARE(policy.evaluate(30, 10, now += 1000), Action::None);
    QCOMPARE(policy.evaluate(30, 10, now += 1000), Action::None);
    QCOMPARE(policy.evaluate(30, 10, now += 1000), Action::Warn);
    QVERIFY(policy.isWarning());
    // Still bad: no repeat while the warning stands.
    for (int i = 0; i < 10; ++i) QCOMPARE(policy.evaluate(30, 10, now += 1000), Action::None);
    // Recovery needs several clean windows in a row.
    for (int i = 0; i < HealthWarningPolicy::kGoodWindows - 1; ++i)
        QCOMPARE(policy.evaluate(30, 0, now += 1000), Action::None);
    QCOMPARE(policy.evaluate(30, 0, now += 1000), Action::Recovered);
    QVERIFY(!policy.isWarning());
}

void TestRecordingHealth::warningsAreThrottled()
{
    HealthWarningPolicy policy;
    qint64 now = 0;
    for (int i = 0; i < 3; ++i) policy.evaluate(30, 10, now += 1000);
    QVERIFY(policy.isWarning());
    for (int i = 0; i < HealthWarningPolicy::kGoodWindows; ++i) policy.evaluate(30, 0, now += 1000);
    QVERIFY(!policy.isWarning());
    // Bad again soon after: within the minimum interval, no second warning.
    for (int i = 0; i < 5; ++i) QCOMPARE(policy.evaluate(30, 10, now += 1000), Action::None);
    now += HealthWarningPolicy::kMinWarningIntervalMs;
    QCOMPARE(policy.evaluate(30, 10, now), Action::Warn);
}

void TestRecordingHealth::staticScreenIsNotLoss()
{
    HealthWarningPolicy policy;
    qint64 now = 0;
    // No frames offered (nothing changed on screen): never a warning, and it
    // does not break a streak either way.
    for (int i = 0; i < 20; ++i) QCOMPARE(policy.evaluate(0, 0, now += 1000), Action::None);
    QVERIFY(!policy.isWarning());
}

QTEST_MAIN(TestRecordingHealth)
#include "tst_RecordingHealth.moc"
