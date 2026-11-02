#include <QtTest>

#include <QGuiApplication>
#include <QScreen>
#include <QQuickItem>
#include <QQuickView>
#include <QThread>

#include "capture/AudioLevelMonitor.h"
#include "qml/QmlDialog.h"
#include "qml/RecordingRecoveryViewModel.h"
#include <QTemporaryDir>
#include "qml/ScreenPickerViewModel.h"
#include "recording/RecordingAudioStatus.h"
#include "settings/RecordingSettingsManager.h"

#include <atomic>
#include <memory>

using SnapTray::AudioLevelMonitor;
using SnapTray::AudioSourceState;
using Permission = IAudioCaptureEngine::MicrophonePermission;

namespace {

struct FakeEngineState {
    int created = 0;
    int destroyed = 0;
    int started = 0;
    bool startSucceeds = true;
    bool systemAudioSupported = true;
    QList<IAudioCaptureEngine::AudioDevice> devices{{"usb-mic", "USB Microphone", false},
                                                    {"built-in", "Built-in Microphone", true}};
};

class FakeLevelEngine final : public IAudioCaptureEngine
{
public:
    explicit FakeLevelEngine(std::shared_ptr<FakeEngineState> state) : m_state(std::move(state))
    {
        ++m_state->created;
    }
    ~FakeLevelEngine() override { ++m_state->destroyed; }
    bool setAudioSource(AudioSource source) override { m_source = source; return true; }
    AudioSource audioSource() const override { return m_source; }
    bool setDevice(const QString& device) override { m_deviceId = device; return true; }
    AudioFormat audioFormat() const override { return {}; }
    QList<AudioDevice> availableInputDevices() const override { return m_state->devices; }
    QString defaultInputDevice() const override { return QStringLiteral("built-in"); }
    bool start() override
    {
        ++m_state->started;
        m_running = m_state->startSucceeds;
        return m_running;
    }
    void stop() override { m_running = false; }
    void pause() override {}
    void resume() override {}
    bool isRunning() const override { return m_running; }
    bool isPaused() const override { return false; }
    QString engineName() const override { return QStringLiteral("fake"); }
    bool isAvailable() const override { return true; }
    bool isSystemAudioSupported() const override { return m_state->systemAudioSupported; }
    QString deviceId() const { return m_deviceId; }

private:
    std::shared_ptr<FakeEngineState> m_state;
    bool m_running = false;
};

QByteArray pcmWithPeak(qint16 peak)
{
    QByteArray data(8, '\0');
    auto* samples = reinterpret_cast<qint16*>(data.data());
    samples[0] = 100;
    samples[1] = static_cast<qint16>(-peak);
    samples[2] = 50;
    samples[3] = 0;
    return data;
}

} // namespace

class TestRecordingSetup : public QObject
{
    Q_OBJECT

private slots:
    void init();

    // Status model
    void requestedStatusFollowsSourceAndFormat_data();
    void requestedStatusFollowsSourceAndFormat();
    void describeNamesEachSource();

    // Level monitor
    void peakOf16BitPcm();
    void permissionDeniedDoesNotOpenDevice();
    void permissionPromptIsAsynchronous();
    void missingDeviceIsReported();
    void startFailureIsUnavailable();
    void levelIsThrottledAndReleasedOnStop();

    // Picker setup
    void setupTogglesSaveSettings();
    void chooseAndCancelReleaseMicrophone();
    void missingSavedDeviceStaysVisible();
    void outputNotes();
    void dialogShowsSetup();
    void recoveryDialogLists();

private:
    std::shared_ptr<FakeEngineState> m_engineState;
    AudioLevelMonitor::EngineFactory factory()
    {
        auto state = m_engineState;
        return [state] { return new FakeLevelEngine(state); };
    }
};

void TestRecordingSetup::init()
{
    m_engineState = std::make_shared<FakeEngineState>();
    auto& settings = RecordingSettingsManager::instance();
    settings.setAudioEnabled(false);
    settings.setAudioSource(SnapTray::kAudioSourceMicrophone);
    settings.setAudioDevice(QString());
    settings.setOutputFormat(0);
    settings.setShowPreview(true);
    settings.setShowSetupBeforeRecording(false);
}

