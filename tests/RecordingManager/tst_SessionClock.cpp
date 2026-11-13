#include <QtTest>

#include "capture/WGCFrameTiming.h"
#include "recording/CapturedFrame.h"
#include "recording/FramePacer.h"
#include "recording/SessionClock.h"

using SnapTray::FramePacer;
using SnapTray::SessionClock;
using Admission = SessionClock::Admission;

namespace {
constexpr qint64 kMs = 1000000;  // ns per ms
constexpr qint64 kEpoch = 5000 * kMs;
}

class TestSessionClock : public QObject
{
    Q_OBJECT

private slots:
    void startGateRejectsEarlierFrames();
    void pausesAreRemoved();
    void repeatedPauseResume();
    void duplicateAndBackwardAreRejectedAndCounted();
    void stopRejectsLateFrames();
    void encoderMillisecondsRoundOnce();
    void capturedFrameKeepsNativeBufferAlive();
    void wgcRealFrameAfterRepeat_data();
    void wgcRealFrameAfterRepeat();

    void pacerDropsOverDelivery();
    void pacerToleratesJitter();
    void pacerClassifiesLateAndStall();
    void pacerIgnoresPauseGap();
    void pacedTimesAreRealCaptureTimes();
};

void TestSessionClock::startGateRejectsEarlierFrames()
{
    SessionClock clock;
    qint64 time = -1;
    // Nothing is recording yet (preparation, countdown).
    QCOMPARE(clock.admit(kEpoch, &time), Admission::BeforeStart);
    clock.start(kEpoch);
    QCOMPARE(clock.admit(kEpoch - 1, &time), Admission::BeforeStart);
    QCOMPARE(clock.admit(kEpoch, &time), Admission::Accepted);
    QCOMPARE(time, qint64(0));
    QCOMPARE(clock.admit(kEpoch + 40 * kMs, &time), Admission::Accepted);
    QCOMPARE(time, 40 * kMs);
    // start() begins a session's counters; the frame before it is not counted.
    QCOMPARE(clock.stats().beforeStart, qint64(1));
    QCOMPARE(clock.stats().accepted, qint64(2));
    // A restart is a new session: counters and order start over.
    clock.start(kEpoch + 10000 * kMs);
    QCOMPARE(clock.stats().accepted, qint64(0));
    QCOMPARE(clock.admit(kEpoch + 10000 * kMs, &time), Admission::Accepted);
    QCOMPARE(time, qint64(0));
}

void TestSessionClock::pausesAreRemoved()
{
    SessionClock clock;
    clock.start(kEpoch);
    qint64 time = -1;
    QCOMPARE(clock.admit(kEpoch + 1000 * kMs, &time), Admission::Accepted);
    clock.pause(kEpoch + 1500 * kMs);
    QVERIFY(clock.isPaused());
    // Captured during the pause: not part of the video.
    QCOMPARE(clock.admit(kEpoch + 1600 * kMs, &time), Admission::Paused);
    // The duration shown to the user is frozen while paused.
    QCOMPARE(clock.recordingTimeAt(kEpoch + 1500 * kMs), 1500 * kMs);
    QCOMPARE(clock.recordingTimeAt(kEpoch + 9000 * kMs), 1500 * kMs);
    clock.resume(kEpoch + 4500 * kMs);  // 3 s paused
    QVERIFY(!clock.isPaused());
    QCOMPARE(clock.admit(kEpoch + 4500 * kMs, &time), Admission::Accepted);
    QCOMPARE(time, 1500 * kMs);  // continues where the pause began; no wall time added
    QCOMPARE(clock.admit(kEpoch + 4533 * kMs, &time), Admission::Accepted);
    QCOMPARE(time, 1533 * kMs);
    QCOMPARE(clock.recordingTimeAt(kEpoch + 5500 * kMs), 2500 * kMs);
    QCOMPARE(clock.stats().paused, qint64(1));
}

void TestSessionClock::repeatedPauseResume()
{
    SessionClock clock;
    clock.start(0);
    qint64 time = -1;
    qint64 expectedPaused = 0;
    for (int i = 0; i < 5; ++i) {
        const qint64 base = i * 1000 * kMs;
        QCOMPARE(clock.admit(base + 100 * kMs, &time), Admission::Accepted);
        QCOMPARE(time, base + 100 * kMs - expectedPaused);
        clock.pause(base + 200 * kMs);
        clock.pause(base + 300 * kMs);  // a second pause while paused is ignored
        clock.resume(base + 700 * kMs);
        clock.resume(base + 800 * kMs);  // and so is a second resume
        expectedPaused += 500 * kMs;
    }
    QCOMPARE(clock.recordingTimeAt(5000 * kMs), 5000 * kMs - expectedPaused);
}

