#include <QtTest/QtTest>
#include <QSignalSpy>

#include "RecordingManager.h"
#include "capture/IAudioCaptureEngine.h"
#include "recording/AudioTracks.h"
#include "AudioTrackWriterTestAccess.h"
#include <QtEndian>
#include "settings/Settings.h"
#include "settings/FileSettingsManager.h"
#include <QFile>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QDir>
#include "recording/WindowTimelineSidecar.h"
#include "recording/WindowTimelineRecorder.h"
#include "recording/CursorTimeline.h"
#include "recording/RecordingSession.h"
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
    void testCursorTimelineFollowsTheRecording();
    void testCursorPauseIsStampedAtTheBoundary();
    void testKeyTimelineOnlyWhenSwitchedOn();
    void testSeparateTracksOnlyWithBothSources();
    void testSeparateTracksDroppedOnFinalFlushFailure_data();
    void testSeparateTracksDroppedOnFinalFlushFailure();
    void testCursorTimelineOutcome_data();
    void testCursorTimelineOutcome();
    void intermediateQualityFollowsFreeSpace_data();
    void intermediateQualityFollowsFreeSpace();
    void previewRequestedCarriesIntermediateDecision_data();
    void previewRequestedCarriesIntermediateDecision();
    void previewOffNeverQueriesDiskSpace();
    void blocksCaptureOnlyWhileRecordingWork();

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
    // Pointer sidecars: an old one goes, unless its recording awaits recovery.
    const QString staleCursor = QDir(tempDir).filePath(QStringLiteral("SnapTray_Recording_stale-c.mp4.cursor.jsonl"));
    const QString pendingVideo = QDir(tempDir).filePath(QStringLiteral("SnapTray_Recording_pending-c.mp4"));
    const QString pendingCursor = SnapTray::CursorTimelineSidecar::pathFor(pendingVideo);
    SnapTray::RecordingSessionInfo pending;
    pending.videoPath = pendingVideo;
    QVERIFY(SnapTray::RecordingSession::write(pending));
    for (const QString& path : {staleCursor, pendingCursor}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{}");
        file.close();
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(QDateTime::currentDateTime().addDays(-2), QFileDevice::FileModificationTime));
    }
    m_manager->cleanupStaleTempFiles();
    QVERIFY(!QFile::exists(stale));
    QVERIFY(QFile::exists(fresh));
    QVERIFY(!QFile::exists(staleCursor));
    QVERIFY(QFile::exists(pendingCursor));
    QFile::remove(fresh);
    SnapTray::RecordingSession::discard(pendingVideo);
    QVERIFY(!QFile::exists(pendingCursor));
}

namespace {
class LifecycleCursorProbe : public SnapTray::CursorProbe
{
public:
    Capabilities capabilities() const override { return {false, true}; }
    std::optional<State> poll() override { return State{QPoint(5, 5), false, 0}; }
    QString name() const override { return QStringLiteral("test"); }
    void click(bool down) { emit buttonChanged(Qt::LeftButton, down); }
};

// A probe whose position query can block, like XQueryPointer on a busy server.
class SlowCursorProbe : public LifecycleCursorProbe
{
public:
    std::optional<State> poll() override
    {
        if (slowMs > 0) QThread::msleep(static_cast<unsigned long>(slowMs));
        return State{position, false, 0};
    }
    int slowMs = 0;
    QPoint position{5, 5};
};
} // namespace

