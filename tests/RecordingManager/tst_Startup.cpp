#include <QtTest>
#include <QFile>
#include <QDir>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QUuid>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QScreen>
#include <QGuiApplication>
#include <QTimer>
#include "RecordingManager.h"
#include "settings/FileSettingsManager.h"
#include "recording/RecordingProject.h"
#include "recording/RecordingSession.h"
#include "settings/RecordingSettingsManager.h"
#include "RecordingInitTask.h"
#include "qml/QmlRecordingControlBar.h"
#include "../Encoding/FakeAudioEncoder.h"
#include <QQuickItem>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

namespace {
struct AudioCaptureTestState { int created = 0; int destroyed = 0; };
class FakeStartupAudio final : public IAudioCaptureEngine
{
public:
    explicit FakeStartupAudio(std::shared_ptr<AudioCaptureTestState> state) : m_state(std::move(state)) { ++m_state->created; }
    ~FakeStartupAudio() override { ++m_state->destroyed; }
    bool setAudioSource(AudioSource source) override { m_source = source; return true; }
    AudioSource audioSource() const override { return m_source; }
    bool setDevice(const QString& device) override { m_deviceId = device; return true; }
    AudioFormat audioFormat() const override { return {}; }
    QList<AudioDevice> availableInputDevices() const override { return {{"saved-device", "Test microphone", true}}; }
    QString defaultInputDevice() const override { return "saved-device"; }
    bool start() override { m_running = true; return true; }
    void stop() override { m_running = false; }
    void pause() override {}
    void resume() override {}
    bool isRunning() const override { return m_running; }
    bool isPaused() const override { return false; }
    QString engineName() const override { return "test"; }
    bool isAvailable() const override { return true; }
    bool isSystemAudioSupported() const override { return true; }
    void sendPcm() { emit audioDataReady(QByteArray(4, '\0'), 0); }
private:
    std::shared_ptr<AudioCaptureTestState> m_state;
    bool m_running = false;
};
class FakeStartupCapture final : public ICaptureEngine
{
public:
    bool setRegion(const QRect&, QScreen*) override { return true; }
    bool start() override { return true; }
    void stop() override {}
    bool isRunning() const override { return true; }
    QImage captureFrame() override { QImage image(16,16,QImage::Format_RGB32); image.fill(Qt::black); return image; }
    QString engineName() const override { return "test"; }
};
}

class TestRecordingStartup : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void sessionClockFollowsRecordingLifecycle();
    void sustainedFrameLossWarnsOnce();
    void startRefusedWhenDiskIsFull();
    void diskSamplesWarnAndStopSafely();
    void sessionManifestFollowsTheRecording();
    void staleCleanupSparesPendingRecoveries();
    void recoveredRecordingOpensInPreview();
    void savesNeverGoIntoTheProjectsFolder();
    void interruptionsKeepTheRecording_data();
    void interruptionsKeepTheRecording();
    void concurrentStopsEndOnce();
    void interruptionBeforeRecordingCancels();
    void userStopIsNotAnInterruption();
    void failedFinishSaysWhatWasKept_data();
    void failedFinishSaysWhatWasKept();
    void microphoneLossIsReported();
    void recoveredAnimationIsSavedDirectly();
    void recoveredRecordingKeepsItsQuality();
    void saveDialogSessionSurvivesANewRecording();
    void bufferedAnimationCountsTowardTheDiskGuard();
    void encoderWrittenBytesFeedTheDiskGuard();
    void controlBarOffersPauseResumeAndStop();
    void waitingKeepsEventLoopResponsiveAndSettingsSnapshot();
    void permissionPolicy_data();
    void permissionPolicy();
    void staleAndDuplicateRepliesCannotStart();
    void missingScreenAndDestructionRejectReplies();
    void permissionWaitIsOutsideTimelineAndOldTimerIsIgnored();
    void encoderCapabilityControlsAudioPipeline_data();
    void encoderCapabilityControlsAudioPipeline();
    void lostSystemAudioDeviceKeepsTheMicrophone();
    void captureExclusionWarning_data();
    void captureExclusionWarning();
    void nativeStreamStopFinishesRecording_data();
    void nativeStreamStopFinishesRecording();
    void basicBackendIgnoresUnsupportedPreferences();
private:
    void prepare(RecordingManager& manager);
    // Brings `manager` into Recording with fake capture and encoder.
    void startFakeRecording(RecordingManager& manager, std::shared_ptr<AudioEncoderTestState> encoderState);
    void startFakeRecordingForDisk(RecordingManager& manager, IVideoEncoder::ByteCounter written = nullptr);
};

void TestRecordingStartup::nativeStreamStopFinishesRecording_data()
{
    QTest::addColumn<bool>("userStopped");
    QTest::addColumn<bool>("paused");
    QTest::newRow("user-recording") << true << false;
    QTest::newRow("user-paused") << true << true;
    QTest::newRow("native-error") << false << false;
}

void TestRecordingStartup::nativeStreamStopFinishesRecording()
{
    QFETCH(bool, userStopped);
    QFETCH(bool, paused);
    RecordingSettingsManager::instance().setAudioEnabled(false);
    RecordingManager manager;
    prepare(manager);
    auto encoderState = std::make_shared<AudioEncoderTestState>();
    RecordingInitTask::Config config;
    config.region = QRect(0, 0, 16, 16);
    config.screenInfo.geometry = config.region;
    config.frameSize = QSize(16, 16);
    config.outputPath = "unused-native-stop-test.mp4";
    auto task = QSharedPointer<RecordingInitTask>::create(config);
    task->m_createEncoder = [encoderState](const auto& options, QObject* parent) {
        return EncoderFactoryTestAccess::create(options, parent, encoderState);
    };
    QVERIFY(task->initializeEncoder());
    task->result().success = true;
    task->result().captureEngine = std::make_unique<FakeStartupCapture>();
    task->result().captureEngineStarted = true;
    manager.m_initTask = task;
    manager.onInitializationComplete(task, manager.m_initGeneration);
    QCOMPARE(manager.state(), RecordingManager::State::Recording);
    if (paused) manager.pauseRecording();
    QSignalSpy errors(&manager, &RecordingManager::recordingError);
    QSignalSpy warnings(&manager, &RecordingManager::recordingWarning);
    QSignalSpy cancelled(&manager, &RecordingManager::recordingCancelled);
    QSignalSpy interrupted(&manager, &RecordingManager::recordingInterrupted);
    if (userStopped) manager.m_captureEngine->stoppedByUser();
    else manager.m_captureEngine->error(QStringLiteral("native stream failed"));
    QCOMPARE(manager.state(), RecordingManager::State::Encoding);
    QVERIFY(!manager.m_captureEngine);
    QVERIFY(!manager.m_captureTimer);
    // Either way the recording is finished and kept: an interruption with
    // its reason, not an error.
    QCOMPARE(errors.count(), 0);
    QCOMPARE(interrupted.count(), 1);
    QCOMPARE(interrupted.first().first().value<SnapTray::RecordingStopReason>(),
             userStopped ? SnapTray::RecordingStopReason::SystemControl : SnapTray::RecordingStopReason::CaptureFailed);
    QCOMPARE(warnings.count(), 1);
    QVERIFY(warnings.first().first().toString().contains(QStringLiteral("Everything recorded so far is kept")));
    if (!userStopped) QVERIFY(warnings.first().first().toString().contains(QStringLiteral("native stream failed")));
    QCOMPARE(cancelled.count(), 0);
    manager.cancelRecording();
}

void TestRecordingStartup::encoderCapabilityControlsAudioPipeline_data()
{
    QTest::addColumn<bool>("requested");
    QTest::addColumn<bool>("acceptsAudio");
    QTest::addColumn<bool>("starts");
    QTest::newRow("audio-ready") << true << true << true;
    QTest::newRow("silent-fallback") << true << false << true;
    QTest::newRow("silent-request") << false << true << true;
    QTest::newRow("encoder-failure") << true << true << false;
}

