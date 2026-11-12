#include "ContentExport.h"
#include "InspectState.h"
#include "ProbeEventLoop.h"
#include <QTimer>

#include <QFile>
#include <QTemporaryDir>
#include <QProcess>
#include <QProcessEnvironment>
#include <QtTest>

using namespace snaptray::scrollcapture;
using namespace snaptray::scrollcapture::probe;

namespace {
QImage frame(int offset)
{
    QImage image(80, 180, QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const quint32 value = quint32(y + offset) * 2654435761U + quint32(x) * 40503U;
            image.setPixel(x, y, qRgb(value & 255, (value >> 8) & 255, (value >> 16) & 255));
        }
    }
    return image;
}

ContentRepeat moving(int offset = 0)
{
    ShiftEstimate dominant;
    dominant.status = ShiftEstimate::Status::Found;
    dominant.shift = 30;
    return validateContentRepeat(frame(offset), frame(offset + 30), FrameSettleDetector::Outcome::Settled,
                                 QStringLiteral("within_search_window"), true, dominant);
}

ContentRepeat still(FrameSettleDetector::Outcome outcome = FrameSettleDetector::Outcome::Settled)
{
    ShiftEstimate dominant;
    dominant.status = ShiftEstimate::Status::Found;
    return validateContentRepeat(frame(30), frame(30), outcome,
                                 QStringLiteral("within_search_window"), true, dominant);
}
}

class tst_ProbeContentExport : public QObject
{
    Q_OBJECT
private slots:
    void syncCauseUsesUnconstrainedEstimateAndOffsetPresence();
    void activeSettleDeliversQueuedCancellation();
    void inspectLevelChangesAndReentrantMeasurement();
    void timedOutUnchangedRepeatRejectsExport();
    void settledNoMotionEndsSegment();
    void resumedMotionRejectsExport();
    void rejectionReasons_data();
    void rejectionReasons();
    void failedRepeatPreservesFirstReasonAndStaleFile();
    void captureBudgetBoundary();
    void savesAtomicallyAndReportsStage();
    void validatesDestinationBeforeCapture();
    void noMotionWithoutContentRejects();
    void rejectsInvalidCommandLine();
};

void tst_ProbeContentExport::syncCauseUsesUnconstrainedEstimateAndOffsetPresence()
{
    ShiftEstimate visual;
    visual.status = ShiftEstimate::Status::Ambiguous;
    ShiftEstimate constrained;
    constrained.status = ShiftEstimate::Status::Found;
    constrained.shift = 30;
    const auto reason = positionSyncUnavailableReason(true, true, true, visual);
    const auto result = validateContentRepeat(frame(0), frame(30), FrameSettleDetector::Outcome::Settled,
        QStringLiteral("not_available"), true, constrained, reason);
    QCOMPARE(result.error, QStringLiteral("visual_shift_ambiguous"));
    QVERIFY(!result.pair);
    for (bool beforePresent : {false, true}) {
        const auto missing = positionSyncUnavailableReason(true, beforePresent, false, visual);
        const auto rejected = validateContentRepeat(frame(0), frame(30), FrameSettleDetector::Outcome::Settled,
            QStringLiteral("not_available"), true, visual, missing);
        QCOMPARE(rejected.error, beforePresent ? QStringLiteral("position_missing_offset_after")
                                              : QStringLiteral("position_missing_offset_before"));
        QVERIFY(!rejected.pair);
    }
}

