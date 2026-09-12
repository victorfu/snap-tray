#include <QtTest>
#include <QSignalSpy>
#include "capture/CoreAudioCaptureEngine.h"
#import <AVFoundation/AVFoundation.h>
#include <chrono>
#include <thread>

class TestCoreAudioRuntime : public QObject
{
    Q_OBJECT
private slots:
    void notificationDegradesOnlySelectedSource_data();
    void notificationDegradesOnlySelectedSource();
    void stoppedAndDestroyedEnginesIgnoreQueuedNotifications();
private:
    void arm(CoreAudioCaptureEngine& engine, NSObject* session, NSObject* device, bool systemAudio);
};

void TestCoreAudioRuntime::arm(CoreAudioCaptureEngine& engine, NSObject* session, NSObject* device, bool systemAudio)
{
    // Exercise real NotificationCenter subscriptions without opening hardware
    // or prompting for access to the user's microphone.
    engine.m_source = systemAudio ? IAudioCaptureEngine::AudioSource::Both
                                 : IAudioCaptureEngine::AudioSource::Microphone;
    engine.m_running = true;
    engine.m_paused = false;
    engine.m_starting = false;
    engine.m_stopping = false;
    engine.m_microphoneActive = true;
    engine.m_systemAudioActive = systemAudio;
    engine.m_microphoneFailed = false;
    engine.m_lastNotifiedActiveSource = static_cast<int>(engine.m_source);
    engine.m_startTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    engine.m_pausedDuration = 0;
    SnapTray::Audio::TimestampedPcmMixer::Config config;
    config.microphoneEnabled = true;
    config.systemAudioEnabled = systemAudio;
    engine.m_mixer = std::make_unique<SnapTray::Audio::TimestampedPcmMixer>(config);
    engine.installMicrophoneObservers((__bridge void*)session, (__bridge void*)device);
}

void TestCoreAudioRuntime::notificationDegradesOnlySelectedSource_data()
{
    QTest::addColumn<int>("notification");
    QTest::addColumn<bool>("systemAudio");
    QTest::addColumn<bool>("paused");
    for (int notification : {0, 1, 2}) {
        for (bool systemAudio : {false, true}) {
            for (bool paused : {false, true}) {
                QTest::addRow("notification-%d-system-%d-paused-%d", notification, systemAudio, paused)
                    << notification << systemAudio << paused;
            }
        }
    }
}

void TestCoreAudioRuntime::notificationDegradesOnlySelectedSource()
{
    QFETCH(int, notification);
    QFETCH(bool, systemAudio);
    QFETCH(bool, paused);
    CoreAudioCaptureEngine engine;
    NSObject* session = [NSObject new];
    NSObject* device = [NSObject new];
    NSObject* unrelated = [NSObject new];
    arm(engine, session, device, systemAudio);
    if (paused) engine.pause();
    QSignalSpy changed(&engine, &IAudioCaptureEngine::activeSourceChanged);
    QSignalSpy audio(&engine, &IAudioCaptureEngine::audioDataReady);
    NSArray* names = @[AVCaptureDeviceWasDisconnectedNotification,
                       AVCaptureSessionRuntimeErrorNotification,
                       AVCaptureSessionDidStopRunningNotification];
    NSNotificationName name = names[notification];
    [[NSNotificationCenter defaultCenter] postNotificationName:name object:unrelated];
    QCoreApplication::processEvents();
    QCOMPARE(changed.count(), 0);

    NSObject* target = notification == 0 ? device : session;
    std::thread nativeCallback([=] {
        @autoreleasepool {
            [[NSNotificationCenter defaultCenter] postNotificationName:name object:target];
        }
    });
    nativeCallback.join();
    QTRY_COMPARE(changed.count(), 1);
    QCOMPARE(changed.first()[0].value<IAudioCaptureEngine::AudioSource>(),
             systemAudio ? IAudioCaptureEngine::AudioSource::SystemAudio : IAudioCaptureEngine::AudioSource::None);
    QVERIFY(engine.isRunning());
    QCOMPARE(engine.isPaused(), paused);
    QVERIFY(!engine.m_microphoneActive.load());
    QCOMPARE(engine.m_systemAudioActive.load(), systemAudio);
    QCOMPARE(engine.audioSource(), systemAudio ? IAudioCaptureEngine::AudioSource::Both
                                              : IAudioCaptureEngine::AudioSource::Microphone);
    // Subsequent failure and reconnection notifications cannot re-enable or
    // repeatedly report a source already retired for this recording.
    [[NSNotificationCenter defaultCenter] postNotificationName:name object:target];
    [[NSNotificationCenter defaultCenter] postNotificationName:AVCaptureDeviceWasConnectedNotification object:device];
    QCoreApplication::processEvents();
    QCOMPARE(changed.count(), 1);
    if (paused) engine.resume();
    const SnapTray::Audio::Pcm16Format format{48000, 2};
    const QByteArray pcm(480 * 4, '\1');
    const auto micResult = engine.m_mixer->push(SnapTray::Audio::Source::Microphone,
        {pcm, 1000000000, format}, 1020000000);
    QCOMPARE(micResult.code, SnapTray::Audio::TimestampedPcmMixer::ResultCode::SourceDisabled);
    if (systemAudio) {
        engine.processCapturedAudio(SnapTray::Audio::Source::SystemAudio, pcm, format, 1000000000, 1020000000);
        engine.deliverMixerOutput(engine.m_mixer->advanceTo(1020000000));
        bool hasSystemSamples = false;
        for (const auto& event : audio) hasSystemSamples |= event[0].toByteArray().contains('\1');
        QVERIFY(hasSystemSamples);
    }
    engine.stop();
}

void TestCoreAudioRuntime::stoppedAndDestroyedEnginesIgnoreQueuedNotifications()
{
    auto engine = std::make_unique<CoreAudioCaptureEngine>();
    NSObject* session = [NSObject new];
    NSObject* device = [NSObject new];
    arm(*engine, session, device, true);
    QSignalSpy changed(engine.get(), &IAudioCaptureEngine::activeSourceChanged);
    [[NSNotificationCenter defaultCenter] postNotificationName:AVCaptureDeviceWasDisconnectedNotification object:device];
    engine->stop();
    // A new subscription has a distinct bridge, even on the same engine/device.
    arm(*engine, session, device, true);
    QCoreApplication::processEvents();
    QCOMPARE(changed.count(), 0);
    QVERIFY(engine->m_microphoneActive.load());
    [[NSNotificationCenter defaultCenter] postNotificationName:AVCaptureSessionRuntimeErrorNotification object:session];
    engine.reset();
    QCoreApplication::processEvents();
    QCOMPARE(changed.count(), 0);
    // Observer tokens have also been removed; posting again remains harmless.
    [[NSNotificationCenter defaultCenter] postNotificationName:AVCaptureDeviceWasDisconnectedNotification object:device];
    QCoreApplication::processEvents();
}

QTEST_GUILESS_MAIN(TestCoreAudioRuntime)
#include "tst_CoreAudioRuntime.moc"