void TestRecordingStartup::encoderCapabilityControlsAudioPipeline()
{
    QFETCH(bool, requested);
    QFETCH(bool, acceptsAudio);
    QFETCH(bool, starts);
    RecordingSettingsManager::instance().setAudioEnabled(requested);
    constexpr int kBothSourcesSetting = 2;
    RecordingSettingsManager::instance().setAudioSource(kBothSourcesSetting);
    auto encoderState = std::make_shared<AudioEncoderTestState>();
    encoderState->acceptsAudio = acceptsAudio;
    encoderState->starts = starts;
    auto audioState = std::make_shared<AudioCaptureTestState>();
    RecordingManager manager;
    prepare(manager);
    manager.m_createAudioEngine = [audioState] { return new FakeStartupAudio(audioState); };
    RecordingInitTask::Config config;
    config.region = QRect(0,0,16,16);
    config.screenInfo.geometry = config.region;
    config.frameSize = QSize(16,16);
    config.audioEnabled = requested;
    config.outputPath = "unused-fake.mp4";
    auto task = QSharedPointer<RecordingInitTask>::create(config);
    task->m_createEncoder = [encoderState](const auto& options, QObject* parent) {
        return EncoderFactoryTestAccess::create(options, parent, encoderState);
    };
    QCOMPARE(task->initializeEncoder(), starts);
    QCOMPARE(task->result().audioEnabled, starts && requested && acceptsAudio);
    task->result().success = starts;
    if (starts) {
        task->result().captureEngine = std::make_unique<FakeStartupCapture>();
        task->result().captureEngineStarted = true;
    }
    manager.m_controlBar = new SnapTray::QmlRecordingControlBar();
    manager.m_controlBar->ensureView();
    QVERIFY(manager.m_controlBar->m_rootItem);
    manager.m_controlBar->setAudioEnabled(true);
    manager.m_initTask = task;
    QSignalSpy warnings(&manager, &RecordingManager::recordingWarning);
    QSignalSpy errors(&manager, &RecordingManager::recordingError);
    if (starts && requested && !acceptsAudio)
        manager.addStartupAudioWarning("A microphone source was unavailable.");
    manager.onInitializationComplete(task, manager.m_initGeneration);
    if (!starts) {
        QCOMPARE(manager.state(), RecordingManager::State::Idle);
        QCOMPARE(errors.count(), 1);
        QCOMPARE(warnings.count(), 0);
        QCOMPARE(audioState->created, 0);
        QCOMPARE(encoderState->destroyed.load(), 1);
        return;
    }
    QCOMPARE(manager.state(), RecordingManager::State::Recording);
    const bool audioEnabled = requested && acceptsAudio;
    using AudioState = SnapTray::AudioSourceState;
    {
        // The tray reads this persistent status; it follows actual results.
        const auto expected = !requested ? AudioState::Off
            : audioEnabled ? AudioState::Active : AudioState::Unavailable;
        QCOMPARE(manager.audioStatus().microphone, expected);
        QCOMPARE(manager.audioStatus().systemAudio, expected);
    }
    QCOMPARE(manager.m_audioEnabled, audioEnabled);
    QCOMPARE(manager.m_controlBar->m_rootItem->property("audioEnabled").toBool(), audioEnabled);
    QCOMPARE(audioState->created, audioEnabled ? 1 : 0);
    QCOMPARE(warnings.count(), requested && !acceptsAudio ? 1 : 0);
    if (requested && !acceptsAudio) {
        const QString warning = warnings.first().first().toString();
        QVERIFY(warning.contains("microphone source"));
        QVERIFY(warning.contains("silent"));
        manager.applyEncoderAudioResult(false, task->result().audioWarning);
        manager.flushStartupAudioWarnings();
        QCOMPARE(warnings.count(), 1);
    }
    if (audioEnabled) {
        auto* audio = static_cast<FakeStartupAudio*>(manager.m_audioEngine.get());
        audio->sendPcm();
        QTRY_COMPARE(encoderState->audioWrites.load(), 1);
        audio->activeSourceChanged(IAudioCaptureEngine::AudioSource::SystemAudio);
        QCOMPARE(warnings.count(), 1);
        QCOMPARE(manager.audioStatus().microphone, AudioState::Unavailable);
        QCOMPARE(manager.audioStatus().systemAudio, AudioState::Active);
        QCOMPARE(manager.state(), RecordingManager::State::Recording);
        QVERIFY(manager.m_audioEnabled);
        QVERIFY(manager.m_controlBar->m_rootItem->property("audioEnabled").toBool());
        audio->sendPcm();
        QTRY_COMPARE(encoderState->audioWrites.load(), 2);
        audio->activeSourceChanged(IAudioCaptureEngine::AudioSource::None);
        QCOMPARE(warnings.count(), 2);
        QCOMPARE(manager.audioStatus().microphone, AudioState::Unavailable);
        QCOMPARE(manager.audioStatus().systemAudio, AudioState::Unavailable);
        emit audio->deviceLost(IAudioCaptureEngine::AudioSource::Microphone);
        // The microphone was no longer recording: nothing new to report.
        QCOMPARE(warnings.count(), 2);
        QCOMPARE(manager.audioStatus().microphone, AudioState::DeviceMissing);
        QCOMPARE(manager.state(), RecordingManager::State::Recording);
        QVERIFY(!manager.m_audioEnabled);
        QVERIFY(!manager.m_controlBar->m_rootItem->property("audioEnabled").toBool());
        QCOMPARE(RecordingSettingsManager::instance().audioSource(), kBothSourcesSetting);
    } else {
        QVERIFY(!manager.m_audioEngine);
        QCOMPARE(encoderState->audioWrites.load(), 0);
    }
    QCOMPARE(RecordingSettingsManager::instance().audioEnabled(), requested);
    manager.cancelRecording();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTRY_COMPARE(encoderState->destroyed.load(), 1);
    QCOMPARE(audioState->destroyed, audioState->created);
}

void TestRecordingStartup::lostSystemAudioDeviceKeepsTheMicrophone()
{
    // Both sources; the playback endpoint fails (Windows reports deviceLost
    // for it too). The microphone is still recording and must not read as missing.
    RecordingSettingsManager::instance().setAudioEnabled(true);
    constexpr int kBothSourcesSetting = 2;
    RecordingSettingsManager::instance().setAudioSource(kBothSourcesSetting);
    auto encoderState = std::make_shared<AudioEncoderTestState>();
    auto audioState = std::make_shared<AudioCaptureTestState>();
    RecordingManager manager;
    prepare(manager);
    manager.m_createAudioEngine = [audioState] { return new FakeStartupAudio(audioState); };
    RecordingInitTask::Config config;
    config.region = QRect(0, 0, 16, 16);
    config.screenInfo.geometry = config.region;
    config.frameSize = QSize(16, 16);
    config.audioEnabled = true;
    config.outputPath = "unused-device-lost.mp4";
    auto task = QSharedPointer<RecordingInitTask>::create(config);
    task->m_createEncoder = [encoderState](const auto& options, QObject* parent) {
        return EncoderFactoryTestAccess::create(options, parent, encoderState);
    };
    QVERIFY(task->initializeEncoder());
    task->result().success = true;
    task->result().captureEngine = std::make_unique<FakeStartupCapture>();
    task->result().captureEngineStarted = true;
    manager.m_initTask = task;
    manager.onInitializationComplete(task, manager.m_initGeneration);
    QCOMPARE(manager.state(), RecordingManager::State::Recording);
    using AudioState = SnapTray::AudioSourceState;
    QCOMPARE(manager.audioStatus().microphone, AudioState::Active);
    auto* audio = static_cast<FakeStartupAudio*>(manager.m_audioEngine.get());
    QSignalSpy warnings(&manager, &RecordingManager::recordingWarning);
    emit audio->deviceLost(IAudioCaptureEngine::AudioSource::SystemAudio);
    audio->activeSourceChanged(IAudioCaptureEngine::AudioSource::Microphone);
    QCOMPARE(manager.audioStatus().microphone, AudioState::Active);
    // PR #173 review: no "microphone was disconnected" for a system-audio loss.
    for (const auto& args : warnings) {
        QVERIFY2(!args.first().toString().contains(QStringLiteral("microphone was disconnected")),
                 qPrintable(args.first().toString()));
    }
    QCOMPARE(manager.audioStatus().systemAudio, AudioState::DeviceMissing);
    manager.cancelRecording();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTRY_COMPARE(encoderState->destroyed.load(), 1);
}