void TestRecordingSetup::requestedStatusFollowsSourceAndFormat_data()
{
    QTest::addColumn<bool>("enabled");
    QTest::addColumn<int>("source");
    QTest::addColumn<int>("format");
    QTest::addColumn<bool>("preview");
    QTest::addColumn<int>("mic");
    QTest::addColumn<int>("system");
    const int off = int(AudioSourceState::Off);
    const int starting = int(AudioSourceState::Starting);
    const int unsupported = int(AudioSourceState::UnsupportedFormat);
    QTest::newRow("disabled") << false << 2 << 0 << true << off << off;
    QTest::newRow("mic-mp4") << true << 0 << 0 << false << starting << off;
    QTest::newRow("system-mp4") << true << 1 << 0 << false << off << starting;
    QTest::newRow("both-gif-preview") << true << 2 << 1 << true << starting << starting;
    QTest::newRow("both-gif-direct") << true << 2 << 1 << false << unsupported << unsupported;
    QTest::newRow("mic-webp-direct") << true << 0 << 2 << false << unsupported << off;
}

void TestRecordingSetup::requestedStatusFollowsSourceAndFormat()
{
    QFETCH(bool, enabled);
    QFETCH(int, source);
    QFETCH(int, format);
    QFETCH(bool, preview);
    QFETCH(int, mic);
    QFETCH(int, system);
    const auto status = SnapTray::requestedAudioStatus(enabled, source, format, preview);
    QCOMPARE(int(status.microphone), mic);
    QCOMPARE(int(status.systemAudio), system);
}

void TestRecordingSetup::describeNamesEachSource()
{
    QVERIFY(SnapTray::describeRecordingAudioStatus({}).isEmpty());
    SnapTray::RecordingAudioStatus status;
    status.microphone = AudioSourceState::PermissionDenied;
    status.systemAudio = AudioSourceState::Active;
    const QString text = SnapTray::describeRecordingAudioStatus(status);
    QVERIFY2(text.contains(QStringLiteral("Microphone: Permission denied")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("System audio: Recording")), qPrintable(text));
    // Every state has its own label; none falls back to empty.
    QSet<QString> labels;
    for (auto state : {AudioSourceState::Off, AudioSourceState::Starting, AudioSourceState::Active,
                       AudioSourceState::PermissionDenied, AudioSourceState::DeviceMissing,
                       AudioSourceState::Unavailable, AudioSourceState::UnsupportedFormat}) {
        const QString label = SnapTray::audioSourceStateText(state);
        QVERIFY(!label.isEmpty());
        labels.insert(label);
    }
    QCOMPARE(labels.size(), 7);
}

void TestRecordingSetup::peakOf16BitPcm()
{
    QCOMPARE(AudioLevelMonitor::peakOf16BitPcm(QByteArray()), 0.0f);
    QCOMPARE(AudioLevelMonitor::peakOf16BitPcm(QByteArray(6, '\0')), 0.0f);
    QCOMPARE(AudioLevelMonitor::peakOf16BitPcm(pcmWithPeak(16384)), 0.5f);
    QCOMPARE(AudioLevelMonitor::peakOf16BitPcm(pcmWithPeak(32767)), 32767.0f / 32768.0f);
    QByteArray extreme(2, '\0');
    reinterpret_cast<qint16*>(extreme.data())[0] = -32768;
    QCOMPARE(AudioLevelMonitor::peakOf16BitPcm(extreme), 1.0f);
}

void TestRecordingSetup::permissionDeniedDoesNotOpenDevice()
{
    AudioLevelMonitor monitor;
    monitor.setEngineFactory(factory());
    int requests = 0;
    monitor.setPermissionFunctions([] { return Permission::Denied; },
                                   [&](auto) { ++requests; });
    monitor.start(QString());
    QCOMPARE(monitor.state(), AudioLevelMonitor::State::PermissionDenied);
    QCOMPARE(m_engineState->created, 0);
    QCOMPARE(requests, 0);
}

void TestRecordingSetup::permissionPromptIsAsynchronous()
{
    AudioLevelMonitor monitor;
    monitor.setEngineFactory(factory());
    std::function<void(bool)> reply;
    monitor.setPermissionFunctions([] { return Permission::NotDetermined; },
                                   [&](auto callback) { reply = std::move(callback); });
    monitor.start(QString());
    QCOMPARE(monitor.state(), AudioLevelMonitor::State::WaitingForPermission);
    QVERIFY(reply);
    QCOMPARE(m_engineState->created, 0);
    // The answer may arrive on another thread.
    QThread* thread = QThread::create([&] { reply(true); });
    thread->start();
    thread->wait();
    delete thread;
    QTRY_COMPARE(monitor.state(), AudioLevelMonitor::State::Listening);
    QCOMPARE(m_engineState->started, 1);

    // A late answer for an earlier start is ignored.
    monitor.stop();
    reply(true);
    QTest::qWait(10);
    QCOMPARE(monitor.state(), AudioLevelMonitor::State::Idle);
    QCOMPARE(m_engineState->started, 1);
}