void tst_ProbeContentExport::activeSettleDeliversQueuedCancellation()
{
    FrameSettleDetector::Config config;
    config.timeoutMs = 5000;
    FrameSettleDetector detector(config);
    detector.start(frame(0));
    bool stopped = false;
    int captures = 0;
    QObject context;
    const auto result = settleFrames(detector, [&] {
        ++captures;
        // A stop shortcut delivered while a frame is being captured.
        QTimer::singleShot(0, &context, [&] { stopped = true; });
        return frame(captures * 30);
    }, [&] { return captures * 100; }, [&] { return stopped; });
    QCOMPARE(result, SettleLoopResult::Cancelled);
    QVERIFY(stopped);
    QCOMPARE(captures, 1);
    QCOMPARE(detector.result().outcome, FrameSettleDetector::Outcome::Pending);

    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("cancelled.png"));
    ContentExportSession session;
    session.add(0, moving());
    QCOMPARE(session.finish(path, stopped).value("status").toString(), QStringLiteral("rejected"));
    QVERIFY(!QFile::exists(path));
}

void tst_ProbeContentExport::inspectLevelChangesAndReentrantMeasurement()
{
    InspectState state(1);
    QVERIFY(!state.beginMeasurement());
    state.updateLevels(0);
    QCOMPARE(state.level(), 1); // Hovering a non-scrollable gap must preserve --level.
    QCOMPARE(QByteArray(state.ignoredReason()), QByteArray("no scroll area"));
    state.updateLevels(3);
    QCOMPARE(state.level(), 1);
    state.cycle(1);
    QCOMPARE(state.level(), 2);
    state.cycle(1);
    QCOMPARE(state.level(), 0);
    state.cycle(-1);
    QCOMPARE(state.level(), 2);
    QVERIFY(state.beginMeasurement());
    QCOMPARE(QByteArray(state.ignoredReason()), QByteArray("measurement pending or running"));
    QVERIFY(!state.beginMeasurement()); // processEvents must not start a second capture.
    state.cycle(-1);
    QCOMPARE(state.level(), 2);
    state.endMeasurement();
    QVERIFY(state.ignoredReason() == nullptr);
    state.updateLevels(0);
    QCOMPARE(state.level(), 2);
    state.updateLevels(1); // Moving from a nested div to a plain document resets level.
    QCOMPARE(state.level(), 0);
    state.updateLevels(0);
    QVERIFY(!state.beginMeasurement());
}

void tst_ProbeContentExport::timedOutUnchangedRepeatRejectsExport()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("content.png"));
    FrameSettleDetector::Config config;
    config.timeoutMs = 200; // Less than the 300 ms no-motion grace period.
    FrameSettleDetector detector(config);
    const QImage unchanged = frame(30);
    detector.start(unchanged);
    for (int t = 25; t <= 200; t += 25) detector.addFrame(unchanged, t);
    QCOMPARE(detector.result().outcome, FrameSettleDetector::Outcome::TimedOut);
    QCOMPARE(detector.lastFrame(), unchanged);
    const auto repeat = still(detector.result().outcome);
    QVERIFY(!repeat.noMotion);
    ContentExportSession session;
    session.add(0, moving());
    session.add(1, repeat);
    const auto result = session.finish(path);
    QCOMPARE(result.value("status").toString(), QStringLiteral("rejected"));
    QCOMPARE(result.value("reason").toString(), QStringLiteral("settle_timed_out"));
    QCOMPARE(result.value("repeatIndex").toInt(), 1);
    QVERIFY(!result.contains("endedAtNoMotionRepeat"));
    QVERIFY(!QFile::exists(path));
}

void tst_ProbeContentExport::settledNoMotionEndsSegment()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ContentExportSession session;
    session.add(0, moving());
    session.add(1, still());
    session.add(2, still());
    const QString path = directory.filePath(QStringLiteral("content.png"));
    const auto result = session.finish(path);
    QCOMPARE(result.value("status").toString(), QStringLiteral("exported"));
    QCOMPARE(result.value("endedAtNoMotionRepeat").toInt(), 1);
    QCOMPARE(QImage(path).size(), QSize(80, 210));
    QCOMPARE(QImage(path).copy(0, 30, 80, 180), frame(30));
}