void TestRecordingStartup::bufferedAnimationCountsTowardTheDiskGuard()
{
    // PR #171 review: with preview off, a GIF/WebP recording has no file
    // until it ends; its buffered size must still count.
    RecordingManager manager;
    startFakeRecordingForDisk(manager);
    constexpr qint64 kMiB = 1024 * 1024;
    const quint64 generation = manager.m_startGeneration;
    QSignalSpy interrupted(&manager, &RecordingManager::recordingWarning);
    // 400 MiB free, no file on disk, 500 MiB of animation in memory.
    manager.onDiskSample(generation, 400 * kMiB, 0, 1000, 500 * kMiB);
    QCOMPARE(manager.state(), RecordingManager::State::Encoding); // stopped safely, kept
    QVERIFY(interrupted.count() >= 1);
    manager.cancelRecording();
}

void TestRecordingStartup::encoderWrittenBytesFeedTheDiskGuard()
{
    // PR #171 review: the Linux encoder writes a temporary file until it
    // finishes, so the destination's size read 0 all recording long and the
    // guard reserved nothing for it. The encoder's own count is used.
    constexpr qint64 kMiB = 1024 * 1024;
    auto written = std::make_shared<std::atomic<qint64>>(10240 * kMiB);
    RecordingManager manager;
    manager.m_freeBytesForPath = [](const QString&) { return 150 * kMiB; };
    startFakeRecordingForDisk(manager, written);
    // 150 MiB free: enough for an empty file, not for finishing 10 GiB.
    manager.checkDiskSpace();
    // Deliver only the probe's result; a full event loop would also run the
    // stopped recording's save, which opens a dialog without a real file.
    QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
    QCoreApplication::sendPostedEvents(QCoreApplication::instance(), QEvent::MetaCall);
    QCOMPARE(manager.state(), RecordingManager::State::Encoding); // stopped safely, kept
    manager.cancelRecording();
}

void TestRecordingStartup::startFakeRecordingForDisk(RecordingManager& manager, IVideoEncoder::ByteCounter written)
{
    RecordingSettingsManager::instance().setAudioEnabled(false);
    RecordingSettingsManager::instance().setShowPreview(false);
    prepare(manager);
    auto encoderState = std::make_shared<AudioEncoderTestState>();
    encoderState->written = written;
    RecordingInitTask::Config config;
    config.region = QRect(0, 0, 16, 16);
    config.screenInfo.geometry = config.region;
    config.frameSize = QSize(16, 16);
    config.outputPath = "unused-buffered-disk-test.mp4";
    auto task = QSharedPointer<RecordingInitTask>::create(config);
    task->m_createEncoder = [encoderState](const auto& options, QObject* parent) {
        return EncoderFactoryTestAccess::create(options, parent, encoderState);
    };
    QVERIFY(task->initializeEncoder());
    task->result().success = true;
    task->result().captureEngine = std::make_unique<FakeStartupCapture>();
    task->result().captureEngineStarted = true;
    manager.m_initTask = task;
    manager.m_recordingFilePath = QStringLiteral("unused-buffered-disk-test.mp4");
    manager.onInitializationComplete(task, manager.m_initGeneration);
    QCOMPARE(manager.state(), RecordingManager::State::Recording);
}

void TestRecordingStartup::recoveredRecordingKeepsItsQuality()
{
    QTemporaryDir dir;
    const QString video = dir.filePath(QStringLiteral("SnapTray_Recording_q.mp4"));
    {
        QFile file(video);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("x");
    }
    SnapTray::RecordingSessionInfo info;
    info.videoPath = video;
    info.intermediate = false; // recorded at the selected quality
    QVERIFY(SnapTray::RecordingSession::write(info));
    RecordingManager manager;
    QSignalSpy preview(&manager, &RecordingManager::previewRequested);
    QVERIFY(manager.openRecoveredRecording(video));
    QCOMPARE(preview.first().at(2).toBool(), false);
    manager.onPreviewClosed(false);
}

void TestRecordingStartup::saveDialogSessionSurvivesANewRecording()
{
    // PR #172 review: with preview off, a recording started while the first
    // one's save dialog is open lost its manifest when that dialog returned.
    QTemporaryDir dir;
    const QString first = dir.filePath(QStringLiteral("SnapTray_Recording_first.mp4"));
    const QString second = dir.filePath(QStringLiteral("SnapTray_Recording_second.mp4"));
    {
        QFile file(first);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("x");
    }
    RecordingManager manager;
    manager.m_startSettings.showPreview = false;
    manager.beginSession(first, QSize(16, 16), false);
    manager.m_state = RecordingManager::State::Encoding;
    // Finished: the save dialog is queued (not run here) and owns this session now.
    manager.onEncodingFinished(true, first);
    QCOMPARE(manager.state(), RecordingManager::State::Idle);
    QVERIFY(manager.m_session.videoPath.isEmpty());
    QVERIFY(SnapTray::RecordingSession::hasSession(first));
    QVERIFY(RecordingManager::isSessionActive(first)); // still locked by its dialog
    // A second recording starts meanwhile: both sessions stay.
    manager.beginSession(second, QSize(16, 16), false);
    QVERIFY(SnapTray::RecordingSession::hasSession(first));
    QVERIFY(SnapTray::RecordingSession::hasSession(second));
    QCOMPARE(manager.m_session.videoPath, second);
    manager.endSession(true);
}

void TestRecordingStartup::init()
{
    auto& settings = RecordingSettingsManager::instance();
    settings.setOutputFormat(0);
    settings.setShowPreview(true);
    settings.setQuality(83);
    settings.setAudioEnabled(true);
    settings.setAudioSource(0);
    settings.setAudioDevice("saved-device");
    settings.setCountdownEnabled(false);
    settings.setCountdownSeconds(3);
}

void TestRecordingStartup::prepare(RecordingManager& manager)
{
    // These tests exercise the full audio/preview flow with injected engines.
    manager.m_requiresDirectMp4Recording = [] { return false; };
    manager.m_targetScreen = QGuiApplication::primaryScreen();
    QVERIFY(manager.m_targetScreen);
    manager.m_recordingRegion = manager.m_targetScreen->geometry();
    manager.initializeStartState();
}

void TestRecordingStartup::basicBackendIgnoresUnsupportedPreferences()
{
    auto& settings = RecordingSettingsManager::instance();
    settings.setOutputFormat(2);
    settings.setShowPreview(true);
    settings.setAudioEnabled(true);
    RecordingManager manager;
    manager.m_requiresDirectMp4Recording = [] { return true; };
    manager.initializeStartState();
    QCOMPARE(manager.m_startSettings.outputFormat, 0);
    QVERIFY(!manager.m_startSettings.showPreview);
    QVERIFY(!manager.m_audioEnabled);
    QVERIFY(manager.generateOutputPath().endsWith(".mp4"));
    QCOMPARE(settings.outputFormat(), 2);
    QVERIFY(settings.showPreview());
    QVERIFY(settings.audioEnabled());
}