void TestRecordingSetup::missingDeviceIsReported()
{
    AudioLevelMonitor monitor;
    monitor.setEngineFactory(factory());
    monitor.setPermissionFunctions([] { return Permission::Authorized; }, {});
    monitor.start(QStringLiteral("unplugged-mic"));
    QCOMPARE(monitor.state(), AudioLevelMonitor::State::DeviceMissing);
    QCOMPARE(m_engineState->started, 0);
    QTRY_COMPARE(m_engineState->destroyed, m_engineState->created);
}

void TestRecordingSetup::startFailureIsUnavailable()
{
    m_engineState->startSucceeds = false;
    AudioLevelMonitor monitor;
    monitor.setEngineFactory(factory());
    monitor.setPermissionFunctions([] { return Permission::Authorized; }, {});
    monitor.start(QStringLiteral("usb-mic"));
    QCOMPARE(monitor.state(), AudioLevelMonitor::State::Unavailable);

    AudioLevelMonitor noEngine;
    noEngine.setEngineFactory([] { return nullptr; });
    noEngine.setPermissionFunctions([] { return Permission::Authorized; }, {});
    noEngine.start(QString());
    QCOMPARE(noEngine.state(), AudioLevelMonitor::State::Unavailable);
}

void TestRecordingSetup::levelIsThrottledAndReleasedOnStop()
{
    AudioLevelMonitor monitor;
    FakeLevelEngine* engine = nullptr;
    auto state = m_engineState;
    monitor.setEngineFactory([state, &engine] { return engine = new FakeLevelEngine(state); });
    monitor.setPermissionFunctions([] { return Permission::Authorized; }, {});
    QSignalSpy levelSpy(&monitor, &AudioLevelMonitor::levelChanged);
    monitor.start(QStringLiteral("usb-mic"));
    QCOMPARE(monitor.state(), AudioLevelMonitor::State::Listening);
    QVERIFY(engine);
    QCOMPARE(engine->deviceId(), QStringLiteral("usb-mic"));

    // A burst of callbacks from the audio thread produces at most one GUI
    // update per poll interval, not one per callback.
    constexpr int kCallbacks = 2000;
    QThread* audioThread = QThread::create([engine] {
        for (int i = 0; i < kCallbacks; ++i) {
            emit engine->audioDataReady(pcmWithPeak(16384), i);
        }
    });
    audioThread->start();
    audioThread->wait();
    delete audioThread;
    QTRY_COMPARE_WITH_TIMEOUT(monitor.level(), 0.5f, 1000);
    QVERIFY2(levelSpy.count() < 10, qPrintable(QString::number(levelSpy.count())));

    // Silence is a level of zero, not an error.
    QTest::qWait(AudioLevelMonitor::kPollIntervalMs * 2);
    QCOMPARE(monitor.level(), 0.0f);
    QCOMPARE(monitor.state(), AudioLevelMonitor::State::Listening);

    monitor.stop();
    QCOMPARE(monitor.state(), AudioLevelMonitor::State::Idle);
    QVERIFY(!engine->isRunning());
    QTRY_COMPARE(m_engineState->destroyed, m_engineState->created);
}

void TestRecordingSetup::setupTogglesSaveSettings()
{
    SnapTray::ScreenPickerViewModel viewModel;
    viewModel.setAudioEngineFactory(factory());
    auto& settings = RecordingSettingsManager::instance();
    QVERIFY(!viewModel.microphoneEnabled());
    QVERIFY(!viewModel.systemAudioEnabled());

    viewModel.setMicrophoneEnabled(true);
    QVERIFY(settings.audioEnabled());
    QCOMPARE(settings.audioSource(), SnapTray::kAudioSourceMicrophone);
    viewModel.setSystemAudioEnabled(true);
    QCOMPARE(settings.audioSource(), SnapTray::kAudioSourceBoth);
    viewModel.setMicrophoneEnabled(false);
    QCOMPARE(settings.audioSource(), SnapTray::kAudioSourceSystem);
    viewModel.setSystemAudioEnabled(false);
    QVERIFY(!settings.audioEnabled());

    viewModel.setSelectedDevice(QStringLiteral("usb-mic"));
    QCOMPARE(settings.audioDevice(), QStringLiteral("usb-mic"));

    viewModel.setShowSetupBeforeRecording(true);
    QVERIFY(settings.showSetupBeforeRecording());

    // A new picker reads the saved choices back.
    settings.setAudioEnabled(true);
    settings.setAudioSource(SnapTray::kAudioSourceBoth);
    SnapTray::ScreenPickerViewModel reopened;
    QVERIFY(reopened.microphoneEnabled());
    QVERIFY(reopened.systemAudioEnabled());
    QCOMPARE(reopened.selectedDevice(), QStringLiteral("usb-mic"));
}