void TestSessionClock::duplicateAndBackwardAreRejectedAndCounted()
{
    SessionClock clock;
    clock.start(0);
    qint64 time = -1;
    QCOMPARE(clock.admit(100 * kMs, &time), Admission::Accepted);
    QCOMPARE(clock.admit(100 * kMs, &time), Admission::Duplicate);
    QCOMPARE(clock.admit(50 * kMs, &time), Admission::Backward);
    // A rejected frame does not move the clock: the next real one is fine.
    QCOMPARE(clock.admit(133 * kMs, &time), Admission::Accepted);
    QCOMPARE(time, 133 * kMs);
    const auto stats = clock.stats();
    QCOMPARE(stats.duplicates, qint64(1));
    QCOMPARE(stats.backwards, qint64(1));
    QCOMPARE(stats.accepted, qint64(2));
}

void TestSessionClock::stopRejectsLateFrames()
{
    SessionClock clock;
    clock.start(0);
    qint64 time = -1;
    QCOMPARE(clock.admit(10 * kMs, &time), Admission::Accepted);
    clock.stop();
    QVERIFY(!clock.isStarted());
    // A frame still in flight from the capture thread after stop.
    QCOMPARE(clock.admit(20 * kMs, &time), Admission::Stopped);
    clock.reset();
    QCOMPARE(clock.admit(30 * kMs, &time), Admission::BeforeStart);
}

void TestSessionClock::encoderMillisecondsRoundOnce()
{
    QCOMPARE(SessionClock::toEncoderMs(-5), qint64(0));
    QCOMPARE(SessionClock::toEncoderMs(0), qint64(0));
    QCOMPARE(SessionClock::toEncoderMs(1499999), qint64(1));
    QCOMPARE(SessionClock::toEncoderMs(1500000), qint64(2));
    // 59.94 fps: rounding per frame keeps the drift bounded to half a ms.
    const qint64 intervalNs = 1001000000LL / 60;
    for (qint64 frame = 0; frame < 10000; ++frame) {
        const qint64 ns = frame * intervalNs;
        QVERIFY(qAbs(SessionClock::toEncoderMs(ns) * kMs - ns) <= kMs / 2);
    }
}

void TestSessionClock::capturedFrameKeepsNativeBufferAlive()
{
    // The deleter is the release of a platform surface (CVPixelBuffer,
    // ID3D11Texture2D); it must run only after every copy is gone.
    int released = 0;
    {
        SnapTray::CapturedFrame frame;
        frame.nativeBuffer = std::shared_ptr<void>(reinterpret_cast<void*>(0x1), [&released](void*) {
            ++released;
        });
        QVERIFY(frame.isValid());
        SnapTray::CapturedFrame queued = frame;  // for example in an encoder queue
        frame = {};
        QCOMPARE(released, 0);
        QVERIFY(queued.isValid());
    }
    QCOMPARE(released, 1);
    QVERIFY(!SnapTray::CapturedFrame{}.isValid());
    QVERIFY(SnapTray::captureClockNowNs() > 0);
}

void TestSessionClock::wgcRealFrameAfterRepeat_data()
{
    QTest::addColumn<qint64>("composedNs");
    QTest::addColumn<qint64>("expectedNs");
    QTest::newRow("late") << qint64(95) << qint64(101);
    QTest::newRow("equal") << qint64(100) << qint64(101);
    QTest::newRow("forward") << qint64(110) << qint64(110);
}

void TestSessionClock::wgcRealFrameAfterRepeat()
{
    QFETCH(qint64, composedNs);
    QFETCH(qint64, expectedNs);
    SessionClock clock;
    clock.start(0);
    SnapTray::CapturedFrame repeat;
    repeat.image = QImage(2, 2, QImage::Format_RGB32);
    repeat.image.fill(Qt::red);
    repeat.sourceTimeNs = 100;
    repeat.sequence = 1;
    qint64 lastEmittedNs = -1;
    const auto first = SnapTray::wgcFrameForEmission(repeat, lastEmittedNs);
    QCOMPARE(first.sourceTimeNs, qint64(100));
    lastEmittedNs = first.sourceTimeNs;
    QCOMPARE(clock.admit(first.sourceTimeNs, nullptr), Admission::Accepted);

    SnapTray::CapturedFrame real = repeat;
    real.image.fill(Qt::green);
    real.sourceTimeNs = composedNs;
    real.sequence = 2;
    real.generation = 3;
    real.pixelSize = real.image.size();
    real.nativeBuffer = std::make_shared<int>(42);
    const auto second = SnapTray::wgcFrameForEmission(real, lastEmittedNs);
    QCOMPARE(clock.admit(second.sourceTimeNs, nullptr), Admission::Accepted);
    QCOMPARE(second.sourceTimeNs, expectedNs);
    QVERIFY(second.sourceTimeNs > first.sourceTimeNs);
    QCOMPARE(second.image.pixelColor(0, 0), QColor(Qt::green));
    QCOMPARE(first.image.pixelColor(0, 0), QColor(Qt::red));
    QCOMPARE(second.nativeBuffer, real.nativeBuffer);
    QCOMPARE(second.sequence, real.sequence);
    QCOMPARE(second.generation, real.generation);
    QCOMPARE(second.clockDomain, real.clockDomain);
    QCOMPARE(second.pixelSize, real.pixelSize);
    QCOMPARE(real.sourceTimeNs, composedNs); // The cached source frame is unchanged.

    // Another delayed arrival advances from the adjusted time, not the repeat.
    lastEmittedNs = second.sourceTimeNs;
    const auto third = SnapTray::wgcFrameForEmission(real, lastEmittedNs);
    QCOMPARE(third.sourceTimeNs, expectedNs + 1);
    QCOMPARE(clock.admit(third.sourceTimeNs, nullptr), Admission::Accepted);
    QCOMPARE(clock.stats().duplicates, qint64(0));
    QCOMPARE(clock.stats().backwards, qint64(0));
    QCOMPARE(clock.stats().accepted, qint64(3));
    // A new capture session does not inherit the previous session's adjustment.
    QCOMPARE(SnapTray::wgcFrameForEmission(real, -1).sourceTimeNs, composedNs);
}