void TestRecordingStartup::waitingKeepsEventLoopResponsiveAndSettingsSnapshot()
{
    RecordingManager manager;
    std::function<void(bool)> reply;
    int initialized = 0;
    manager.m_checkMicrophonePermission = [] { return IAudioCaptureEngine::MicrophonePermission::NotDetermined; };
    manager.m_requestMicrophonePermission = [&](auto callback) { reply = std::move(callback); };
    manager.m_initializeRecording = [&] { ++initialized; };
    manager.m_elapsedTimer.start(); // A previous recording's clock must be discarded.
    prepare(manager);
    manager.prepareAudioPermission();
    QVERIFY(reply);
    int heartbeat = 0;
    QTimer timer;
    connect(&timer, &QTimer::timeout, [&] { ++heartbeat; });
    timer.start(1);
    QTRY_VERIFY(heartbeat >= 3);
    QCOMPARE(manager.state(), RecordingManager::State::Preparing);
    QCOMPARE(initialized, 0);
    QVERIFY(!manager.m_elapsedTimer.isValid());
    QVERIFY(!manager.m_initTask);
    QVERIFY(!manager.m_countdownOverlay);
    auto& settings = RecordingSettingsManager::instance();
    settings.setAudioSource(1);
    settings.setAudioEnabled(false);
    settings.setQuality(10);
    settings.setShowPreview(false);
    reply(true);
    QTRY_COMPARE(initialized, 1);
    QCOMPARE(manager.m_audioSource, 0);
    QVERIFY(manager.m_audioEnabled);
    QCOMPARE(manager.m_startSettings.quality, 83);
    QVERIFY(manager.m_startSettings.showPreview);
    QCOMPARE(manager.m_audioDevice, QString("saved-device"));
}

void TestRecordingStartup::permissionPolicy_data()
{
    QTest::addColumn<int>("permission");
    QTest::addColumn<int>("source");
    QTest::addColumn<bool>("requested");
    QTest::addColumn<int>("format");
    QTest::addColumn<bool>("preview");
    for (int permission = 0; permission <= 3; ++permission)
        for (int source = 0; source <= 2; ++source)
            QTest::newRow(qPrintable(QString("permission-%1-source-%2").arg(permission).arg(source)))
                << permission << source << true << 0 << true;
    QTest::newRow("disabled") << 1 << 0 << false << 0 << true;
    QTest::newRow("gif-direct") << 1 << 0 << true << 1 << false;
    QTest::newRow("webp-direct") << 1 << 0 << true << 2 << false;
    QTest::newRow("gif-preview-mp4") << 1 << 0 << true << 1 << true;
}

void TestRecordingStartup::permissionPolicy()
{
    QFETCH(int, permission);
    QFETCH(int, source);
    QFETCH(bool, requested);
    QFETCH(int, format);
    QFETCH(bool, preview);
    auto& settings = RecordingSettingsManager::instance();
    settings.setAudioEnabled(requested);
    settings.setAudioSource(source);
    settings.setOutputFormat(format);
    settings.setShowPreview(preview);
    RecordingManager manager;
    int checks = 0, prompts = 0, initialized = 0;
    manager.m_checkMicrophonePermission = [&] {
        ++checks;
        return static_cast<IAudioCaptureEngine::MicrophonePermission>(permission);
    };
    manager.m_requestMicrophonePermission = [&](auto callback) { ++prompts; callback(false); };
    manager.m_initializeRecording = [&] { ++initialized; };
    QSignalSpy warnings(&manager, &RecordingManager::recordingWarning);
    prepare(manager);
    manager.prepareAudioPermission();
    QTRY_COMPARE(initialized, 1);
    const bool audioSupported = requested && (preview || format == 0);
    const bool microphone = audioSupported && source != 1;
    const bool denied = microphone && permission != int(IAudioCaptureEngine::MicrophonePermission::Authorized);
    QCOMPARE(checks, microphone ? 1 : 0);
    QCOMPARE(prompts, microphone && permission == int(IAudioCaptureEngine::MicrophonePermission::NotDetermined) ? 1 : 0);
    QCOMPARE(manager.m_audioEnabled, audioSupported && !(denied && source == 0));
    QCOMPARE(manager.m_audioSource, denied && source == 2 ? 1 : source);
    manager.flushStartupAudioWarnings();
    manager.flushStartupAudioWarnings();
    QCOMPARE(warnings.count(), denied ? 1 : 0);
    QCOMPARE(settings.audioSource(), source);
    QCOMPARE(settings.audioEnabled(), requested);
    using AudioState = SnapTray::AudioSourceState;
    const auto requestedState = !requested ? AudioState::Off
        : audioSupported ? AudioState::Starting : AudioState::UnsupportedFormat;
    const auto expectedMic = source == 1 ? AudioState::Off
        : denied ? AudioState::PermissionDenied : requestedState;
    const auto expectedSystem = source == 0 ? AudioState::Off : requestedState;
    QCOMPARE(manager.audioStatus().microphone, expectedMic);
    QCOMPARE(manager.audioStatus().systemAudio, expectedSystem);
    manager.cancelRecording();
    QCOMPARE(manager.audioStatus(), SnapTray::RecordingAudioStatus{});
}

void TestRecordingStartup::staleAndDuplicateRepliesCannotStart()
{
    RecordingManager manager;
    QList<std::function<void(bool)>> replies;
    int initialized = 0;
    manager.m_checkMicrophonePermission = [] { return IAudioCaptureEngine::MicrophonePermission::NotDetermined; };
    manager.m_requestMicrophonePermission = [&](auto callback) { replies.append(std::move(callback)); };
    manager.m_initializeRecording = [&] { ++initialized; };
    prepare(manager);
    manager.prepareAudioPermission();
    manager.stopRecording(); // Escape/stop while preparing cancels this request.
    QCOMPARE(manager.state(), RecordingManager::State::Idle);
    replies[0](true);
    QCoreApplication::processEvents();
    QCOMPARE(initialized, 0);
    prepare(manager);
    manager.prepareAudioPermission();
    replies[0](false);
    replies[1](true);
    replies[1](false);
    QTRY_COMPARE(initialized, 1);
    QVERIFY(manager.m_audioEnabled);
    QVERIFY(manager.m_startupAudioWarnings.isEmpty());
}

void TestRecordingStartup::missingScreenAndDestructionRejectReplies()
{
    std::function<void(bool)> reply;
    int initialized = 0;
    auto manager = std::make_unique<RecordingManager>();
    manager->m_checkMicrophonePermission = [] { return IAudioCaptureEngine::MicrophonePermission::NotDetermined; };
    manager->m_requestMicrophonePermission = [&](auto callback) { reply = std::move(callback); };
    manager->m_initializeRecording = [&] { ++initialized; };
    prepare(*manager);
    manager->prepareAudioPermission();
    manager->m_targetScreen.clear();
    QSignalSpy errors(manager.get(), &RecordingManager::recordingError);
    reply(true);
    QTRY_COMPARE(errors.count(), 1);
    QCOMPARE(initialized, 0);
    QCOMPARE(manager->state(), RecordingManager::State::Idle);
    prepare(*manager);
    manager->prepareAudioPermission();
    manager.reset();
    reply(true);
    QCoreApplication::processEvents();
    QCOMPARE(initialized, 0);
}

void TestRecordingStartup::permissionWaitIsOutsideTimelineAndOldTimerIsIgnored()
{
    RecordingManager manager;
    std::function<void(bool)> reply;
    manager.m_checkMicrophonePermission = [] { return IAudioCaptureEngine::MicrophonePermission::NotDetermined; };
    manager.m_requestMicrophonePermission = [&](auto callback) { reply = std::move(callback); };
    manager.m_initializeRecording = [&] { manager.startRecordingAfterCountdown(); };
    prepare(manager);
    manager.prepareAudioPermission();
    QTest::qWait(50);
    QVERIFY(!manager.m_elapsedTimer.isValid());
    reply(true);
    QTRY_COMPARE(manager.state(), RecordingManager::State::Recording);
    QVERIFY(manager.m_elapsedTimer.isValid());
    manager.cancelRecording();
    prepare(manager);
    manager.setState(RecordingManager::State::Recording);
    QTest::qWait(150); // The previous session's delayed capture startup must do nothing.
    QVERIFY(!manager.m_captureTimer);
    QVERIFY(!manager.m_durationTimer);
}

