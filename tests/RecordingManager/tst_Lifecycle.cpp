#include <QtTest/QtTest>
#include <QSignalSpy>

#include "RecordingManager.h"
#include "settings/Settings.h"
#include "settings/FileSettingsManager.h"
#include <QFile>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QDir>
#include "recording/WindowTimelineSidecar.h"
#include "recording/WindowTimelineRecorder.h"
#include "encoding/IntermediateQuality.h"

/**
 * @brief Tests for RecordingManager resource lifecycle
 *
 * Covers:
 * - Resource creation and cleanup
 * - Bug #2 prevention: testOnInitializationComplete_InitTaskNull
 * - Error handling paths
 * - Signal emission during lifecycle
 */
class TestRecordingManagerLifecycle : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Constructor/Destructor tests
    void testConstructorInitializesState();
    void testMultipleInstances();

    // Bug #2 prevention: Null pointer checks
    void testNullPointerSafety();

    // Resource cleanup tests
    void testCleanupInIdleState();
    void testStopRecordingInIdleState();

    // Error handling tests
    void testErrorSignalOnInvalidOperation();
    void testAutoSaveKeepsExistingRecording();
    void testSaveNameUsesCroppedOutputSize();
    void testWindowTimelineOnlyRecordedForPreview();
    void testWindowTimelinePausesWithRecording();
    void testFinishWritesSidecarOnlyOnSuccess();
    void testFinishSkipsSidecarWithoutWindows();
    void testStaleSidecarsAreCleanedUp();
    void intermediateQualityFollowsFreeSpace_data();
    void intermediateQualityFollowsFreeSpace();
    void previewRequestedCarriesIntermediateDecision_data();
    void previewRequestedCarriesIntermediateDecision();

private:
    RecordingManager* m_manager = nullptr;
};

void TestRecordingManagerLifecycle::initTestCase()
{
}

void TestRecordingManagerLifecycle::cleanupTestCase()
{
}

void TestRecordingManagerLifecycle::init()
{
    m_manager = new RecordingManager();
}

void TestRecordingManagerLifecycle::cleanup()
{
    delete m_manager;
    m_manager = nullptr;
}

// ============================================================================
// Constructor/Destructor Tests
// ============================================================================

void TestRecordingManagerLifecycle::testConstructorInitializesState()
{
    RecordingManager manager;

    QCOMPARE(manager.state(), RecordingManager::State::Idle);
    QVERIFY(!manager.isActive());
    QVERIFY(!manager.isRecording());
    QVERIFY(!manager.isPaused());
}

void TestRecordingManagerLifecycle::testMultipleInstances()
{
    // Multiple managers should coexist
    RecordingManager manager1;
    RecordingManager manager2;

    QCOMPARE(manager1.state(), RecordingManager::State::Idle);
    QCOMPARE(manager2.state(), RecordingManager::State::Idle);
}

// ============================================================================
// Bug #2 Prevention Tests
// ============================================================================

void TestRecordingManagerLifecycle::testNullPointerSafety()
{
    // Verify various operations don't crash when in initial state

    // These should all be safe in idle state
    m_manager->stopRecording();
    m_manager->cancelRecording();
    m_manager->pauseRecording();
    m_manager->resumeRecording();
    m_manager->togglePause();

    // Should still be in idle state
    QCOMPARE(m_manager->state(), RecordingManager::State::Idle);
}

// ============================================================================
// Resource Cleanup Tests
// ============================================================================

void TestRecordingManagerLifecycle::testCleanupInIdleState()
{
    // Calling operations in idle state should be safe
    QCOMPARE(m_manager->state(), RecordingManager::State::Idle);

    m_manager->cancelRecording();

    // Should remain idle
    QCOMPARE(m_manager->state(), RecordingManager::State::Idle);
}

