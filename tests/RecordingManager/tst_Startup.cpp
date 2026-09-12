#include <QtTest>
#include <QScreen>
#include <QGuiApplication>
#include <QTimer>
#include "RecordingManager.h"
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
    void waitingKeepsEventLoopResponsiveAndSettingsSnapshot();
    void permissionPolicy_data();
    void permissionPolicy();
    void staleAndDuplicateRepliesCannotStart();
    void missingScreenAndDestructionRejectReplies();
    void permissionWaitIsOutsideTimelineAndOldTimerIsIgnored();
    void encoderCapabilityControlsAudioPipeline_data();
    void encoderCapabilityControlsAudioPipeline();
    void captureExclusionWarning_data();
    void captureExclusionWarning();
private:
    void prepare(RecordingManager& manager);
};

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
        static_cast<FakeStartupAudio*>(manager.m_audioEngine.get())->sendPcm();
        QTRY_COMPARE(encoderState->audioWrites.load(), 1);
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
    manager.m_targetScreen = QGuiApplication::primaryScreen();
    QVERIFY(manager.m_targetScreen);
    manager.m_recordingRegion = manager.m_targetScreen->geometry();
    manager.initializeStartState();
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

QTEST_MAIN(TestRecordingStartup)
#include "tst_Startup.moc"