void TestRecordingSetup::chooseAndCancelReleaseMicrophone()
{
    QScreen* primaryScreen = QGuiApplication::primaryScreen();
    if (!primaryScreen) {
        QSKIP("No screen available");
    }
    qRegisterMetaType<QScreen*>("QScreen*");
    SnapTray::ScreenSource source;
    source.id = QStringLiteral("screen-a");
    source.name = QStringLiteral("Display A");
    source.screen = primaryScreen;

    for (bool choose : {true, false}) {
        m_engineState = std::make_shared<FakeEngineState>();
        RecordingSettingsManager::instance().setAudioEnabled(false);
        SnapTray::ScreenPickerViewModel viewModel({source});
        viewModel.setAudioEngineFactory(factory());
        viewModel.levelMonitor().setPermissionFunctions([] { return Permission::Authorized; }, {});

        // Testing needs the microphone switched on.
        viewModel.toggleMicrophoneTest();
        QVERIFY(!viewModel.microphoneTestActive());
        viewModel.setMicrophoneEnabled(true);
        viewModel.toggleMicrophoneTest();
        QVERIFY(viewModel.microphoneTestActive());
        QCOMPARE(viewModel.microphoneTestStatus(),
                 SnapTray::ScreenPickerViewModel::tr("Speak to test the microphone"));
        QCOMPARE(m_engineState->started, 1);

        // The test microphone is released before the recording is started,
        // so the two captures never overlap.
        bool releasedBeforeSignal = false;
        auto check = [&] { releasedBeforeSignal = !viewModel.microphoneTestActive(); };
        connect(&viewModel, &SnapTray::ScreenPickerViewModel::screenChosen, this, check);
        connect(&viewModel, &SnapTray::ScreenPickerViewModel::cancelled, this, check);
        if (choose) viewModel.chooseScreen(0);
        else viewModel.cancel();
        QVERIFY(releasedBeforeSignal);
        QTRY_COMPARE(m_engineState->destroyed, m_engineState->created);
    }
}

void TestRecordingSetup::missingSavedDeviceStaysVisible()
{
    RecordingSettingsManager::instance().setAudioDevice(QStringLiteral("unplugged-mic"));
    SnapTray::ScreenPickerViewModel viewModel;
    viewModel.setAudioEngineFactory(factory());
    QVERIFY(!viewModel.devicesLoaded());
    viewModel.loadDevices();
    QVERIFY(viewModel.devicesLoaded());
    const QVariantList devices = viewModel.inputDevices();
    QCOMPARE(devices.size(), 3);
    const QVariantMap missing = devices.last().toMap();
    QCOMPARE(missing.value(QStringLiteral("value")).toString(), QStringLiteral("unplugged-mic"));
    QVERIFY(missing.value(QStringLiteral("missing")).toBool());
    QCOMPARE(viewModel.selectedDevice(), QStringLiteral("unplugged-mic"));

    m_engineState->systemAudioSupported = false;
    viewModel.loadDevices();
    QVERIFY(!viewModel.systemAudioSupported());
}

void TestRecordingSetup::outputNotes()
{
    auto& settings = RecordingSettingsManager::instance();
    settings.setOutputFormat(0);
    settings.setShowPreview(true);
    {
        SnapTray::ScreenPickerViewModel viewModel;
        QVERIFY(viewModel.outputHasAudio());
        QVERIFY(viewModel.audioNote().isEmpty());
        QVERIFY(viewModel.outputSummary().startsWith(QStringLiteral("MP4")));
    }
    settings.setOutputFormat(1);
    {
        SnapTray::ScreenPickerViewModel viewModel;
        QVERIFY(viewModel.outputHasAudio());
        QVERIFY(viewModel.audioNote().contains(QStringLiteral("silent")));
    }
    settings.setShowPreview(false);
    settings.setCountdownEnabled(false);
    {
        SnapTray::ScreenPickerViewModel viewModel;
        QVERIFY(!viewModel.outputHasAudio());
        QVERIFY(viewModel.audioNote().contains(QStringLiteral("no sound")));
        QVERIFY(viewModel.outputSummary().startsWith(QStringLiteral("GIF")));
        QVERIFY(viewModel.outputSummary().contains(QStringLiteral("No countdown")));
    }
    settings.setCountdownEnabled(true);
}