void TestRecordingManagerLifecycle::testCursorTimelineFollowsTheRecording()
{
    QTemporaryDir dir;
    const QString video = dir.filePath(QStringLiteral("SnapTray_Recording_c.mp4"));
    LifecycleCursorProbe* probe = nullptr;
    m_manager->m_createCursorProbe = [&probe] {
        auto owned = std::make_unique<LifecycleCursorProbe>();
        probe = owned.get();
        return owned;
    };
    m_manager->m_windowFrameMapping = {QRect(0, 0, 1000, 500), QRect(), 2.0, QRect(0, 0, 2000, 1000)};
    m_manager->m_session.videoPath = video;
    m_manager->m_elapsedTimer.start();

    // Without a preview there is no use for it.
    m_manager->m_startSettings.showPreview = false;
    m_manager->startCursorTimeline();
    QVERIFY(!m_manager->m_cursorRecorder);
    QVERIFY(!probe);

    m_manager->m_startSettings.showPreview = true;
    m_manager->startCursorTimeline();
    QVERIFY(m_manager->m_cursorRecorder && m_manager->m_cursorRecorder->isRunning());
    QVERIFY(m_manager->m_cursorDiagnostics.contains(QStringLiteral("shapes no, clicks yes")));
    QVERIFY(QFile::exists(SnapTray::CursorTimelineSidecar::pathFor(video)));

    // Pauses with the recording; a held button is released at the pause.
    m_manager->m_state = RecordingManager::State::Recording;
    probe->click(true);
    m_manager->pauseRecording();
    QVERIFY(!m_manager->m_cursorRecorder->isRunning());
    m_manager->resumeRecording();
    QVERIFY(m_manager->m_cursorRecorder->isRunning());

    m_manager->stopFrameCapture();
    const auto timeline = SnapTray::CursorTimelineSidecar::read(video);
    QVERIFY(timeline && timeline->finished);
    QCOMPARE(timeline->samples.front().position, QPoint(10, 10));
    QCOMPARE(int(timeline->buttons.size()), 2);
    QVERIFY(timeline->buttons.back().synthetic);

    // Cancelling deletes the recording, and the sidecar with it.
    m_manager->cancelRecording();
    QCOMPARE(m_manager->state(), RecordingManager::State::Idle);
    QVERIFY(!QFile::exists(SnapTray::CursorTimelineSidecar::pathFor(video)));
    m_manager->m_session = {};
}

namespace {
class LifecycleKeyProbe : public SnapTray::KeyProbe
{
public:
    QString name() const override { return QStringLiteral("test"); }
    void press(int key, Qt::KeyboardModifiers modifiers) { emit keyPressed(key, modifiers, QString()); }
};
} // namespace

// #187: no key is kept unless switched on before the recording; then the
// sidecar follows the recording and goes with it.
void TestRecordingManagerLifecycle::testKeyTimelineOnlyWhenSwitchedOn()
{
    QTemporaryDir dir;
    const QString video = dir.filePath(QStringLiteral("SnapTray_Recording_k.mp4"));
    m_manager->m_createCursorProbe = [] { return std::make_unique<LifecycleCursorProbe>(); };
    LifecycleKeyProbe* keys = nullptr;
    int probesMade = 0;
    m_manager->m_createKeyProbe = [&keys, &probesMade] {
        ++probesMade;
        auto owned = std::make_unique<LifecycleKeyProbe>();
        keys = owned.get();
        return owned;
    };
    m_manager->m_windowFrameMapping = {QRect(0, 0, 1000, 500), QRect(), 1.0, QRect(0, 0, 1000, 500)};
    m_manager->m_session.videoPath = video;
    m_manager->m_startSettings.showPreview = true;
    m_manager->m_elapsedTimer.start();

    // Off (the default): no probe is even created, no file, nothing said.
    m_manager->m_startSettings.keyRecordingMode = 0;
    m_manager->startCursorTimeline();
    QCOMPARE(probesMade, 0);
    QVERIFY(!m_manager->isRecordingKeys());
    QVERIFY(!QFile::exists(SnapTray::KeyTimelineSidecar::pathFor(video)));
    m_manager->dropCursorTimeline(true);

    m_manager->m_startSettings.keyRecordingMode = int(SnapTray::KeyRecordingMode::Shortcuts);
    m_manager->startCursorTimeline();
    QVERIFY(keys);
    QVERIFY(m_manager->isRecordingKeys());
    m_manager->m_state = RecordingManager::State::Recording;
    keys->press(Qt::Key_C, Qt::ControlModifier);
    keys->press(Qt::Key_A, Qt::NoModifier); // typing: not kept in this mode
    m_manager->pauseRecording();
    keys->press(Qt::Key_V, Qt::ControlModifier); // paused: not kept
    m_manager->resumeRecording();
    keys->press(Qt::Key_Return, Qt::NoModifier);
    m_manager->stopFrameCapture();
    const auto timeline = SnapTray::KeyTimelineSidecar::read(video);
    QVERIFY(timeline && timeline->finished);
    QCOMPARE(int(timeline->strokes.size()), 2);
    QCOMPARE(timeline->strokes[0].key, int(Qt::Key_C));
    QCOMPARE(timeline->strokes[1].key, int(Qt::Key_Return));

    // Cancelling deletes the recording, and the key sidecar with it.
    m_manager->cancelRecording();
    QVERIFY(!QFile::exists(SnapTray::KeyTimelineSidecar::pathFor(video)));
    m_manager->m_session = {};
}

