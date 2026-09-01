#include <QtTest/QtTest>

#ifdef Q_OS_WIN

#include "capture/WASAPIAudioCaptureEngine.h"
#include "utils/ResourceCleanupHelper.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QSemaphore>
#include <QThread>

#include <memory>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>

#include <windows.h>
#include <audioclient.h>

namespace {

class BlockingThread final : public QThread
{
public:
    QSemaphore entered;
    QSemaphore release;

protected:
    void run() override
    {
        entered.release();
        release.acquire();
    }
};

} // namespace

class TestWASAPIAudioCaptureEngineThreadSafetyWin : public QObject
{
    Q_OBJECT

private slots:
    void runningThreadIsRetainedUntilItFinishes();
    void disposalReturnsWithoutWaitingForRunningThread();
    void disposalAllowsResponsiveWorkerTail();
    void disposalCleansUpAlreadyFinishedThread();
    void disposalWaitsForInFlightDirectCallback();
    void stopPreservesConnectionsForRestart();
    void outputFormatIsCanonical();
    void mixerDeliveryPreservesFrameTimestamp();
    void farFuturePacketIsRejectedBeforeMixerDrain();
    void parsesSupportedWaveFormats();
    void convertsSupportedNativeFormats();
    void mismatchedSourcesMixThroughCanonicalPipeline();
    void canonicalPipelineKeepsPendingBounded();
    void periodicTimelineAdvanceBoundsSilentOutput();
    void packetTimestampUsesSharedQpcTimeline();
    void stalePacketIsRejectedAcrossPauseResume();
    void sourceFailureReportsExplicitDegradation();
};

void TestWASAPIAudioCaptureEngineThreadSafetyWin::runningThreadIsRetainedUntilItFinishes()
{
    WASAPIAudioCaptureEngine engine;
    auto *thread = new BlockingThread;
    engine.m_captureThread = thread;
    thread->start();

    QVERIFY2(thread->entered.tryAcquire(1, 1000), "Test thread did not start");
    QVERIFY(!engine.releaseCaptureThreadIfFinished());
    QCOMPARE(engine.m_captureThread, static_cast<QThread *>(thread));
    QVERIFY(thread->isRunning());

    thread->release.release();
    QVERIFY2(thread->wait(1000), "Test thread did not stop");
    QVERIFY(engine.releaseCaptureThreadIfFinished());
    QVERIFY(engine.m_captureThread == nullptr);
}