void TestRecordingManagerLifecycle::testStopRecordingInIdleState()
{
    QSignalSpy stoppedSpy(m_manager, &RecordingManager::recordingStopped);

    m_manager->stopRecording();

    // Should not emit stopped signal when not recording
    QCOMPARE(stoppedSpy.count(), 0);
    QCOMPARE(m_manager->state(), RecordingManager::State::Idle);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

void TestRecordingManagerLifecycle::testErrorSignalOnInvalidOperation()
{
    QSignalSpy errorSpy(m_manager, &RecordingManager::recordingError);

    // Operations in wrong state should not crash
    m_manager->stopRecording();
    m_manager->pauseRecording();
    m_manager->resumeRecording();

    // These operations silently fail when in wrong state (no error signal)
    // This documents current behavior
    QCOMPARE(m_manager->state(), RecordingManager::State::Idle);
}

void TestRecordingManagerLifecycle::testAutoSaveKeepsExistingRecording()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto& settings = FileSettingsManager::instance();
    settings.saveAutoSaveRecordings(true);
    settings.saveRecordingPath(dir.path());
    settings.saveFilenameTemplate("same.{ext}");
    const QString existing = dir.filePath("same.mp4");
    const QString temporary = dir.filePath("input.mp4");
    for (const auto& path : {existing, temporary}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(path == existing ? "original" : "new-video"),
                 path == existing ? qint64(8) : qint64(9));
    }
    QSignalSpy saved(m_manager, &RecordingManager::recordingStopped);
    m_manager->triggerSaveDialog(temporary);
    QCOMPARE(saved.count(), 1);
    QCOMPARE(saved.first().first().toString(), dir.filePath("same_1.mp4"));
    QFile original(existing);
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), QByteArray("original"));
    QVERIFY(!QFile::exists(temporary));
}

// {w}x{h} names a cropped export by its own pixel size; any other save keeps
// the recording region's size.
void TestRecordingManagerLifecycle::testSaveNameUsesCroppedOutputSize()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto& settings = FileSettingsManager::instance();
    settings.saveAutoSaveRecordings(true);
    settings.saveRecordingPath(dir.path());
    settings.saveFilenameTemplate("{w}x{h}.{ext}");
    m_manager->m_recordingRegion = QRect(0, 0, 1440, 900);
    const QString cropped = dir.filePath("cropped.gif");
    const QString uncropped = dir.filePath("uncropped.mp4");
    for (const auto& path : {cropped, uncropped}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("video"), qint64(5));
    }

    SnapTray::WindowTimeline timeline;
    timeline.setFrameSize(QSize(16, 16));
    QVERIFY(SnapTray::WindowTimelineSidecar::write(uncropped, timeline));

    QSignalSpy saved(m_manager, &RecordingManager::recordingStopped);
    m_manager->triggerSaveDialog(cropped, QSize(640, 360));
    QCOMPARE(saved.count(), 1);
    QCOMPARE(saved.at(0).first().toString(), dir.filePath("640x360.gif"));

    m_manager->triggerSaveDialog(uncropped);
    QCOMPARE(saved.count(), 2);
    QCOMPARE(saved.at(1).first().toString(), dir.filePath("1440x900.mp4"));
    // The saved recording takes its sidecar with it.
    QVERIFY(!QFile::exists(SnapTray::WindowTimelineSidecar::pathFor(uncropped)));
}

namespace {
SnapTray::WindowTimelineRecorder::Enumerator fakeEnumerator(int* calls)
{
    return [calls]() {
        ++*calls;
        DetectedElement e;
        e.bounds = QRect(10, 10, 100, 100);
        e.ownerApp = QStringLiteral("Code");
        e.windowId = 7;
        return std::vector<DetectedElement>{e};
    };
}
} // namespace

void TestRecordingManagerLifecycle::testWindowTimelineOnlyRecordedForPreview()
{
    int calls = 0;
    m_manager->m_createWindowEnumerator = [&calls](QScreen*) { return fakeEnumerator(&calls); };
    m_manager->m_windowFrameMapping = {QRect(0, 0, 1000, 500), QRect(), 2.0, QRect(0, 0, 2000, 1000)};
    m_manager->m_elapsedTimer.start();

    m_manager->m_startSettings.showPreview = false;
    m_manager->startWindowTimeline();
    QVERIFY(!m_manager->m_windowTimelineRecorder);
    QCOMPARE(calls, 0);

    m_manager->m_startSettings.showPreview = true;
    m_manager->startWindowTimeline();
    QVERIFY(m_manager->m_windowTimelineRecorder);
    QCOMPARE(calls, 1); // sampled immediately
    const auto timeline = m_manager->m_windowTimelineRecorder->timeline();
    QCOMPARE(timeline.frameSize(), QSize(2000, 1000));
    QCOMPARE(timeline.entries().front().windows.front().rect, QRect(20, 20, 200, 200));
    m_manager->m_windowTimelineRecorder.reset();
}

