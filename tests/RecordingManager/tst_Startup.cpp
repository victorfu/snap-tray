#include <QtTest>
#include <QScreen>
#include <QGuiApplication>
#include <QTimer>
#include "RecordingManager.h"
#include "settings/RecordingSettingsManager.h"

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
private:
    void prepare(RecordingManager& manager);
};

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

QTEST_MAIN(TestRecordingStartup)
#include "tst_Startup.moc"