void TestRecordingStartup::captureExclusionWarning_data()
{
    QTest::addColumn<bool>("legacy");
    QTest::newRow("modern") << false;
    QTest::newRow("legacy") << true;
}

void TestRecordingStartup::captureExclusionWarning()
{
    QFETCH(bool, legacy);
    RecordingManager manager;
    manager.m_captureControlsMayBeVisible = [legacy] { return legacy; };
    QSignalSpy warnings(&manager, &RecordingManager::recordingWarning);
    prepare(manager);
    manager.warnAboutVisibleCaptureControls();
    manager.warnAboutVisibleCaptureControls();
    QCOMPARE(warnings.count(), legacy ? 1 : 0);
    QVERIFY(!manager.m_elapsedTimer.isValid());
    manager.cancelRecording();
    prepare(manager);
    manager.warnAboutVisibleCaptureControls();
    QCOMPARE(warnings.count(), legacy ? 2 : 0);
    manager.cancelRecording();
}

void TestRecordingStartup::controlBarOffersPauseResumeAndStop()
{
    SnapTray::QmlRecordingControlBar bar;
    bar.ensureView();
    QVERIFY(bar.m_rootItem);
    auto* pauseButton = bar.m_rootItem->findChild<QQuickItem*>(QStringLiteral("pauseResumeButton"));
    auto* stopButton = bar.m_rootItem->findChild<QQuickItem*>(QStringLiteral("stopButton"));
    QVERIFY(pauseButton);
    QVERIFY(stopButton);

    QSignalSpy pause(&bar, &SnapTray::QmlRecordingControlBar::pauseRequested);
    QSignalSpy resume(&bar, &SnapTray::QmlRecordingControlBar::resumeRequested);
    QSignalSpy stop(&bar, &SnapTray::QmlRecordingControlBar::stopRequested);
    bar.setPreparing(false);
    QVERIFY(QMetaObject::invokeMethod(pauseButton, "activated"));
    QCOMPARE(pause.count(), 1);
    bar.setPaused(true);
    QVERIFY(QMetaObject::invokeMethod(pauseButton, "activated"));
    QCOMPARE(resume.count(), 1);
    QVERIFY(QMetaObject::invokeMethod(stopButton, "activated"));
    QCOMPARE(stop.count(), 1);

    // Paused shows a label next to the frozen time.
    bar.updateDuration(65'000);
    auto* label = bar.m_rootItem->findChild<QQuickItem*>(QStringLiteral("durationLabel"));
    QVERIFY(label);
    QVERIFY(label->property("text").toString().contains(QStringLiteral("00:01:05")));
    QVERIFY(label->property("text").toString() != QStringLiteral("00:01:05"));
    bar.setPaused(false);
    QCOMPARE(label->property("text").toString(), QStringLiteral("00:01:05"));

    // Tooltips name the outcome of Stop and the configured hotkeys.
    QCOMPARE(bar.tooltipForButton(0), QStringLiteral("Stop Recording"));
    bar.setStopOpensPreview(true);
    QCOMPARE(bar.tooltipForButton(0), QStringLiteral("Stop and Review"));
    QCOMPARE(bar.tooltipForButton(1), QStringLiteral("Pause Recording"));
    bar.setShortcutHints(QStringLiteral("F9"), QStringLiteral("F10"));
    QCOMPARE(bar.tooltipForButton(1), QStringLiteral("Pause Recording (F9)"));
    QCOMPARE(bar.tooltipForButton(0), QStringLiteral("Stop and Review (F10)"));
    bar.setPaused(true);
    QCOMPARE(bar.tooltipForButton(1), QStringLiteral("Resume Recording (F9)"));
}

void TestRecordingStartup::sessionClockFollowsRecordingLifecycle()
{
    RecordingSettingsManager::instance().setAudioEnabled(false);
    RecordingManager manager;
    prepare(manager);
    // Preparation: frames from now on are before the recording starts.
    const qint64 beforeStartNs = SnapTray::captureClockNowNs();
    QVERIFY(!manager.m_sessionClock.isStarted());

    auto encoderState = std::make_shared<AudioEncoderTestState>();
    RecordingInitTask::Config config;
    config.region = QRect(0, 0, 16, 16);
    config.screenInfo.geometry = config.region;
    config.frameSize = QSize(16, 16);
    config.outputPath = "unused-session-clock-test.mp4";
    auto task = QSharedPointer<RecordingInitTask>::create(config);
    task->m_createEncoder = [encoderState](const auto& options, QObject* parent) {
        return EncoderFactoryTestAccess::create(options, parent, encoderState);
    };
    QVERIFY(task->initializeEncoder());
    task->result().success = true;
    task->result().captureEngine = std::make_unique<FakeStartupCapture>();
    task->result().captureEngineStarted = true;
    manager.m_initTask = task;
    manager.onInitializationComplete(task, manager.m_initGeneration);
    QCOMPARE(manager.state(), RecordingManager::State::Recording);
    QVERIFY(manager.m_sessionClock.isStarted());

    qint64 ms = -1;
    {
        QMutexLocker lock(&manager.m_durationMutex);
        // A frame captured before the start gate (preparation, countdown) is not recorded.
        QVERIFY(!manager.admitFrameLocked(beforeStartNs, true, &ms));
        // Frames carry their own capture time, whatever thread delivers them.
        const qint64 now = SnapTray::captureClockNowNs();
        QVERIFY(manager.admitFrameLocked(now, true, &ms));
        QVERIFY(ms >= 0 && ms < 5000);
        // A copy of the same frame, or one older than it, is rejected.
        QVERIFY(!manager.admitFrameLocked(now, true, &ms));
    }

    manager.pauseRecording();
    QVERIFY(manager.m_sessionClock.isPaused());
    manager.resumeRecording();
    QVERIFY(!manager.m_sessionClock.isPaused());

    manager.stopRecording();
    QVERIFY(!manager.m_sessionClock.isStarted());
    {
        QMutexLocker lock(&manager.m_durationMutex);
        QVERIFY(!manager.admitFrameLocked(SnapTray::captureClockNowNs(), true, &ms));
    }
    manager.cancelRecording();
}

void TestRecordingStartup::sustainedFrameLossWarnsOnce()
{
    RecordingSettingsManager::instance().setAudioEnabled(false);
    RecordingManager manager;
    prepare(manager);
    auto encoderState = std::make_shared<AudioEncoderTestState>();
    RecordingInitTask::Config config;
    config.region = QRect(0, 0, 16, 16);
    config.screenInfo.geometry = config.region;
    config.frameSize = QSize(16, 16);
    config.outputPath = "unused-health-test.mp4";
    auto task = QSharedPointer<RecordingInitTask>::create(config);
    task->m_createEncoder = [encoderState](const auto& options, QObject* parent) {
        return EncoderFactoryTestAccess::create(options, parent, encoderState);
    };
    QVERIFY(task->initializeEncoder());
    task->result().success = true;
    task->result().captureEngine = std::make_unique<FakeStartupCapture>();
    task->result().captureEngineStarted = true;
    manager.m_initTask = task;
    manager.onInitializationComplete(task, manager.m_initGeneration);
    QCOMPARE(manager.state(), RecordingManager::State::Recording);
    QVERIFY(manager.m_health);

    QSignalSpy warnings(&manager, &RecordingManager::recordingWarning);
    QSignalSpy dropping(&manager, &RecordingManager::droppingFramesChanged);
    // Three seconds in which half of the offered frames are lost.
    for (int second = 1; second <= 3; ++second) {
        for (int i = 0; i < 30; ++i) manager.m_health->frameOffered();
        for (int i = 0; i < 15; ++i) manager.m_health->frameDropped(SnapTray::RecordingHealth::FrameDrop::QueueFull);
        manager.checkHealth(second * 1000);
    }
    QCOMPARE(warnings.count(), 1);
    QVERIFY(warnings.first().first().toString().contains(QStringLiteral("dropping frames")));
    QCOMPARE(dropping.count(), 1);
    QVERIFY(manager.isDroppingFrames());
    // More bad seconds do not repeat the message.
    for (int i = 0; i < 30; ++i) manager.m_health->frameOffered();
    for (int i = 0; i < 15; ++i) manager.m_health->frameDropped(SnapTray::RecordingHealth::FrameDrop::QueueFull);
    manager.checkHealth(4000);
    QCOMPARE(warnings.count(), 1);

    // Stopping clears the state for the tray.
    manager.stopRecording();
    QVERIFY(!manager.isDroppingFrames());
    QCOMPARE(dropping.count(), 2);
    manager.cancelRecording();
}

void TestRecordingStartup::startRefusedWhenDiskIsFull()
{
    RecordingSettingsManager::instance().setAudioEnabled(false);
    RecordingManager manager;
    manager.m_freeBytesForPath = [](const QString&) { return qint64(10 * 1024 * 1024); };
    prepare(manager);
    QSignalSpy errors(&manager, &RecordingManager::recordingError);
    manager.beginAsyncInitialization();
    QCOMPARE(manager.state(), RecordingManager::State::Idle);
    QCOMPARE(errors.count(), 1);
    QVERIFY(errors.first().first().toString().contains(QStringLiteral("Not enough free disk space")));
    QVERIFY(!manager.m_initTask);

    // Unknown free space does not block a start.
    RecordingManager unknown;
    unknown.m_freeBytesForPath = [](const QString&) { return qint64(-1); };
    unknown.m_initializeRecording = [] {};
    prepare(unknown);
    QSignalSpy unknownErrors(&unknown, &RecordingManager::recordingError);
    unknown.beginAsyncInitialization();
    QCOMPARE(unknownErrors.count(), 0);
    unknown.cancelRecording();
}

void TestRecordingStartup::diskSamplesWarnAndStopSafely()
{
    RecordingSettingsManager::instance().setAudioEnabled(false);
    RecordingSettingsManager::instance().setShowPreview(false);
    RecordingManager manager;
    prepare(manager);
    auto encoderState = std::make_shared<AudioEncoderTestState>();
    RecordingInitTask::Config config;
    config.region = QRect(0, 0, 16, 16);
    config.screenInfo.geometry = config.region;
    config.frameSize = QSize(16, 16);
    config.outputPath = "unused-disk-test.mp4";
    auto task = QSharedPointer<RecordingInitTask>::create(config);
    task->m_createEncoder = [encoderState](const auto& options, QObject* parent) {
        return EncoderFactoryTestAccess::create(options, parent, encoderState);
    };
    QVERIFY(task->initializeEncoder());
    task->result().success = true;
    task->result().captureEngine = std::make_unique<FakeStartupCapture>();
    task->result().captureEngineStarted = true;
    manager.m_initTask = task;
    manager.m_recordingFilePath = QStringLiteral("unused-disk-test.mp4");
    manager.onInitializationComplete(task, manager.m_initGeneration);
    QCOMPARE(manager.state(), RecordingManager::State::Recording);
    QVERIFY(manager.m_diskTimer);  // monitoring runs while recording

    constexpr qint64 kMiB = 1024 * 1024;
    const quint64 generation = manager.m_startGeneration;
    QSignalSpy warnings(&manager, &RecordingManager::recordingWarning);
    QSignalSpy low(&manager, &RecordingManager::lowDiskSpaceChanged);
    // Plenty of room, a growing file at 1 MiB/s.
    manager.onDiskSample(generation, 100000 * kMiB, 0, 0);
    manager.onDiskSample(generation, 100000 * kMiB, 1 * kMiB, 1000);
    QCOMPARE(warnings.count(), 0);
    // Into the warning band: one warning with an estimated time left.
    manager.onDiskSample(generation, 150 * kMiB, 2 * kMiB, 2000);
    QCOMPARE(warnings.count(), 1);
    QVERIFY(warnings.last().first().toString().contains(QStringLiteral("estimate")));
    QVERIFY(manager.isLowOnDiskSpace());
    manager.onDiskSample(generation, 140 * kMiB, 3 * kMiB, 3000);
    QCOMPARE(warnings.count(), 1);  // no repeat
    // A sample from an older session is ignored.
    manager.onDiskSample(generation - 1, 0, 0, 3500);
    QCOMPARE(manager.state(), RecordingManager::State::Recording);
    // Space freed: the warning clears.
    manager.onDiskSample(generation, 100000 * kMiB, 4 * kMiB, 4000);
    QVERIFY(!manager.isLowOnDiskSpace());
    // The probe fails: reported once, the recording continues.
    manager.onDiskSample(generation, -1, 5 * kMiB, 5000);
    manager.onDiskSample(generation, -1, 6 * kMiB, 6000);
    QCOMPARE(warnings.count(), 2);
    QCOMPARE(manager.state(), RecordingManager::State::Recording);
    // Nearly full: the recording is stopped through the normal stop path
    // (finalize, then save), never discarded.
    QSignalSpy cancelled(&manager, &RecordingManager::recordingCancelled);
    manager.onDiskSample(generation, 70 * kMiB, 7 * kMiB, 7000);
    QCOMPARE(manager.state(), RecordingManager::State::Encoding);
    QCOMPARE(warnings.count(), 3);
    QVERIFY(warnings.last().first().toString().contains(QStringLiteral("disk is almost full")));
    QCOMPARE(cancelled.count(), 0);
    QVERIFY(!manager.m_diskTimer);
    manager.cancelRecording();
}

void TestRecordingStartup::sessionManifestFollowsTheRecording()
{
    QTemporaryDir dir;
    const QString video = dir.filePath(QStringLiteral("SnapTray_Recording_s.mp4"));
    RecordingManager manager;
    manager.beginSession(video, QSize(1920, 1080), true);
    QVERIFY(SnapTray::RecordingSession::hasSession(video));
    // Live: a scan (this or another SnapTray) must leave it alone.
    QVERIFY(RecordingManager::isSessionActive(video));
    auto info = SnapTray::RecordingSession::read(SnapTray::RecordingSession::manifestPathFor(video));
    QVERIFY(info);
    QCOMPARE(info->state, SnapTray::RecordingSessionInfo::State::Recording);
    QVERIFY(info->audioRequired);
    manager.updateSession(SnapTray::RecordingSessionInfo::State::Finished);
    QCOMPARE(SnapTray::RecordingSession::read(SnapTray::RecordingSession::manifestPathFor(video))->state,
             SnapTray::RecordingSessionInfo::State::Finished);
    // Ending without removing (encoder failure, quit with the file kept):
    // the manifest stays for recovery, the lock goes.
    manager.endSession(false);
    QVERIFY(SnapTray::RecordingSession::hasSession(video));
    QVERIFY(!RecordingManager::isSessionActive(video));
    // Saved or discarded: both go.
    manager.beginSession(video, QSize(1920, 1080), false);
    manager.endSession(true);
    QVERIFY(!SnapTray::RecordingSession::hasSession(video));
}

void TestRecordingStartup::staleCleanupSparesPendingRecoveries()
{
    const QDir temp(QStandardPaths::writableLocation(QStandardPaths::TempLocation));
    const QString tag = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString pending = temp.filePath(QStringLiteral("SnapTray_Recording_t157_%1_a.mp4").arg(tag));
    const QString stale = temp.filePath(QStringLiteral("SnapTray_Recording_t157_%1_b.mp4").arg(tag));
    auto cleanup = qScopeGuard([&] {
        SnapTray::RecordingSession::discard(pending);
        SnapTray::RecordingSession::discard(stale);
    });
    for (const QString& path : {pending, stale}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("x");
        file.close();
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(QDateTime::currentDateTime().addDays(-3), QFileDevice::FileModificationTime));
    }
    SnapTray::RecordingSessionInfo info;
    info.videoPath = pending;
    QVERIFY(SnapTray::RecordingSession::write(info));

    RecordingManager manager;  // its constructor runs the old 24-hour cleanup
    QVERIFY2(QFileInfo::exists(pending), "a recording waiting for recovery must not be cleaned up");
    QVERIFY(!QFileInfo::exists(stale));
}