void TestRecordingManagerLifecycle::testWindowTimelinePausesWithRecording()
{
    int calls = 0;
    m_manager->m_createWindowEnumerator = [&calls](QScreen*) { return fakeEnumerator(&calls); };
    m_manager->m_windowFrameMapping = {QRect(0, 0, 1000, 500), QRect(), 1.0, QRect(0, 0, 1000, 500)};
    m_manager->m_startSettings.showPreview = true;
    m_manager->m_elapsedTimer.start();
    m_manager->startWindowTimeline();
    QVERIFY(m_manager->m_windowTimelineRecorder->isRunning());

    // pauseRecording()/resumeRecording() are state-gated; drive the recorder the way they do.
    m_manager->m_state = RecordingManager::State::Recording;
    m_manager->pauseRecording();
    QVERIFY(!m_manager->m_windowTimelineRecorder->isRunning());
    m_manager->resumeRecording();
    QVERIFY(m_manager->m_windowTimelineRecorder->isRunning());
    m_manager->m_state = RecordingManager::State::Idle;
    m_manager->m_windowTimelineRecorder.reset();
}

void TestRecordingManagerLifecycle::testFinishWritesSidecarOnlyOnSuccess()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString output = dir.filePath(QStringLiteral("rec.mp4"));
    int calls = 0;
    m_manager->m_createWindowEnumerator = [&calls](QScreen*) { return fakeEnumerator(&calls); };
    m_manager->m_windowFrameMapping = {QRect(0, 0, 1000, 500), QRect(), 1.0, QRect(0, 0, 1000, 500)};
    m_manager->m_startSettings.showPreview = true;
    m_manager->m_elapsedTimer.start();

    m_manager->startWindowTimeline();
    m_manager->finishWindowTimeline(output, false);
    QVERIFY(!QFile::exists(SnapTray::WindowTimelineSidecar::pathFor(output)));
    QVERIFY(!m_manager->m_windowTimelineRecorder);

    m_manager->startWindowTimeline();
    m_manager->finishWindowTimeline(output, true);
    QVERIFY(!m_manager->m_windowTimelineRecorder);
    const auto sidecar = SnapTray::WindowTimelineSidecar::read(output);
    QVERIFY(sidecar.has_value());
    QCOMPARE(sidecar->frameSize(), QSize(1000, 500));
    QCOMPARE(sidecar->entries().front().windows.front().ownerApp, QStringLiteral("Code"));

    // Without a recorder (direct save) finishing writes nothing.
    QFile::remove(SnapTray::WindowTimelineSidecar::pathFor(output));
    m_manager->finishWindowTimeline(output, true);
    QVERIFY(!QFile::exists(SnapTray::WindowTimelineSidecar::pathFor(output)));
}

void TestRecordingManagerLifecycle::testFinishSkipsSidecarWithoutWindows()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString output = dir.filePath(QStringLiteral("rec.mp4"));
    m_manager->m_createWindowEnumerator = [](QScreen*) {
        return SnapTray::WindowTimelineRecorder::Enumerator([]() { return std::vector<DetectedElement>{}; });
    };
    m_manager->m_windowFrameMapping = {QRect(0, 0, 1000, 500), QRect(), 1.0, QRect(0, 0, 1000, 500)};
    m_manager->m_startSettings.showPreview = true;
    m_manager->m_elapsedTimer.start();

    m_manager->startWindowTimeline();
    QVERIFY(m_manager->m_windowTimelineRecorder);
    m_manager->finishWindowTimeline(output, true);
    QVERIFY(!m_manager->m_windowTimelineRecorder);
    QVERIFY(!QFile::exists(SnapTray::WindowTimelineSidecar::pathFor(output)));
}

void TestRecordingManagerLifecycle::testStaleSidecarsAreCleanedUp()
{
    const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString stale = QDir(tempDir).filePath(QStringLiteral("SnapTray_Recording_stale-test.mp4.windows.json"));
    const QString fresh = QDir(tempDir).filePath(QStringLiteral("SnapTray_Recording_fresh-test.mp4.windows.json"));
    for (const QString& path : {stale, fresh}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{}");
    }
    {
        QFile file(stale);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(QDateTime::currentDateTime().addDays(-2), QFileDevice::FileModificationTime));
    }
    m_manager->cleanupStaleTempFiles();
    QVERIFY(!QFile::exists(stale));
    QVERIFY(QFile::exists(fresh));
    QFile::remove(fresh);
}

