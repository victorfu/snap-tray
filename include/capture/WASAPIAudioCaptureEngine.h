#ifndef WASAPIAUDIOCAPTUREENGINE_H
#define WASAPIAUDIOCAPTUREENGINE_H

#include "IAudioCaptureEngine.h"
#include "capture/TimestampedPcmMixer.h"

#ifdef Q_OS_WIN

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <atomic>
#include <memory>

// Forward declarations - Windows types are opaque pointers here
// Actual types are defined in the .cpp file via Windows headers
struct IMMDeviceEnumerator;
struct IMMDevice;
struct IAudioClient;
struct IAudioCaptureClient;

/**
 * @brief Windows WASAPI-based audio capture engine
 *
 * Uses Windows Audio Session API (WASAPI) for low-latency audio capture.
 * Supports both microphone input and system audio loopback capture.
 */
class WASAPIAudioCaptureEngine : public IAudioCaptureEngine
{
    Q_OBJECT

public:
    explicit WASAPIAudioCaptureEngine(QObject *parent = nullptr);
    ~WASAPIAudioCaptureEngine() override;

    // IAudioCaptureEngine interface
    bool setAudioSource(AudioSource source) override;
    AudioSource audioSource() const override { return m_source; }
    bool setDevice(const QString &deviceId) override;
    AudioFormat audioFormat() const override { return m_format; }

    QList<AudioDevice> availableInputDevices() const override;
    QString defaultInputDevice() const override;

    bool start() override;
    void stop() override;
    void disposeAsync() override;
    void pause() override;
    void resume() override;
    bool isRunning() const override { return m_running; }
    bool isPaused() const override { return m_paused; }

    QString engineName() const override { return QStringLiteral("WASAPI"); }
    bool isAvailable() const override;
    bool isSystemAudioSupported() const override { return true; }

private:
    friend class TestWASAPIAudioCaptureEngineThreadSafetyWin;
    class CaptureThread;
    friend class CaptureThread;

    // Thread-based COM initialization (runs in capture thread)
    bool initializeInThread();
    void cleanupInThread();

    // Legacy COM stubs (kept for compatibility but do nothing)
    bool initializeCOM();
    void uninitializeCOM();

    // Device management (called from capture thread)
    IMMDevice* getDevice(const QString &deviceId, bool forLoopback) const;
    IMMDevice* getDefaultDevice(bool forLoopback) const;

    // Audio client setup (called from capture thread)
    bool setupAudioClient(IMMDevice *device, bool forLoopback);
    void cleanupAudioClient();

    // Convert WASAPI format to our format (takes void* to avoid WAVEFORMATEX in header)
    struct NativeFormatInfo {
        enum class Encoding {
            Unsupported,
            SignedInteger,
            UnsignedInteger,
            Float,
        };

        Encoding encoding = Encoding::Unsupported;
        int bitsPerSample = 16;
        int bytesPerSample = 2;
        int channels = 2;
        int sampleRate = 48000;
    };
    bool updateFormatFromWaveFormat(const void *wfx, NativeFormatInfo &nativeFormat, AudioFormat &outputFormat) const;
    void refreshProbedFormat();

    // Convert audio data from native format to 16-bit PCM
    // Uses unsigned char* to avoid Windows header dependency
    QByteArray convertToInt16PCM(const unsigned char *data, int numFrames,
                                 const NativeFormatInfo &nativeFormat) const;

    AudioSource activeAudioSource() const;
    void notifyActiveSourceChanged();
    void cleanupSource(SnapTray::Audio::Source source);
    void handleSourceFailure(SnapTray::Audio::Source source,
                             long errorCode,
                             qint64 effectiveTimeNs);
    void deliverMixerOutput(
        const SnapTray::Audio::TimestampedPcmMixer::ProcessResult& result);
    void processAudioPacket(SnapTray::Audio::Source source,
                            const QByteArray& pcm,
                            qint64 timestampNs,
                            const NativeFormatInfo& nativeFormat);
    void advanceMixerTimeline(qint64 activeTimeNs);
    qint64 currentActiveTimeNs() const;
    qint64 packetTimestampNs(quint64 qpcPosition100ns,
                             quint32 flags,
                             int numFrames,
                             const NativeFormatInfo& nativeFormat) const;

    // Capture loop (runs in separate thread)
    void captureLoop();
    void enableDataCallbacks();
    void drainDataCallbacks(bool disconnectConnections);
    void deliverAudioData(const QByteArray &data, qint64 startFrame);
    void finishDataCallback();

    // Deletes the thread object only after QThread has actually stopped. A timed-out
    // WASAPI/COM call can keep initialization blocked after start() has failed.
    bool releaseCaptureThreadIfFinished();
    void onCaptureThreadFinished();

    // State
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_initDone{false};      // Thread initialization complete flag
    std::atomic<bool> m_initSuccess{false};   // Thread initialization success flag

    // COM and WASAPI objects (owned by capture thread)
    IMMDeviceEnumerator *m_deviceEnumerator = nullptr;

    // Microphone capture
    IMMDevice *m_micDevice = nullptr;
    IAudioClient *m_micAudioClient = nullptr;
    IAudioCaptureClient *m_micCaptureClient = nullptr;

    // System audio loopback capture
    IMMDevice *m_loopbackDevice = nullptr;
    IAudioClient *m_loopbackAudioClient = nullptr;
    IAudioCaptureClient *m_loopbackCaptureClient = nullptr;

    // Capture thread
    QThread *m_captureThread = nullptr;
    bool m_disposePending = false;

    // Direct audioDataReady connections run on the capture thread. Teardown
    // closes this gate and waits for signal delivery already in progress before
    // a receiver such as EncodingWorker may be destroyed.
    QMutex m_dataCallbackMutex;
    QWaitCondition m_dataCallbacksDrained;
    bool m_acceptingDataCallbacks = false;
    int m_activeDataCallbacks = 0;

    // Timing
    qint64 m_startTime = 0;
    qint64 m_startQpc100ns = 0;
    std::atomic<qint64> m_stopActiveTimeNs{-1};
    qint64 m_pausedDuration = 0;
    qint64 m_pauseStartTime = 0;
    mutable QMutex m_timingMutex;

    // Native audio format info (for conversion and mixing)
    NativeFormatInfo m_micNativeFormat;
    NativeFormatInfo m_loopbackNativeFormat;
    std::unique_ptr<SnapTray::Audio::TimestampedPcmMixer> m_mixer;
    QMutex m_mixerDeliveryMutex;
    std::atomic<bool> m_microphoneActive{false};
    std::atomic<bool> m_systemAudioActive{false};
    std::atomic<int> m_lastNotifiedActiveSource{
        static_cast<int>(AudioSource::None)};
    std::atomic<bool> m_reportedMixerDrop{false};
};

#endif // Q_OS_WIN

#endif // WASAPIAUDIOCAPTUREENGINE_H