void TestRecordingStartup::recoveredRecordingOpensInPreview()
{
    QTemporaryDir dir;
    const QString video = dir.filePath(QStringLiteral("SnapTray_Recording_r.mp4"));
    {
        QFile file(video);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("x");
    }
    SnapTray::RecordingSessionInfo info;
    info.videoPath = video;
    QVERIFY(SnapTray::RecordingSession::write(info));

    RecordingManager manager;
    QSignalSpy preview(&manager, &RecordingManager::previewRequested);
    QVERIFY(manager.openRecoveredRecording(video));
    QCOMPARE(manager.state(), RecordingManager::State::Previewing);
    QCOMPARE(preview.count(), 1);
    QCOMPARE(preview.first().first().toString(), video);
    // PR #172 review: a recovered intermediate keeps its quality check on Save.
    QCOMPARE(preview.first().at(2).toBool(), true);
    QVERIFY(RecordingManager::isSessionActive(video));
    // Busy: a second one cannot open, and neither can another instance.
    QVERIFY(!manager.openRecoveredRecording(video));
    RecordingManager other;
    QVERIFY(!other.openRecoveredRecording(video));

    // Closing the preview while the file is still there (SnapTray quitting)
    // keeps it for the next launch.
    manager.onPreviewClosed(false);
    QVERIFY(SnapTray::RecordingSession::hasSession(video));
    QVERIFY(!RecordingManager::isSessionActive(video));
}