QTEST_MAIN(TestRecordingManagerLifecycle)
void TestRecordingManagerLifecycle::intermediateQualityFollowsFreeSpace_data()
{
    QTest::addColumn<qint64>("freeBytes");
    QTest::addColumn<bool>("expectIntermediate");
    QTest::addColumn<int>("expectWarnings");
    const QSize frame(1920, 1080);
    const qint64 needed = SnapTray::IntermediateQuality::estimatedBytesPerMinute(
                              SnapTray::IntermediateQuality::intermediateBitrate(frame, 30))
                          * SnapTray::IntermediateQuality::kMinimumRecordingMinutes;
    QTest::newRow("enough space") << needed << true << 0;
    QTest::newRow("one byte short") << needed - 1 << false << 1;
    QTest::newRow("unknown space") << qint64(-1) << false << 1;
}

void TestRecordingManagerLifecycle::intermediateQualityFollowsFreeSpace()
{
    QFETCH(qint64, freeBytes);
    QFETCH(bool, expectIntermediate);
    QFETCH(int, expectWarnings);
    RecordingManager manager;
    manager.m_frameRate = 30;
    QString queriedPath;
    manager.m_freeBytesForPath = [&queriedPath, freeBytes](const QString& path) {
        queriedPath = path;
        return freeBytes;
    };
    QSignalSpy warnings(&manager, &RecordingManager::recordingWarning);
    const QString directory = QStringLiteral("C:/tmp/recordings");
    QCOMPARE(manager.chooseIntermediateQuality(directory, QSize(1920, 1080)), expectIntermediate);
    QCOMPARE(queriedPath, directory);
    QCOMPARE(warnings.count(), expectWarnings);
    if (expectWarnings) {
        QCOMPARE(warnings.first().at(0).toString(), RecordingManager::tr(
            "Not enough free disk space for high-quality recording. Recording at the selected quality instead."));
    }
}

void TestRecordingManagerLifecycle::previewRequestedCarriesIntermediateDecision_data()
{
    QTest::addColumn<qint64>("freeBytes");
    QTest::addColumn<bool>("expectIntermediate");
    const QSize frame(1920, 1080);
    const qint64 needed = SnapTray::IntermediateQuality::estimatedBytesPerMinute(
                              SnapTray::IntermediateQuality::intermediateBitrate(frame, 30))
                          * SnapTray::IntermediateQuality::kMinimumRecordingMinutes;
    QTest::newRow("ample space") << needed * 10 << true;
    QTest::newRow("low disk") << needed - 1 << false;
}

void TestRecordingManagerLifecycle::previewRequestedCarriesIntermediateDecision()
{
    QFETCH(qint64, freeBytes);
    QFETCH(bool, expectIntermediate);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    m_manager->m_frameRate = 30;
    m_manager->m_freeBytesForPath = [freeBytes](const QString&) { return freeBytes; };
    m_manager->m_startSettings.showPreview = true;

    // The decision beginAsyncInitialization() makes for a preview-on start...
    QCOMPARE(m_manager->decideIntermediateQuality(true, dir.path(), QSize(1920, 1080)), expectIntermediate);

    // ...reaches the preview when encoding finishes.
    QSignalSpy previewSpy(m_manager, &RecordingManager::previewRequested);
    m_manager->m_state = RecordingManager::State::Encoding;
    const QString output = dir.filePath(QStringLiteral("rec.mp4"));
    m_manager->onEncodingFinished(true, output);
    QTRY_COMPARE(previewSpy.count(), 1);
    QCOMPARE(previewSpy.first().at(0).toString(), output);
    QCOMPARE(previewSpy.first().at(2).toBool(), expectIntermediate);

    // A new start forgets the previous decision.
    m_manager->initializeStartState();
    QVERIFY(!m_manager->m_recordedAsIntermediate);
    m_manager->m_state = RecordingManager::State::Idle;
}

#include "tst_Lifecycle.moc"