namespace {
class TrackAudioEngine final : public IAudioCaptureEngine
{
public:
    bool setAudioSource(AudioSource source) override { m_source = source; return true; }
    AudioSource audioSource() const override { return m_source; }
    bool setDevice(const QString&) override { return true; }
    AudioFormat audioFormat() const override { return {}; }
    QList<AudioDevice> availableInputDevices() const override { return {}; }
    QString defaultInputDevice() const override { return {}; }
    bool start() override { return true; }
    void stop() override {}
    void pause() override {}
    void resume() override {}
    bool isRunning() const override { return true; }
    bool isPaused() const override { return false; }
    QString engineName() const override { return "test"; }
    bool isAvailable() const override { return true; }
    bool isSystemAudioSupported() const override { return true; }
    bool supportsSeparateTracks() const override { return true; }
    bool separateTracks() const { return m_separateTracks; }
    void send(qint16 mic, qint16 system, qint64 frame)
    {
        QByteArray a(4, '\0'), b(4, '\0');
        qToLittleEndian<qint16>(mic, a.data());
        qToLittleEndian<qint16>(system, b.data());
        emit sourceAudioReady(a, b, frame);
    }
};
} // namespace

// #189: two tracks are written only for a previewed recording with both
// sources, on the mixed track's frames, and go with the recording.
void TestRecordingManagerLifecycle::testSeparateTracksOnlyWithBothSources()
{
    QTemporaryDir dir;
    const QString video = dir.filePath(QStringLiteral("SnapTray_Recording_a.mp4"));
    m_manager->m_session.videoPath = video;
    m_manager->m_startSettings.showPreview = true;
    auto* engine = new TrackAudioEngine;
    m_manager->m_audioEngine = AudioEnginePtr(engine);

    engine->setAudioSource(IAudioCaptureEngine::AudioSource::Microphone);
    m_manager->startAudioTracks();
    QVERIFY(!engine->separateTracks());
    QVERIFY(!QFile::exists(SnapTray::AudioTracks::pathFor(video, SnapTray::AudioTracks::Track::Microphone)));

    engine->setAudioSource(IAudioCaptureEngine::AudioSource::Both);
    m_manager->m_startSettings.showPreview = false; // no preview: nobody would mix them
    m_manager->startAudioTracks();
    QVERIFY(!engine->separateTracks());

    m_manager->m_startSettings.showPreview = true;
    m_manager->startAudioTracks();
    QVERIFY(engine->separateTracks());
    QVERIFY(SnapTray::AudioTracks::exist(video));
    // The recording starts its pointer timeline right after the audio
    // (startRecordingAfterCountdown): that must not close the tracks
    // (PR #202 review).
    m_manager->startCursorTimeline();
    m_manager->dropCursorTimeline(true);
    engine->send(1000, -2000, 0);
    engine->send(3000, 4000, 10);
    m_manager->closeAudioTracks();
    SnapTray::AudioTrackReader mic;
    QVERIFY(mic.open(SnapTray::AudioTracks::pathFor(video, SnapTray::AudioTracks::Track::Microphone)));
    QCOMPARE(mic.frames(), 11);
    const QByteArray at10 = mic.read(10, 1);
    QCOMPARE(qFromLittleEndian<qint16>(at10.constData()), qint16(3000));

    // Dropped with the recording.
    m_manager->m_cursorSidecarVideo = video;
    m_manager->dropRecordingSidecars(true);
    QVERIFY(!SnapTray::AudioTracks::exist(video));

    // A finished recording keeps its tracks for the preview...
    m_manager->startAudioTracks();
    engine->send(1000, -2000, 0);
    m_manager->dropRecordingSidecars(false);
    QVERIFY(SnapTray::AudioTracks::exist(video));
    // ...unless one stopped writing during the recording (PR #202 review):
    // then it keeps no tracks, only its own complete mixed audio.
    m_manager->startAudioTracks();
    engine->send(1000, -2000, 0);
    m_manager->m_systemAudioTrack->failForTests();
    m_manager->dropRecordingSidecars(false);
    QVERIFY(!SnapTray::AudioTracks::exist(video));
    m_manager->m_audioEngine.reset();
    m_manager->m_session = {};
}