// PR #203 review: a save (dialog or auto-save) into the Recording Library's
// folder could replace a project's source recording; it is refused.
void TestRecordingStartup::savesNeverGoIntoTheProjectsFolder()
{
    QTemporaryDir dir;
    const QString root = dir.filePath(QStringLiteral("projects"));
    SnapTray::RecordingProjects::setRootForTests(root);
    auto reset = qScopeGuard([] { SnapTray::RecordingProjects::setRootForTests(QString()); });
    QVERIFY(!RecordingManager::isAllowedSaveDestination(root + QStringLiteral("/20261103-1/recording.mp4")));
    QVERIFY(!RecordingManager::isAllowedSaveDestination(root + QStringLiteral("/other.mp4")));
    QVERIFY(!RecordingManager::isAllowedSaveDestination(QString()));
    QVERIFY(RecordingManager::isAllowedSaveDestination(dir.filePath(QStringLiteral("saved.mp4"))));
    QVERIFY(RecordingManager::isAllowedSaveDestination(root + QStringLiteral("-elsewhere/saved.mp4")));
}

void TestRecordingStartup::startFakeRecording(RecordingManager& manager,
                                              std::shared_ptr<AudioEncoderTestState> encoderState)
{
    RecordingSettingsManager::instance().setAudioEnabled(false);
    prepare(manager);
    RecordingInitTask::Config config;
    config.region = QRect(0, 0, 16, 16);
    config.screenInfo.geometry = config.region;
    config.frameSize = QSize(16, 16);
    config.outputPath = "unused-interruption-test.mp4";
    auto task = QSharedPointer<RecordingInitTask>::create(config);
    task->m_createEncoder = [encoderState](const auto& options, QObject* parent) {
        return EncoderFactoryTestAccess::create(options, parent, encoderState);
    };
    QVERIFY(task->initializeEncoder());
    task->result().success = true;
    task->result().captureEngine = std::make_unique<FakeStartupCapture>();
    task->result().captureEngineStarted = true;
    manager.m_initTask = task;
    manager.onInitializationComplete(task, manager.m_initGeneration);
    QCOMPARE(manager.state(), RecordingManager::State::Recording);
}

void TestRecordingStartup::interruptionsKeepTheRecording_data()
{
    QTest::addColumn<int>("cause");
    QTest::newRow("screen removed (async backends)") << 0;
    QTest::newRow("screen gone (polling backends)") << 1;
    QTest::newRow("screen geometry changed") << 2;
    QTest::newRow("screen scale changed") << 3;
    QTest::newRow("disk almost full") << 4;
}

void TestRecordingStartup::interruptionsKeepTheRecording()
{
    QFETCH(int, cause);
    RecordingManager manager;
    startFakeRecording(manager, std::make_shared<AudioEncoderTestState>());
    QSignalSpy interrupted(&manager, &RecordingManager::recordingInterrupted);
    QSignalSpy warnings(&manager, &RecordingManager::recordingWarning);
    QSignalSpy cancelled(&manager, &RecordingManager::recordingCancelled);
    QSignalSpy errors(&manager, &RecordingManager::recordingError);
    QScreen* screen = manager.m_targetScreen.data();
    QVERIFY(screen);
    SnapTray::RecordingStopReason expected = SnapTray::RecordingStopReason::ScreenDisconnected;
    switch (cause) {
    case 0: emit qGuiApp->screenRemoved(screen); break;
    case 1:
        manager.m_targetScreen = nullptr;  // what isScreenAvailable() sees after an unplug
        manager.captureFrame();
        break;
    case 2:
        emit screen->geometryChanged(screen->geometry().adjusted(0, 0, 10, 10));
        expected = SnapTray::RecordingStopReason::ScreenChanged;
        break;
    case 3:
        emit screen->physicalDotsPerInchChanged(screen->physicalDotsPerInch() * 2);
        expected = SnapTray::RecordingStopReason::ScreenChanged;
        break;
    case 4: {
        const quint64 generation = manager.m_startGeneration;
        manager.onDiskSample(generation, 1, 0, 0);
        expected = SnapTray::RecordingStopReason::DiskFull;
        break;
    }
    }
    // Finished normally (Encoding, then preview or save) - never cancelled.
    QCOMPARE(manager.state(), RecordingManager::State::Encoding);
    QCOMPARE(cancelled.count(), 0);
    QCOMPARE(errors.count(), 0);
    QCOMPARE(interrupted.count(), 1);
    QCOMPARE(interrupted.first().first().value<SnapTray::RecordingStopReason>(), expected);
    QCOMPARE(warnings.count(), 1);
    QVERIFY(warnings.first().first().toString().contains(QStringLiteral("Everything recorded so far is kept")));
    // The screen watch is gone with the recording.
    QVERIFY(manager.m_screenWatch.isEmpty());
    manager.cancelRecording();
}

void TestRecordingStartup::concurrentStopsEndOnce()
{
    RecordingManager manager;
    startFakeRecording(manager, std::make_shared<AudioEncoderTestState>());
    QSignalSpy interrupted(&manager, &RecordingManager::recordingInterrupted);
    QSignalSpy warnings(&manager, &RecordingManager::recordingWarning);
    // Pause, then several causes at once: the first one wins, once.
    manager.pauseRecording();
    QCOMPARE(manager.state(), RecordingManager::State::Paused);
    manager.m_captureEngine->error(QStringLiteral("stream lost"));
    emit qGuiApp->screenRemoved(manager.m_targetScreen.data());
    manager.stopRecording();
    manager.interruptRecording(SnapTray::RecordingStopReason::DiskFull);
    QCOMPARE(manager.state(), RecordingManager::State::Encoding);
    QCOMPARE(interrupted.count(), 1);
    QCOMPARE(interrupted.first().first().value<SnapTray::RecordingStopReason>(),
             SnapTray::RecordingStopReason::CaptureFailed);
    QCOMPARE(warnings.count(), 1);
    manager.cancelRecording();
}