void TestWASAPIAudioCaptureEngineThreadSafetyWin::disposalReturnsWithoutWaitingForRunningThread()
{
    auto *engine = new WASAPIAudioCaptureEngine;
    auto *thread = new BlockingThread;
    engine->m_captureThread = thread;
    QObject::connect(thread, &QThread::finished,
                     engine, &WASAPIAudioCaptureEngine::onCaptureThreadFinished,
                     Qt::QueuedConnection);
    thread->start();
    QVERIFY2(thread->entered.tryAcquire(1, 1000), "Test thread did not start");

    engine->m_startTime = 1000;
    engine->m_pauseStartTime = 1600;
    engine->m_pausedDuration = 100;
    engine->m_paused = true;
    engine->m_running = true;

    QPointer<WASAPIAudioCaptureEngine> engineGuard(engine);
    QPointer<QThread> threadGuard(thread);
    AudioEnginePtr owner(engine);

    QElapsedTimer elapsed;
    elapsed.start();
    owner.reset();
    QVERIFY2(elapsed.elapsed() < 250, "Async disposal blocked on the running thread");
    QVERIFY(!engineGuard.isNull());
    QVERIFY(threadGuard && threadGuard->isRunning());
    QCOMPARE(engine->m_stopActiveTimeNs.load(), qint64(500000000));

    thread->release.release();
    QTRY_VERIFY_WITH_TIMEOUT(threadGuard.isNull(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(engineGuard.isNull(), 2000);
}

void TestWASAPIAudioCaptureEngineThreadSafetyWin::disposalAllowsResponsiveWorkerTail()
{
    auto *engine = new WASAPIAudioCaptureEngine;
    engine->enableDataCallbacks();

    int deliveryCount = 0;
    QObject receiver;
    QObject::connect(engine, &IAudioCaptureEngine::audioDataReady,
                     &receiver, [&deliveryCount](const QByteArray& data, qint64) {
        if (data == QByteArrayLiteral("tail")) {
            ++deliveryCount;
        }
    }, Qt::DirectConnection);

    QSemaphore entered;
    engine->m_captureThread = QThread::create([engine, &entered]() {
        entered.release();
        while (!engine->m_stopRequested) {
            QThread::msleep(1);
        }
        engine->deliverAudioData(QByteArrayLiteral("tail"), 0);
    });
    engine->m_captureThread->start();
    QVERIFY2(entered.tryAcquire(1, 1000), "Test thread did not start");

    QPointer<WASAPIAudioCaptureEngine> engineGuard(engine);
    AudioEnginePtr owner(engine);
    owner.reset();

    QCOMPARE(deliveryCount, 1);
    QTRY_VERIFY_WITH_TIMEOUT(engineGuard.isNull(), 2000);
}

void TestWASAPIAudioCaptureEngineThreadSafetyWin::disposalCleansUpAlreadyFinishedThread()
{
    auto *engine = new WASAPIAudioCaptureEngine;
    auto *thread = new BlockingThread;
    engine->m_captureThread = thread;
    QObject::connect(thread, &QThread::finished,
                     engine, &WASAPIAudioCaptureEngine::onCaptureThreadFinished,
                     Qt::QueuedConnection);
    thread->start();
    QVERIFY2(thread->entered.tryAcquire(1, 1000), "Test thread did not start");
    thread->release.release();
    QVERIFY2(thread->wait(1000), "Test thread did not stop");

    QPointer<WASAPIAudioCaptureEngine> engineGuard(engine);
    QPointer<QThread> threadGuard(thread);
    AudioEnginePtr owner(engine);
    owner.reset();

    QTRY_VERIFY_WITH_TIMEOUT(threadGuard.isNull(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(engineGuard.isNull(), 2000);
}

void TestWASAPIAudioCaptureEngineThreadSafetyWin::disposalWaitsForInFlightDirectCallback()
{
    auto *engine = new WASAPIAudioCaptureEngine;
    QObject receiver;
    QSemaphore callbackEntered;
    QSemaphore releaseCallback;
    QSemaphore disposalFinished;

    QObject::connect(engine, &IAudioCaptureEngine::audioDataReady,
                     &receiver, [&callbackEntered, &releaseCallback](const QByteArray &, qint64) {
        callbackEntered.release();
        releaseCallback.acquire();
    }, Qt::DirectConnection);
    engine->enableDataCallbacks();

    QThread *emitterThread = QThread::create([engine]() {
        engine->deliverAudioData(QByteArrayLiteral("audio"), 0);
    });
    emitterThread->start();
    QVERIFY2(callbackEntered.tryAcquire(1, 1000), "Direct callback did not start");

    QPointer<WASAPIAudioCaptureEngine> engineGuard(engine);
    auto owner = std::make_unique<AudioEnginePtr>(engine);
    QThread *disposalThread = QThread::create([ownerPtr = owner.get(),
                                               &disposalFinished]() {
        ownerPtr->reset();
        disposalFinished.release();
    });
    disposalThread->start();

    const auto callbackGateClosed = [engine]() {
        QMutexLocker locker(&engine->m_dataCallbackMutex);
        return !engine->m_acceptingDataCallbacks;
    };
    QTRY_VERIFY_WITH_TIMEOUT(callbackGateClosed(), 1000);

    QVERIFY2(!disposalFinished.tryAcquire(),
             "Disposal returned while a DirectConnection callback was still running");

    releaseCallback.release();
    QVERIFY2(emitterThread->wait(1000), "Emitter thread did not finish");
    QVERIFY2(disposalFinished.tryAcquire(1, 1000), "Disposal did not drain the callback");
    QVERIFY2(disposalThread->wait(1000), "Disposal thread did not finish");

    delete emitterThread;
    delete disposalThread;
    QTRY_VERIFY_WITH_TIMEOUT(engineGuard.isNull(), 2000);
}

void TestWASAPIAudioCaptureEngineThreadSafetyWin::stopPreservesConnectionsForRestart()
{
    WASAPIAudioCaptureEngine engine;
    int deliveryCount = 0;

    QObject::connect(&engine, &IAudioCaptureEngine::audioDataReady,
                     &engine, [&deliveryCount](const QByteArray &, qint64) {
        ++deliveryCount;
    }, Qt::DirectConnection);

    engine.enableDataCallbacks();
    engine.deliverAudioData(QByteArrayLiteral("first"), 0);
    QCOMPARE(deliveryCount, 1);

    engine.stop();
    engine.enableDataCallbacks();
    engine.deliverAudioData(QByteArrayLiteral("second"), 1);
    QCOMPARE(deliveryCount, 2);
}

void TestWASAPIAudioCaptureEngineThreadSafetyWin::outputFormatIsCanonical()
{
    WASAPIAudioCaptureEngine engine;
    const auto format = engine.audioFormat();
    QCOMPARE(format.sampleRate, 48000);
    QCOMPARE(format.channels, 2);
    QCOMPARE(format.bitsPerSample, 16);
}

void TestWASAPIAudioCaptureEngineThreadSafetyWin::mixerDeliveryPreservesFrameTimestamp()
{
    WASAPIAudioCaptureEngine engine;
    engine.enableDataCallbacks();
    QSignalSpy audioSpy(&engine, &IAudioCaptureEngine::audioDataReady);

    SnapTray::Audio::TimestampedPcmMixer::ProcessResult result;
    SnapTray::Audio::OutputChunk chunk;
    chunk.pcm = QByteArray(128, '\0');
    chunk.startFrame = 512;
    result.output.append(chunk);
    engine.deliverMixerOutput(result);

    QCOMPARE(audioSpy.count(), 1);
    QCOMPARE(audioSpy.first().at(1).toLongLong(), qint64(512));
}

void TestWASAPIAudioCaptureEngineThreadSafetyWin::farFuturePacketIsRejectedBeforeMixerDrain()
{
    WASAPIAudioCaptureEngine engine;
    SnapTray::Audio::TimestampedPcmMixer::Config config;
    config.microphoneEnabled = true;
    engine.m_mixer = std::make_unique<SnapTray::Audio::TimestampedPcmMixer>(config);
    engine.m_startTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    engine.enableDataCallbacks();

    QSignalSpy audioSpy(&engine, &IAudioCaptureEngine::audioDataReady);
    QSignalSpy warningSpy(&engine, &IAudioCaptureEngine::warning);
    WASAPIAudioCaptureEngine::NativeFormatInfo format;
    format.channels = 1;
    format.sampleRate = 48000;
    const QByteArray packet(480 * 2, '\0');

    engine.processAudioPacket(
        SnapTray::Audio::Source::Microphone,
        packet,
        3600LL * 1000000000LL,
        format);

    QCOMPARE(audioSpy.count(), 0);
    QCOMPARE(warningSpy.count(), 1);
    QCOMPARE(engine.m_mixer->stats().futureTimestampPackets, qint64(1));
    QCOMPARE(engine.m_mixer->pendingFrames(
                 SnapTray::Audio::Source::Microphone), qint64(0));

    engine.processAudioPacket(
        SnapTray::Audio::Source::Microphone,
        packet,
        0,
        format);
    QCOMPARE(audioSpy.count(), 1);
    QCOMPARE(audioSpy.first().at(1).toLongLong(), qint64(0));
}

void TestWASAPIAudioCaptureEngineThreadSafetyWin::parsesSupportedWaveFormats()
{
    WASAPIAudioCaptureEngine engine;
    WASAPIAudioCaptureEngine::NativeFormatInfo nativeFormat;
    IAudioCaptureEngine::AudioFormat outputFormat;

    WAVEFORMATEX pcm{};
    pcm.wFormatTag = WAVE_FORMAT_PCM;
    pcm.nChannels = 1;
    pcm.nSamplesPerSec = 44100;
    pcm.wBitsPerSample = 16;
    pcm.nBlockAlign = 2;
    pcm.nAvgBytesPerSec = pcm.nSamplesPerSec * pcm.nBlockAlign;
    QVERIFY(engine.updateFormatFromWaveFormat(&pcm, nativeFormat, outputFormat));
    QCOMPARE(nativeFormat.encoding,
             WASAPIAudioCaptureEngine::NativeFormatInfo::Encoding::SignedInteger);
    QCOMPARE(nativeFormat.sampleRate, 44100);
    QCOMPARE(nativeFormat.channels, 1);

    WAVEFORMATEX floating = pcm;
    floating.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    floating.nChannels = 2;
    floating.nSamplesPerSec = 48000;
    floating.wBitsPerSample = 32;
    floating.nBlockAlign = 8;
    floating.nAvgBytesPerSec = floating.nSamplesPerSec * floating.nBlockAlign;
    QVERIFY(engine.updateFormatFromWaveFormat(&floating, nativeFormat, outputFormat));
    QCOMPARE(nativeFormat.encoding,
             WASAPIAudioCaptureEngine::NativeFormatInfo::Encoding::Float);

    WAVEFORMATEX unsupported = pcm;
    unsupported.wFormatTag = WAVE_FORMAT_ALAW;
    QVERIFY(!engine.updateFormatFromWaveFormat(
        &unsupported, nativeFormat, outputFormat));

    WAVEFORMATEX tooManyChannels = pcm;
    tooManyChannels.nChannels = 33;
    tooManyChannels.nBlockAlign = 66;
    QVERIFY(!engine.updateFormatFromWaveFormat(
        &tooManyChannels, nativeFormat, outputFormat));

    WAVEFORMATEX excessiveRate = pcm;
    excessiveRate.nSamplesPerSec = 384001;
    QVERIFY(!engine.updateFormatFromWaveFormat(
        &excessiveRate, nativeFormat, outputFormat));
}

void TestWASAPIAudioCaptureEngineThreadSafetyWin::convertsSupportedNativeFormats()
{
    WASAPIAudioCaptureEngine engine;

    WASAPIAudioCaptureEngine::NativeFormatInfo format;
    format.encoding = WASAPIAudioCaptureEngine::NativeFormatInfo::Encoding::SignedInteger;
    format.bitsPerSample = 16;
    format.bytesPerSample = 2;
    format.channels = 1;
    const int16_t pcm16[] = {-32768, 1234, 32767};
    const QByteArray copied = engine.convertToInt16PCM(
        reinterpret_cast<const unsigned char *>(pcm16), 3, format);
    QCOMPARE(copied.size(), qsizetype(sizeof(pcm16)));
    QCOMPARE(memcmp(copied.constData(), pcm16, sizeof(pcm16)), 0);

    format.encoding = WASAPIAudioCaptureEngine::NativeFormatInfo::Encoding::Float;
    format.bitsPerSample = 32;
    format.bytesPerSample = 4;
    format.channels = 2;
    const float floats[] = {1.0f, -1.0f};
    const QByteArray convertedFloat = engine.convertToInt16PCM(
        reinterpret_cast<const unsigned char *>(floats), 1, format);
    const auto *floatSamples = reinterpret_cast<const int16_t *>(
        convertedFloat.constData());
    QCOMPARE(floatSamples[0], int16_t(32767));
    QCOMPARE(floatSamples[1], int16_t(-32767));

    format.encoding = WASAPIAudioCaptureEngine::NativeFormatInfo::Encoding::UnsignedInteger;
    format.bitsPerSample = 8;
    format.bytesPerSample = 1;
    format.channels = 1;
    const unsigned char pcm8[] = {0, 128, 255};
    const QByteArray converted8 = engine.convertToInt16PCM(pcm8, 3, format);
    const auto *samples8 = reinterpret_cast<const int16_t *>(converted8.constData());
    QCOMPARE(samples8[0], int16_t(-32768));
    QCOMPARE(samples8[1], int16_t(0));
    QCOMPARE(samples8[2], int16_t(32512));

    format.encoding = WASAPIAudioCaptureEngine::NativeFormatInfo::Encoding::SignedInteger;
    format.bitsPerSample = 24;
    format.bytesPerSample = 3;
    const unsigned char pcm24[] = {0xff, 0xff, 0x7f, 0x00, 0x00, 0x80};
    const QByteArray converted24 = engine.convertToInt16PCM(pcm24, 2, format);
    const auto *samples24 = reinterpret_cast<const int16_t *>(converted24.constData());
    QCOMPARE(samples24[0], int16_t(32767));
    QCOMPARE(samples24[1], int16_t(-32768));
}

void TestWASAPIAudioCaptureEngineThreadSafetyWin::mismatchedSourcesMixThroughCanonicalPipeline()
{
    WASAPIAudioCaptureEngine engine;
    SnapTray::Audio::TimestampedPcmMixer::Config config;
    config.microphoneEnabled = true;
    config.systemAudioEnabled = true;
    engine.m_mixer = std::make_unique<SnapTray::Audio::TimestampedPcmMixer>(config);
    engine.enableDataCallbacks();

    QByteArray delivered;
    QObject::connect(&engine, &IAudioCaptureEngine::audioDataReady,
                     &engine, [&delivered](const QByteArray& pcm, qint64) {
        delivered.append(pcm);
    }, Qt::DirectConnection);

    WASAPIAudioCaptureEngine::NativeFormatInfo microphone;
    microphone.encoding =
        WASAPIAudioCaptureEngine::NativeFormatInfo::Encoding::SignedInteger;
    microphone.bitsPerSample = 16;
    microphone.bytesPerSample = 2;
    microphone.channels = 1;
    microphone.sampleRate = 44100;
    QByteArray microphonePcm(441 * static_cast<int>(sizeof(int16_t)), 0);
    auto *microphoneSamples = reinterpret_cast<int16_t *>(microphonePcm.data());
    std::fill_n(microphoneSamples, 441, int16_t(1000));

    WASAPIAudioCaptureEngine::NativeFormatInfo systemAudio = microphone;
    systemAudio.channels = 2;
    systemAudio.sampleRate = 48000;
    QByteArray systemPcm(480 * 2 * static_cast<int>(sizeof(int16_t)), 0);
    auto *systemSamples = reinterpret_cast<int16_t *>(systemPcm.data());
    std::fill_n(systemSamples, 480 * 2, int16_t(2000));

    engine.processAudioPacket(
        SnapTray::Audio::Source::Microphone, microphonePcm, 0, microphone);
    engine.processAudioPacket(
        SnapTray::Audio::Source::SystemAudio, systemPcm, 0, systemAudio);

    QVERIFY(!delivered.isEmpty());
    QCOMPARE(delivered.size() % (2 * static_cast<int>(sizeof(int16_t))), 0);
    const auto *mixedSamples = reinterpret_cast<const int16_t *>(delivered.constData());
    const qsizetype sampleCount = delivered.size() / static_cast<int>(sizeof(int16_t));
    for (qsizetype i = 0; i < sampleCount; ++i) {
        QCOMPARE(mixedSamples[i], int16_t(3000));
    }

    engine.stop();
}

void TestWASAPIAudioCaptureEngineThreadSafetyWin::canonicalPipelineKeepsPendingBounded()
{
    WASAPIAudioCaptureEngine engine;
    SnapTray::Audio::TimestampedPcmMixer::Config config;
    config.microphoneEnabled = true;
    config.systemAudioEnabled = true;
    config.maxPendingFramesPerSource = 480;
    config.maxPendingBytesPerSource = 480 * 2 * sizeof(int16_t);
    engine.m_mixer = std::make_unique<SnapTray::Audio::TimestampedPcmMixer>(config);

    WASAPIAudioCaptureEngine::NativeFormatInfo format;
    format.encoding = WASAPIAudioCaptureEngine::NativeFormatInfo::Encoding::SignedInteger;
    format.bitsPerSample = 16;
    format.bytesPerSample = 2;
    format.channels = 2;
    format.sampleRate = 48000;
    const QByteArray packet(480 * 2 * static_cast<int>(sizeof(int16_t)), 0);

    for (int i = 0; i < 100; ++i) {
        engine.processAudioPacket(
            SnapTray::Audio::Source::Microphone,
            packet,
            static_cast<qint64>(i) * 10000000,
            format);
    }

    QVERIFY(engine.m_mixer->pendingFrames(
                SnapTray::Audio::Source::Microphone)
            <= config.maxPendingFramesPerSource);
    QVERIFY(engine.m_mixer->pendingBytes(
                SnapTray::Audio::Source::Microphone)
            <= config.maxPendingBytesPerSource);
}

void TestWASAPIAudioCaptureEngineThreadSafetyWin::periodicTimelineAdvanceBoundsSilentOutput()
{
    WASAPIAudioCaptureEngine engine;
    SnapTray::Audio::TimestampedPcmMixer::Config config;
    config.microphoneEnabled = true;
    engine.m_mixer = std::make_unique<SnapTray::Audio::TimestampedPcmMixer>(config);
    engine.m_microphoneActive = true;
    engine.enableDataCallbacks();

    qsizetype maxPacketBytes = 0;
    qint64 totalBytes = 0;
    QObject::connect(&engine, &IAudioCaptureEngine::audioDataReady,
                     &engine, [&maxPacketBytes, &totalBytes](const QByteArray& pcm, qint64) {
        maxPacketBytes = qMax(maxPacketBytes, pcm.size());
        totalBytes += pcm.size();
    }, Qt::DirectConnection);

    for (int second = 1; second <= 60; ++second) {
        engine.advanceMixerTimeline(
            static_cast<qint64>(second) * 1000000000);
    }

    QVERIFY(totalBytes > 0);
    QVERIFY(maxPacketBytes
            <= config.outputChunkFrames
                * SnapTray::Audio::TimestampedPcmMixer::kOutputChannels
                * static_cast<int>(sizeof(int16_t)));
    engine.stop();
}

void TestWASAPIAudioCaptureEngineThreadSafetyWin::packetTimestampUsesSharedQpcTimeline()
{
    WASAPIAudioCaptureEngine engine;
    engine.m_startQpc100ns = 1000000000;
    engine.m_pausedDuration = 3;

    WASAPIAudioCaptureEngine::NativeFormatInfo format;
    format.encoding = WASAPIAudioCaptureEngine::NativeFormatInfo::Encoding::SignedInteger;
    format.sampleRate = 48000;
    format.channels = 2;
    format.bitsPerSample = 16;
    format.bytesPerSample = 2;

    QCOMPARE(engine.packetTimestampNs(
                 static_cast<quint64>(engine.m_startQpc100ns + 100000),
                 0,
                 480,
                 format),
             qint64(7000000));
}

void TestWASAPIAudioCaptureEngineThreadSafetyWin::stalePacketIsRejectedAcrossPauseResume()
{
    WASAPIAudioCaptureEngine engine;
    SnapTray::Audio::TimestampedPcmMixer::Config config;
    config.microphoneEnabled = true;
    config.systemAudioEnabled = true;
    engine.m_mixer = std::make_unique<SnapTray::Audio::TimestampedPcmMixer>(config);
    engine.m_startTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    engine.m_running = true;

    WASAPIAudioCaptureEngine::NativeFormatInfo format;
    format.channels = 1;
    format.sampleRate = 48000;
    const QByteArray packet(480 * static_cast<int>(sizeof(int16_t)), '\0');

    const quint64 staleGeneration = engine.m_pauseGeneration.load();
    engine.pause();
    engine.resume();
    QCOMPARE(engine.m_pauseGeneration.load(), staleGeneration + 2);

    QVERIFY(!engine.processCapturedAudioPacket(
        SnapTray::Audio::Source::Microphone,
        packet,
        0,
        format,
        staleGeneration));
    QCOMPARE(engine.m_mixer->pendingFrames(
                 SnapTray::Audio::Source::Microphone),
             qint64(0));

    const quint64 currentGeneration = engine.m_pauseGeneration.load();
    const qint64 currentTimestampNs = engine.currentActiveTimeNs();
    QVERIFY(engine.processCapturedAudioPacket(
        SnapTray::Audio::Source::Microphone,
        packet,
        currentTimestampNs,
        format,
        currentGeneration));
    QVERIFY(engine.m_mixer->pendingFrames(
                SnapTray::Audio::Source::Microphone) > 0);
}

void TestWASAPIAudioCaptureEngineThreadSafetyWin::sourceFailureReportsExplicitDegradation()
{
    WASAPIAudioCaptureEngine engine;
    SnapTray::Audio::TimestampedPcmMixer::Config config;
    config.microphoneEnabled = true;
    config.systemAudioEnabled = true;
    engine.m_mixer = std::make_unique<SnapTray::Audio::TimestampedPcmMixer>(config);
    engine.m_source = IAudioCaptureEngine::AudioSource::Both;
    engine.m_lastNotifiedActiveSource = static_cast<int>(
        IAudioCaptureEngine::AudioSource::Both);
    engine.m_microphoneActive = true;
    engine.m_systemAudioActive = true;
    engine.m_running = true;
    engine.m_stopRequested = false;

    QSignalSpy sourceSpy(&engine, &IAudioCaptureEngine::activeSourceChanged);
    QSignalSpy deviceSpy(&engine, &IAudioCaptureEngine::deviceLost);

    engine.handleSourceFailure(
        SnapTray::Audio::Source::Microphone,
        AUDCLNT_E_DEVICE_INVALIDATED,
        0);
    QCOMPARE(sourceSpy.size(), 1);
    QCOMPARE(qvariant_cast<IAudioCaptureEngine::AudioSource>(
                 sourceSpy.constFirst().constFirst()),
             IAudioCaptureEngine::AudioSource::SystemAudio);
    QCOMPARE(deviceSpy.size(), 1);
    QVERIFY(!engine.m_stopRequested);

    engine.handleSourceFailure(
        SnapTray::Audio::Source::Microphone,
        AUDCLNT_E_DEVICE_INVALIDATED,
        0);
    QCOMPARE(sourceSpy.size(), 1);
    QCOMPARE(deviceSpy.size(), 1);

    engine.handleSourceFailure(
        SnapTray::Audio::Source::SystemAudio,
        AUDCLNT_E_RESOURCES_INVALIDATED,
        10000000);
    QCOMPARE(sourceSpy.size(), 2);
    QCOMPARE(qvariant_cast<IAudioCaptureEngine::AudioSource>(
                 sourceSpy.constLast().constFirst()),
             IAudioCaptureEngine::AudioSource::None);
    QCOMPARE(deviceSpy.size(), 2);
    QVERIFY(engine.m_stopRequested);
    QVERIFY(!engine.m_running);
}

QTEST_GUILESS_MAIN(TestWASAPIAudioCaptureEngineThreadSafetyWin)
#include "tst_WASAPIAudioCaptureEngineThreadSafety_win.moc"

#else

class TestWASAPIAudioCaptureEngineThreadSafetyWin : public QObject
{
    Q_OBJECT
};

QTEST_GUILESS_MAIN(TestWASAPIAudioCaptureEngineThreadSafetyWin)
#include "tst_WASAPIAudioCaptureEngineThreadSafety_win.moc"

#endif // Q_OS_WIN