void TestRecordingManagerLifecycle::testSeparateTracksDroppedOnFinalFlushFailure_data()
{
    QTest::addColumn<bool>("microphoneFails");
    QTest::newRow("microphone") << true;
    QTest::newRow("system audio") << false;
}

void TestRecordingManagerLifecycle::testSeparateTracksDroppedOnFinalFlushFailure()
{
#ifndef Q_OS_LINUX
    QSKIP("Descriptor fault injection requires Linux");
#else
    QFETCH(bool, microphoneFails);
    using namespace SnapTray;
    QTemporaryDir dir;
    const QString video = dir.filePath(QStringLiteral("recording.mp4"));
    const QByteArray mixedRecording("recording with mixed audio");
    QFile recording(video);
    QVERIFY(recording.open(QIODevice::WriteOnly));
    QCOMPARE(recording.write(mixedRecording), mixedRecording.size());
    recording.close();

    m_manager->m_audioTracksVideo = video;
    const QString micPath = AudioTracks::pathFor(video, AudioTracks::Track::Microphone);
    const QString systemPath = AudioTracks::pathFor(video, AudioTracks::Track::SystemAudio);
    const auto mic = std::make_shared<AudioTrackWriter>(micPath);
    const auto system = std::make_shared<AudioTrackWriter>(systemPath);
    m_manager->m_microphoneTrack = mic;
    m_manager->m_systemAudioTrack = system;
    for (const auto& track : {mic, system}) {
        QVERIFY(track->open());
        track->write(QByteArray(AudioTracks::kBytesPerFrame, '\1'), 0);
        QVERIFY(!track->failed());
    }
    const auto failing = microphoneFails ? mic : system;
    QVERIFY(AudioTrackWriterTestAccess::makeReadOnly(*failing));
    QVERIFY(!failing->failed()); // failure is discovered only during finalization
    m_manager->closeAudioTracks();
    QCOMPARE(mic->failed(), microphoneFails);
    QCOMPARE(system->failed(), !microphoneFails);
    QVERIFY(!m_manager->m_microphoneTrack);
    QVERIFY(!m_manager->m_systemAudioTrack);
    QVERIFY(!QFile::exists(micPath));
    QVERIFY(!QFile::exists(systemPath));
    QVERIFY(recording.open(QIODevice::ReadOnly));
    QCOMPARE(recording.readAll(), mixedRecording);
#endif
}

void TestRecordingManagerLifecycle::testCursorPauseIsStampedAtTheBoundary()
{
    // PR #174 review: a slow final poll at pause used to stamp the pause
    // events after the boundary, later than the first sample after resume.
    QTemporaryDir dir;
    const QString video = dir.filePath(QStringLiteral("SnapTray_Recording_p.mp4"));
    SlowCursorProbe* probe = nullptr;
    m_manager->m_createCursorProbe = [&probe] {
        auto owned = std::make_unique<SlowCursorProbe>();
        probe = owned.get();
        return owned;
    };
    m_manager->m_windowFrameMapping = {QRect(0, 0, 1000, 500), QRect(), 1.0, QRect(0, 0, 1000, 500)};
    m_manager->m_session.videoPath = video;
    m_manager->m_startSettings.showPreview = true;
    m_manager->m_elapsedTimer.start();
    m_manager->startCursorTimeline();
    QVERIFY(probe);
    m_manager->m_state = RecordingManager::State::Recording;
    QTest::qWait(20);
    probe->click(true);
    probe->position = QPoint(6, 6);
    probe->slowMs = 120;
    m_manager->pauseRecording();
    probe->slowMs = 0;
    probe->position = QPoint(7, 7);
    m_manager->resumeRecording();
    m_manager->stopFrameCapture();

    const auto timeline = SnapTray::CursorTimelineSidecar::read(video);
    QVERIFY(timeline);
    for (size_t i = 1; i < timeline->samples.size(); ++i) {
        QVERIFY2(timeline->samples[i - 1].tMs <= timeline->samples[i].tMs,
                 qPrintable(QStringLiteral("sample %1 at %2 after %3").arg(i)
                                .arg(timeline->samples[i].tMs).arg(timeline->samples[i - 1].tMs)));
    }
    QCOMPARE(int(timeline->buttons.size()), 2);
    const auto release = timeline->buttons.back();
    QVERIFY(release.synthetic);
    QCOMPARE(timeline->samples.back().position, QPoint(7, 7));
    QVERIFY(release.tMs <= timeline->samples.back().tMs);
    m_manager->cancelRecording();
    m_manager->m_session = {};
}