void TestRecordingStartup::interruptionBeforeRecordingCancels()
{
    RecordingManager manager;
    prepare(manager);
    QCOMPARE(manager.state(), RecordingManager::State::Preparing);
    QSignalSpy errors(&manager, &RecordingManager::recordingError);
    QSignalSpy interrupted(&manager, &RecordingManager::recordingInterrupted);
    manager.interruptRecording(SnapTray::RecordingStopReason::ScreenDisconnected);
    // Nothing was recorded yet: cancel, say why, no "kept" message.
    QCOMPARE(manager.state(), RecordingManager::State::Idle);
    QCOMPARE(errors.count(), 1);
    QCOMPARE(errors.first().first().toString(),
             QStringLiteral("Recording could not start. The recorded screen was disconnected."));
    QCOMPARE(interrupted.count(), 0);
    manager.interruptRecording(SnapTray::RecordingStopReason::ScreenDisconnected);
    QCOMPARE(errors.count(), 1);
}

void TestRecordingStartup::userStopIsNotAnInterruption()
{
    RecordingManager manager;
    startFakeRecording(manager, std::make_shared<AudioEncoderTestState>());
    QSignalSpy interrupted(&manager, &RecordingManager::recordingInterrupted);
    QSignalSpy warnings(&manager, &RecordingManager::recordingWarning);
    manager.stopRecording();
    QCOMPARE(manager.state(), RecordingManager::State::Encoding);
    QCOMPARE(*manager.m_stopReason, SnapTray::RecordingStopReason::User);
    // A source error after the user stopped is not reported as the cause.
    manager.interruptRecording(SnapTray::RecordingStopReason::CaptureFailed);
    QCOMPARE(interrupted.count(), 0);
    QCOMPARE(warnings.count(), 0);
    manager.cancelRecording();
}

void TestRecordingStartup::failedFinishSaysWhatWasKept_data()
{
    QTest::addColumn<bool>("fragmented");
    QTest::addColumn<bool>("playable");
    QTest::addColumn<bool>("partKept");
    QTest::newRow("crash-safe, playable") << true << true << true;
    // PR #173 review: a non-empty but unplayable file (headers only) is not
    // a kept part: recovery would refuse it.
    QTest::newRow("crash-safe, unplayable") << true << false << false;
    QTest::newRow("unfinished regular file") << false << true << false;
}

void TestRecordingStartup::failedFinishSaysWhatWasKept()
{
    QFETCH(bool, fragmented);
    QFETCH(bool, playable);
    QFETCH(bool, partKept);
    QTemporaryDir dir;
    const QString output = dir.filePath(QStringLiteral("SnapTray_Recording_f.mp4"));
    {
        QFile file(output);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("partial");
    }
    RecordingManager manager;
    manager.m_isRecordingPlayable = [playable](const QString&) { return playable; };
    manager.m_session.fragmented = fragmented;
    manager.m_session.videoPath = output;
    manager.m_state = RecordingManager::State::Encoding;
    QSignalSpy errors(&manager, &RecordingManager::recordingError);
    manager.onEncodingFinished(false, output);
    QCOMPARE(manager.state(), RecordingManager::State::Idle);
    QTRY_COMPARE(errors.count(), 1);
    const QString message = errors.first().first().toString();
    if (partKept) {
        QVERIFY2(message.contains(QStringLiteral("recover it from the tray menu")), qPrintable(message));
    } else {
        QVERIFY2(message.contains(QStringLiteral("Nothing usable could be kept")), qPrintable(message));
    }
}

void TestRecordingStartup::microphoneLossIsReported()
{
    RecordingSettingsManager::instance().setAudioEnabled(true);
    auto encoderState = std::make_shared<AudioEncoderTestState>();
    auto audioState = std::make_shared<AudioCaptureTestState>();
    RecordingManager manager;
    prepare(manager);
    manager.m_createAudioEngine = [audioState] { return new FakeStartupAudio(audioState); };
    RecordingInitTask::Config config;
    config.region = QRect(0, 0, 16, 16);
    config.screenInfo.geometry = config.region;
    config.frameSize = QSize(16, 16);
    config.audioEnabled = true;
    config.outputPath = "unused-mic-loss.mp4";
    auto task = QSharedPointer<RecordingInitTask>::create(config);
    task->m_createEncoder = [encoderState](const auto& options, QObject* parent) {
        return EncoderFactoryTestAccess::create(options, parent, encoderState);
    };
    QVERIFY(task->initializeEncoder());
    task->result().success = true;
    task->result().captureEngine = std::make_unique<FakeStartupCapture>();
    task->result().captureEngineStarted = true;
    manager.m_initTask = task;
    manager.onInitializationComplete(task, manager.m_initGeneration);
    QCOMPARE(manager.state(), RecordingManager::State::Recording);
    QCOMPARE(manager.audioStatus().microphone, SnapTray::AudioSourceState::Active);
    QSignalSpy warnings(&manager, &RecordingManager::recordingWarning);
    QSignalSpy interrupted(&manager, &RecordingManager::recordingInterrupted);

    emit static_cast<FakeStartupAudio*>(manager.m_audioEngine.get())->deviceLost(IAudioCaptureEngine::AudioSource::Microphone);
    // Recording goes on without the microphone, and the user is told so.
    QCOMPARE(manager.state(), RecordingManager::State::Recording);
    QCOMPARE(interrupted.count(), 0);
    QCOMPARE(warnings.count(), 1);
    QVERIFY(warnings.first().first().toString().contains(QStringLiteral("microphone was disconnected")));
    QCOMPARE(manager.audioStatus().microphone, SnapTray::AudioSourceState::DeviceMissing);
    manager.cancelRecording();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTRY_COMPARE(encoderState->destroyed.load(), 1);
}

void TestRecordingStartup::recoveredAnimationIsSavedDirectly()
{
    // PR #172 review: a recovered GIF/WebP went to the preview, which plays
    // video only. A finished animation is the output already: Recover saves
    // it (here through auto-save) and its manifest goes once it is saved.
    QTemporaryDir dir;
    QTemporaryDir out;
    const QString gif = dir.filePath(QStringLiteral("SnapTray_Recording_a.gif"));
    {
        QFile file(gif);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("GIF89a");
    }
    SnapTray::RecordingSessionInfo info;
    info.videoPath = gif;
    info.format = QStringLiteral("gif");
    info.state = SnapTray::RecordingSessionInfo::State::Finished;
    QVERIFY(SnapTray::RecordingSession::write(info));
    auto& settings = FileSettingsManager::instance();
    settings.saveAutoSaveRecordings(true);
    settings.saveRecordingPath(out.path());
    settings.saveFilenameTemplate("recovered.{ext}");
    auto restore = qScopeGuard([&settings] { settings.saveAutoSaveRecordings(false); });

    RecordingManager manager;
    QSignalSpy preview(&manager, &RecordingManager::previewRequested);
    QSignalSpy saved(&manager, &RecordingManager::recordingStopped);
    QVERIFY(manager.openRecoveredRecording(gif));
    QTRY_COMPARE(saved.count(), 1);
    QCOMPARE(preview.count(), 0);
    QCOMPARE(manager.state(), RecordingManager::State::Idle);
    QCOMPARE(saved.first().first().toString(), QDir(out.path()).filePath(QStringLiteral("recovered.gif")));
    QVERIFY(!QFileInfo::exists(gif));
    QVERIFY(!SnapTray::RecordingSession::hasSession(gif));
    QVERIFY(!RecordingManager::isSessionActive(gif));
}

QTEST_MAIN(TestRecordingStartup)
#include "tst_Startup.moc"
