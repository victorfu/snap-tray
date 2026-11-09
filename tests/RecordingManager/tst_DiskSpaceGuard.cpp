#include <QtTest>

#include "recording/DiskSpaceGuard.h"

using SnapTray::DiskSpaceGuard;
using SnapTray::WriteRateEstimator;
using Level = DiskSpaceGuard::Level;

namespace {
constexpr qint64 kMiB = 1024 * 1024;
constexpr qint64 kRate = 2 * kMiB;  // 2 MiB/s, a high-quality 1080p intermediate

DiskSpaceGuard::Input input(std::optional<qint64> freeBytes, qint64 recorded = 0, bool exportLikely = false)
{
    DiskSpaceGuard::Input in;
    in.freeBytes = freeBytes;
    in.recordedBytes = recorded;
    in.bytesPerSecond = kRate;
    in.exportLikely = exportLikely;
    return in;
}
}

class TestDiskSpaceGuard : public QObject
{
    Q_OBJECT

private slots:
    void thresholdsFollowTheRecording();
    void levels();
    void warningHasHysteresis();
    void safeStopIsFinal();
    void unknownIsNotUnlimited();
    void secondsLeftIsAnEstimate();
    void writeRateSmoothsBursts();
    void bufferedOutputIsReservedInFull();
};

void TestDiskSpaceGuard::thresholdsFollowTheRecording()
{
    // Floor + 1 % finalize reserve + 10 s of data.
    QCOMPARE(DiskSpaceGuard::safeStopThreshold(input(0)), DiskSpaceGuard::kFloorBytes + 10 * kRate);
    QCOMPARE(DiskSpaceGuard::safeStopThreshold(input(0, 1000 * kMiB)),
             DiskSpaceGuard::kFloorBytes + 10 * kMiB + 10 * kRate);
    // The warning adds two minutes of data, plus room for an export when
    // preview is on (a second file as large as the recording).
    const qint64 warnWithout = DiskSpaceGuard::warningThreshold(input(0, 1000 * kMiB, false));
    const qint64 warnWith = DiskSpaceGuard::warningThreshold(input(0, 1000 * kMiB, true));
    QCOMPARE(warnWithout, DiskSpaceGuard::safeStopThreshold(input(0, 1000 * kMiB)) + 120 * kRate);
    QCOMPARE(warnWith - warnWithout, 1000 * kMiB);
}

void TestDiskSpaceGuard::levels()
{
    DiskSpaceGuard guard;
    const qint64 safeStop = DiskSpaceGuard::safeStopThreshold(input(0));
    const qint64 warning = DiskSpaceGuard::warningThreshold(input(0));
    QCOMPARE(guard.evaluate(input(warning + 10 * kMiB + DiskSpaceGuard::kHysteresisBytes)).level, Level::Ok);
    QCOMPARE(guard.evaluate(input(warning)).level, Level::Warning);
    QCOMPARE(guard.evaluate(input(safeStop + 1)).level, Level::Warning);
    QCOMPARE(guard.evaluate(input(safeStop)).level, Level::SafeStop);
}

void TestDiskSpaceGuard::warningHasHysteresis()
{
    DiskSpaceGuard guard;
    const qint64 warning = DiskSpaceGuard::warningThreshold(input(0));
    QCOMPARE(guard.evaluate(input(warning - kMiB)).level, Level::Warning);
    // Just above the threshold is not yet recovered: no flapping.
    QCOMPARE(guard.evaluate(input(warning + kMiB)).level, Level::Warning);
    QCOMPARE(guard.evaluate(input(warning + DiskSpaceGuard::kHysteresisBytes)).level, Level::Warning);
    // Clearly above it is.
    QCOMPARE(guard.evaluate(input(warning + DiskSpaceGuard::kHysteresisBytes + 1)).level, Level::Ok);
    // From Ok, being just above the threshold is fine.
    QCOMPARE(guard.evaluate(input(warning + kMiB)).level, Level::Ok);
}

void TestDiskSpaceGuard::safeStopIsFinal()
{
    DiskSpaceGuard guard;
    QCOMPARE(guard.evaluate(input(0)).level, Level::SafeStop);
    // Space freed afterwards does not restart a stopped recording.
    QCOMPARE(guard.evaluate(input(100 * 1024 * kMiB)).level, Level::SafeStop);
    QCOMPARE(guard.evaluate(input(std::nullopt)).level, Level::SafeStop);
    guard.reset();
    QCOMPARE(guard.evaluate(input(100 * 1024 * kMiB)).level, Level::Ok);
}

void TestDiskSpaceGuard::unknownIsNotUnlimited()
{
    DiskSpaceGuard guard;
    const auto result = guard.evaluate(input(std::nullopt));
    QCOMPARE(result.level, Level::Unknown);
    QCOMPARE(result.secondsLeft, qint64(-1));
    // A warning already raised stays when the probe starts failing.
    const qint64 warning = DiskSpaceGuard::warningThreshold(input(0));
    guard.evaluate(input(warning - kMiB));
    QCOMPARE(guard.evaluate(input(std::nullopt)).level, Level::Warning);
}

void TestDiskSpaceGuard::secondsLeftIsAnEstimate()
{
    DiskSpaceGuard guard;
    const qint64 safeStop = DiskSpaceGuard::safeStopThreshold(input(0));
    QCOMPARE(guard.evaluate(input(safeStop + 300 * kRate)).secondsLeft, qint64(300));
    // Without a write rate there is no estimate (show free space instead).
    DiskSpaceGuard::Input noRate = input(10 * 1024 * kMiB);
    noRate.bytesPerSecond = 0;
    QCOMPARE(guard.evaluate(noRate).secondsLeft, qint64(-1));
}

void TestDiskSpaceGuard::writeRateSmoothsBursts()
{
    WriteRateEstimator rate;
    QCOMPARE(rate.bytesPerSecond(), qint64(0));
    rate.addSample(0, 0);
    rate.addSample(2 * kMiB, 1000);
    QCOMPARE(rate.bytesPerSecond(), 2 * kMiB);
    // A flush burst moves the estimate only part of the way.
    rate.addSample(12 * kMiB, 2000);
    QVERIFY(rate.bytesPerSecond() > 2 * kMiB);
    QVERIFY(rate.bytesPerSecond() < 10 * kMiB);
    // A shrinking or unknown file size is ignored.
    const qint64 before = rate.bytesPerSecond();
    rate.addSample(-1, 3000);
    rate.addSample(5 * kMiB, 4000);
    QCOMPARE(rate.bytesPerSecond(), before);
    rate.reset();
    QCOMPARE(rate.bytesPerSecond(), qint64(0));
}

void TestDiskSpaceGuard::bufferedOutputIsReservedInFull()
{
    // PR #171 review: GIF/WebP hold the animation in memory and write it all
    // at the end, so it is reserved in full, not as 1 % of a file on disk.
    DiskSpaceGuard::Input buffered;
    buffered.pendingBytes = 500 * kMiB;
    QCOMPARE(DiskSpaceGuard::safeStopThreshold(buffered), DiskSpaceGuard::kFloorBytes + 500 * kMiB);
    DiskSpaceGuard guard;
    buffered.freeBytes = 400 * kMiB; // plenty for an MP4, not for this animation
    QCOMPARE(guard.evaluate(buffered).level, Level::SafeStop);
}

QTEST_MAIN(TestDiskSpaceGuard)
#include "tst_DiskSpaceGuard.moc"