void TestRecordingManagerLifecycle::testCursorTimelineOutcome_data()
{
    QTest::addColumn<bool>("success");
    QTest::addColumn<bool>("crashSafe");
    QTest::addColumn<bool>("kept");
    QTest::newRow("finished") << true << false << true;
    QTest::newRow("failed, part kept for recovery") << false << true << true;
    QTest::newRow("failed, nothing usable") << false << false << false;
}

void TestRecordingManagerLifecycle::testCursorTimelineOutcome()
{
    QFETCH(bool, success);
    QFETCH(bool, crashSafe);
    QFETCH(bool, kept);
    QTemporaryDir dir;
    const QString video = dir.filePath(QStringLiteral("SnapTray_Recording_o.mp4"));
    {
        QFile file(video);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("partial");
    }
    m_manager->m_createCursorProbe = [] { return std::make_unique<LifecycleCursorProbe>(); };
    m_manager->m_windowFrameMapping = {QRect(0, 0, 1000, 500), QRect(), 1.0, QRect(0, 0, 1000, 500)};
    m_manager->m_session.videoPath = video;
    m_manager->m_session.fragmented = crashSafe;
    // With a preview, success hands the file on (previewRequested; nothing listens here).
    m_manager->m_startSettings.showPreview = true;
    m_manager->m_elapsedTimer.start();
    m_manager->startCursorTimeline();
    m_manager->stopFrameCapture();
    m_manager->m_state = RecordingManager::State::Encoding;
    m_manager->onEncodingFinished(success, video);
    QCOMPARE(QFile::exists(SnapTray::CursorTimelineSidecar::pathFor(video)), kept);
    QVERIFY(!m_manager->m_cursorRecorder);
    QCoreApplication::processEvents(); // the queued preview hand-off
    m_manager->m_state = RecordingManager::State::Idle;
    m_manager->m_session = {};
    SnapTray::CursorTimelineSidecar::remove(video);
}

void TestRecordingManagerLifecycle::blocksCaptureOnlyWhileRecordingWork()
{
    struct Row { RecordingManager::State state; bool blocks; bool active; };
    const Row rows[] = {
        {RecordingManager::State::Idle, false, false},
        {RecordingManager::State::Preparing, true, true},
        {RecordingManager::State::Countdown, true, true},
        {RecordingManager::State::Recording, true, true},
        {RecordingManager::State::Paused, true, true},
        {RecordingManager::State::Encoding, true, true},
        // The preview is an ordinary window: screenshots stay available while
        // it is open, but a new recording does not start (isActive stays true).
        {RecordingManager::State::Previewing, false, true},
    };
    for (const Row& row : rows) {
        m_manager->m_state = row.state;
        QVERIFY2(m_manager->blocksCapture() == row.blocks,
                 qPrintable(QStringLiteral("blocksCapture() for state %1").arg(int(row.state))));
        QVERIFY2(m_manager->isActive() == row.active,
                 qPrintable(QStringLiteral("isActive() for state %1").arg(int(row.state))));
    }
    m_manager->m_state = RecordingManager::State::Idle;
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

void TestRecordingManagerLifecycle::previewOffNeverQueriesDiskSpace()
{
    int seamCalls = 0;
    m_manager->m_frameRate = 30;
    m_manager->m_freeBytesForPath = [&seamCalls](const QString&) {
        ++seamCalls;
        return qint64(0); // would warn if it were ever consulted
    };
    m_manager->m_startSettings.showPreview = false;
    m_manager->m_recordedAsIntermediate = true;
    QSignalSpy warnings(m_manager, &RecordingManager::recordingWarning);

    // The same call beginAsyncInitialization() makes when building the init config.
    QCOMPARE(m_manager->decideIntermediateQuality(m_manager->m_startSettings.showPreview,
                                                  QStringLiteral("C:/tmp/recordings"), QSize(1920, 1080)),
             false);
    QCOMPARE(seamCalls, 0);
    QCOMPARE(warnings.count(), 0);
    QVERIFY(!m_manager->m_recordedAsIntermediate);
}

#include "tst_Lifecycle.moc"