void TestSessionClock::pacerDropsOverDelivery()
{
    FramePacer pacer(30);
    int emitted = 0;
    // A 60 fps source recorded at 30 fps keeps every other frame.
    for (int i = 0; i < 120; ++i) {
        const qint64 ns = i * 1000000000LL / 60;
        if (pacer.onFrame(ns) == FramePacer::Decision::Emit) ++emitted;
    }
    QCOMPARE(emitted, 60);
    QCOMPARE(pacer.stats().dropped, qint64(60));
    QCOMPARE(pacer.stats().late, qint64(0));
}

void TestSessionClock::pacerToleratesJitter()
{
    FramePacer pacer(30);
    // ±6 ms of jitter around a 33.3 ms cadence (consecutive frames up to
    // 12 ms closer or further apart): nothing dropped, nothing late.
    const qint64 jitter[] = {0, 6, -6, 4, -4, 6, -6, 3};
    for (int i = 0; i < 300; ++i) {
        const qint64 ns = i * 1000000000LL / 30 + jitter[i % 8] * kMs;
        QCOMPARE(pacer.onFrame(qMax<qint64>(0, ns)), FramePacer::Decision::Emit);
    }
    QCOMPARE(pacer.stats().dropped, qint64(0));
    QCOMPARE(pacer.stats().late, qint64(0));
    QCOMPARE(pacer.stats().stalls, qint64(0));
}

void TestSessionClock::pacerClassifiesLateAndStall()
{
    FramePacer pacer(30);
    pacer.onFrame(0);
    pacer.onFrame(33 * kMs);
    pacer.onFrame(33 * kMs + 60 * kMs);     // 60 ms gap: late
    pacer.onFrame(93 * kMs + 500 * kMs);    // 500 ms gap: stall (static screen or blocked capture)
    pacer.onFrame(593 * kMs + 33 * kMs);    // back on cadence
    const auto stats = pacer.stats();
    QCOMPARE(stats.late, qint64(1));
    QCOMPARE(stats.stalls, qint64(1));
    QCOMPARE(stats.emitted, qint64(5));
    QCOMPARE(stats.longestGapNs, 500 * kMs);
}

void TestSessionClock::pacerIgnoresPauseGap()
{
    // With the clock removing the pause, frames resume at nearly the same
    // recording time; the cadence rule still applies, but no gap is counted.
    SessionClock clock;
    FramePacer pacer(30);
    clock.start(0);
    qint64 time = 0;
    QCOMPARE(clock.admit(0, &time), Admission::Accepted);
    QCOMPARE(pacer.onFrame(time), FramePacer::Decision::Emit);
    clock.pause(10 * kMs);
    pacer.notePause();
    clock.resume(5000 * kMs);
    QCOMPARE(clock.admit(5040 * kMs, &time), Admission::Accepted);
    QCOMPARE(time, 50 * kMs);
    QCOMPARE(pacer.onFrame(time), FramePacer::Decision::Emit);
    QCOMPARE(pacer.stats().late, qint64(0));
    QCOMPARE(pacer.stats().stalls, qint64(0));
}

void TestSessionClock::pacedTimesAreRealCaptureTimes()
{
    // A late frame keeps its own time; nothing is inserted before it.
    SessionClock clock;
    FramePacer pacer(30);
    clock.start(kEpoch);
    QList<qint64> encoderTimes;
    for (const qint64 offsetMs : {0, 33, 67, 250, 283}) {
        qint64 time = 0;
        if (clock.admit(kEpoch + offsetMs * kMs, &time) == Admission::Accepted
            && pacer.onFrame(time) == FramePacer::Decision::Emit) {
            encoderTimes.append(SessionClock::toEncoderMs(time));
        }
    }
    QCOMPARE(encoderTimes, QList<qint64>({0, 33, 67, 250, 283}));
}

QTEST_MAIN(TestSessionClock)
#include "tst_SessionClock.moc"