void tst_ProbeContentExport::resumedMotionRejectsExport()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ContentExportSession session;
    session.add(0, moving());
    session.add(1, still());
    session.add(2, moving(30));
    const auto result = session.finish(directory.filePath(QStringLiteral("content.png")));
    QCOMPARE(result.value("reason").toString(), QStringLiteral("motion_after_no_motion_repeat"));
    QCOMPARE(result.value("repeatIndex").toInt(), 2);
}

void tst_ProbeContentExport::rejectionReasons_data()
{
    QTest::addColumn<QString>("sync");
    QTest::addColumn<bool>("sameBounds");
    QTest::addColumn<int>("status");
    QTest::addColumn<int>("shift");
    QTest::addColumn<QString>("reason");
    const int found = int(ShiftEstimate::Status::Found);
    for (const QString& sync : {QStringLiteral("timed_out"), QStringLiteral("frame_changed"),
                               QStringLiteral("position_missing"), QStringLiteral("not_available")}) {
        QTest::newRow(qPrintable(sync)) << sync << true << found << 30 << QStringLiteral("position_sync_") + sync;
    }
    QTest::newRow("bounds") << QStringLiteral("within_search_window") << false << found << 30 << QStringLiteral("geometry_changed");
    QTest::newRow("timeout-before-ambiguous") << QStringLiteral("timed_out") << true
        << int(ShiftEstimate::Status::Ambiguous) << 30 << QStringLiteral("position_sync_timed_out");
    for (auto status : {ShiftEstimate::Status::NotFound, ShiftEstimate::Status::Ambiguous,
                        ShiftEstimate::Status::InsufficientDetail}) {
        QTest::newRow(qPrintable(QStringLiteral("unsynchronized-no-cause-") + QLatin1String(shiftEstimateStatusName(status))))
            << QStringLiteral("not_available") << true << int(status) << 30
            << QStringLiteral("position_sync_not_available");
        QTest::newRow(shiftEstimateStatusName(status)) << QStringLiteral("within_search_window") << true
            << int(status) << 30 << QStringLiteral("dominant_shift_") + QLatin1String(shiftEstimateStatusName(status));
    }
    QTest::newRow("reverse") << QStringLiteral("within_search_window") << true << found << -30 << QStringLiteral("non_positive_shift");
    QTest::newRow("zero-but-changed") << QStringLiteral("within_search_window") << true << found << 0 << QStringLiteral("non_positive_shift");
    QTest::newRow("half-viewport") << QStringLiteral("within_search_window") << true << found << 90 << QStringLiteral("shift_too_large");
}

void tst_ProbeContentExport::rejectionReasons()
{
    QFETCH(QString, sync);
    QFETCH(bool, sameBounds);
    QFETCH(int, status);
    QFETCH(int, shift);
    QFETCH(QString, reason);
    ShiftEstimate dominant;
    dominant.status = ShiftEstimate::Status(status);
    dominant.shift = shift;
    const auto repeat = validateContentRepeat(frame(0), frame(30), FrameSettleDetector::Outcome::Settled,
                                               sync, sameBounds, dominant);
    QCOMPARE(repeat.error, reason);
    QVERIFY(!repeat.pair);
    QVERIFY(!repeat.noMotion);
    // A stale/unsynchronized or moved viewport cannot bypass checks by being unchanged.
    if (!sameBounds || sync != QLatin1String("within_search_window")) {
        const auto unchanged = validateContentRepeat(frame(0), frame(0), FrameSettleDetector::Outcome::Settled,
                                                      sync, sameBounds, dominant);
        QCOMPARE(unchanged.error, reason);
        QVERIFY(!unchanged.noMotion);
    }
}