void TestRecordingSetup::dialogShowsSetup()
{
    QScreen* primaryScreen = QGuiApplication::primaryScreen();
    if (!primaryScreen) {
        QSKIP("No screen available");
    }
    SnapTray::ScreenSource source;
    source.id = QStringLiteral("screen-a");
    source.name = QStringLiteral("Display A");
    source.screen = primaryScreen;
    RecordingSettingsManager::instance().setAudioEnabled(true);
    RecordingSettingsManager::instance().setAudioSource(SnapTray::kAudioSourceMicrophone);

    auto* viewModel = new SnapTray::ScreenPickerViewModel({source});
    viewModel->setAudioEngineFactory(factory());
    SnapTray::QmlDialog dialog(QUrl(QStringLiteral("qrc:/SnapTrayQml/dialogs/ScreenPickerDialog.qml")),
                               viewModel, QStringLiteral("viewModel"));
    viewModel->setParent(&dialog);
    dialog.showCenteredOnScreen(primaryScreen);
    QQuickView* view = nullptr;
    for (QWindow* window : QGuiApplication::topLevelWindows()) {
        if (auto* candidate = qobject_cast<QQuickView*>(window); candidate && candidate->isVisible())
            view = candidate;
    }
    QVERIFY(view);
    QVERIFY(view->rootObject());
    auto find = [view](const char* name) {
        return view->rootObject()->findChild<QQuickItem*>(QString::fromLatin1(name));
    };
    QVERIFY(find("recordingSetup"));
    QVERIFY(find("setupMicrophoneToggle"));
    QVERIFY(find("setupSystemAudioToggle"));
    QVERIFY(find("setupShowBeforeRecordingToggle"));
    // One screen: an explicit Start button.
    QVERIFY(find("setupStartButton"));
    QVERIFY(find("setupStartButton")->isVisible());
    // Devices load after the dialog is shown, without blocking its creation.
    QTRY_VERIFY(viewModel->devicesLoaded());
    QVERIFY(find("setupMicrophoneTest")->isVisible());

    viewModel->toggleMicrophoneTest();
    viewModel->levelMonitor().setPermissionFunctions([] { return Permission::Authorized; }, {});
    viewModel->stopMicrophoneTest();
    viewModel->toggleMicrophoneTest();
    QTRY_VERIFY(find("setupMicrophoneLevel")->isVisible());
    viewModel->cancel();
    QVERIFY(!viewModel->microphoneTestActive());
}

void TestRecordingSetup::recoveryDialogLists()
{
    QTemporaryDir dir;
    const QString video = dir.filePath(QStringLiteral("SnapTray_Recording_q.mp4"));
    SnapTray::RecordingSession::Recoverable playable;
    playable.info.videoPath = video;
    playable.info.startedAt = QDateTime::currentDateTime();
    playable.decodable = true;
    playable.durationMs = 65000;
    SnapTray::RecordingSession::Recoverable broken = playable;
    broken.decodable = false;

    auto* viewModel = new SnapTray::RecordingRecoveryViewModel;
    viewModel->setRecoveries({playable, broken});
    SnapTray::QmlDialog dialog(QUrl(QStringLiteral("qrc:/SnapTrayQml/dialogs/RecordingRecoveryDialog.qml")),
                               viewModel, QStringLiteral("viewModel"));
    viewModel->setParent(&dialog);
    dialog.showCenteredOnScreen(QGuiApplication::primaryScreen());
    QQuickView* view = nullptr;
    for (QWindow* window : QGuiApplication::topLevelWindows()) {
        if (auto* candidate = qobject_cast<QQuickView*>(window); candidate && candidate->isVisible())
            view = candidate;
    }
    QVERIFY(view);
    // Recover only where something can be played; Discard on every row.
    int recoverVisible = 0;
    int discardButtons = 0;
    std::function<void(QQuickItem*)> walk = [&](QQuickItem* item) {
        if (item->objectName() == QLatin1String("recoveryRecoverButton") && item->isVisible()) ++recoverVisible;
        if (item->objectName() == QLatin1String("recoveryDiscardButton")) ++discardButtons;
        for (QQuickItem* child : item->childItems()) walk(child);
    };
    walk(qobject_cast<QQuickItem*>(view->rootObject()));
    QCOMPARE(recoverVisible, 1);
    QCOMPARE(discardButtons, 2);
    QSignalSpy recover(viewModel, &SnapTray::RecordingRecoveryViewModel::recoverRequested);
    viewModel->recover(0);
    QCOMPARE(recover.count(), 1);
    QCOMPARE(recover.first().first().toString(), video);
}

QTEST_MAIN(TestRecordingSetup)
#include "tst_RecordingSetup.moc"