void tst_ProbeContentExport::failedRepeatPreservesFirstReasonAndStaleFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("content.png"));
    QFile old(path);
    QVERIFY(old.open(QIODevice::WriteOnly));
    old.write("previous image");
    old.close();
    ContentExportSession session;
    session.add(0, moving());
    session.add(1, {std::nullopt, QStringLiteral("injection_failed"), false});
    session.add(2, {std::nullopt, QStringLiteral("capture_failed"), false});
    const auto result = session.finish(path);
    QCOMPARE(result.value("reason").toString(), QStringLiteral("injection_failed"));
    QCOMPARE(result.value("repeatIndex").toInt(), 1);
    QVERIFY(result.value("staleFileAtPath").toBool());
    QVERIFY(old.open(QIODevice::ReadOnly));
    QCOMPARE(old.readAll(), QByteArray("previous image"));
    QCOMPARE(session.finish(path, true).value("reason").toString(), QStringLiteral("interrupted"));
}

void tst_ProbeContentExport::captureBudgetBoundary()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    constexpr qint64 pixels = 80 * 180 * 2;
    ContentExportSession exact(pixels);
    exact.add(0, moving());
    QCOMPARE(exact.finish(directory.filePath("exact.png")).value("status").toString(), QStringLiteral("exported"));
    ContentExportSession tooSmall(pixels - 1);
    tooSmall.add(0, moving());
    const auto result = tooSmall.finish(directory.filePath("small.png"));
    QCOMPARE(result.value("reason").toString(), QStringLiteral("capture_budget_exceeded"));
    QCOMPARE(result.value("repeatIndex").toInt(), 0);
}

void tst_ProbeContentExport::savesAtomicallyAndReportsStage()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("content.png");
    QVERIFY(saveContentImage(frame(0), path).ok());
    const auto encodeFailure = saveContentImage({}, path);
    QCOMPARE(encodeFailure.stage, QStringLiteral("encode"));
    QVERIFY(!encodeFailure.detail.isEmpty());
    QCOMPARE(QImage(path), frame(0));
    ContentExportSession session;
    session.add(0, moving());
    const auto failure = session.finish(directory.filePath("missing/content.png"));
    QCOMPARE(failure.value("reason").toString(), QStringLiteral("save_failed"));
    QCOMPARE(failure.value("stage").toString(), QStringLiteral("open"));
    QVERIFY(!failure.value("detail").toString().isEmpty());
}

void tst_ProbeContentExport::validatesDestinationBeforeCapture()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("content.png");
    QVERIFY(contentOutputPathError(path).isEmpty());
    QVERIFY(!QFile::exists(path));
    QVERIFY(!contentOutputPathError(directory.path()).isEmpty());
    QVERIFY(!contentOutputPathError(directory.filePath("missing/content.png")).isEmpty());
}

void tst_ProbeContentExport::noMotionWithoutContentRejects()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ContentExportSession session;
    session.add(0, still());
    const auto result = session.finish(directory.filePath("content.png"));
    QCOMPARE(result.value("reason").toString(), QStringLiteral("no_scroll_motion"));
    QCOMPARE(result.value("repeatIndex").toInt(), 0);
}

void tst_ProbeContentExport::rejectsInvalidCommandLine()
{
#if defined(Q_OS_WIN)
    const QString method = QStringLiteral("wheel-delta");
#elif defined(Q_OS_MACOS)
    const QString method = QStringLiteral("wheel-pixels");
#else
    QSKIP("The probe has no scroll injection method on this platform");
    const QString method;
#endif
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    for (const QString& amount : {QStringLiteral("-30"), QStringLiteral("0")}) {
        QProcess process;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        process.setProcessEnvironment(environment);
        process.start(QStringLiteral(SCROLL_PROBE_EXECUTABLE), {QStringLiteral("measure"),
            QStringLiteral("--scroll"), method, QStringLiteral("--amount"), amount,
            QStringLiteral("--content-output"), directory.filePath("content.png")});
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(process.exitCode(), 2);
        QVERIFY2(process.readAllStandardError().contains("positive --amount"), qPrintable(process.errorString()));
        QVERIFY(!QFile::exists(directory.filePath("content.png")));
    }
}

QTEST_GUILESS_MAIN(tst_ProbeContentExport)
#include "tst_ProbeContentExport.moc"
