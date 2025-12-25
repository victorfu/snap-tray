#ifndef WASAPIAUDIOCAPTUREENGINE_H
#define WASAPIAUDIOCAPTUREENGINE_H

#include "IAudioCaptureEngine.h"

#ifdef Q_OS_WIN

#include <QThread>
#include <QMutex>
#include <atomic>

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
    void pause() override;
    void resume() override;
    bool isRunning() const override { return m_running; }
    bool isPaused() const override { return m_paused; }

    QString engineName() const override { return QStringLiteral("WASAPI"); }
    bool isAvailable() const override;
    bool isSystemAudioSupported() const override { return true; }

private:
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
        bool isFloat = false;
        int bitsPerSample = 16;
        int channels = 2;
        int sampleRate = 48000;
    };
    bool updateFormatFromWaveFormat(const void *wfx, NativeFormatInfo &nativeFormat, AudioFormat &outputFormat) const;
    bool probeFormat(bool forLoopback, const QString &deviceId,
                     NativeFormatInfo &nativeFormat, AudioFormat &outputFormat) const;
    void refreshProbedFormat();

    // Convert audio data from native format to 16-bit PCM
    // Uses unsigned char* to avoid Windows header dependency
    QByteArray convertToInt16PCM(const unsigned char *data, int numFrames,
                                 const NativeFormatInfo &nativeFormat) const;

    // Capture loop (runs in separate thread)
    void captureLoop();

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

    // Timing
    qint64 m_startTime = 0;
    qint64 m_pausedDuration = 0;
    qint64 m_pauseStartTime = 0;
    mutable QMutex m_timingMutex;

    // Native audio format info (for conversion and mixing)
    NativeFormatInfo m_micNativeFormat;
    NativeFormatInfo m_loopbackNativeFormat;
    AudioFormat m_micFormat;
    AudioFormat m_loopbackFormat;

    // Pending buffers for mixing mic + loopback
    QByteArray m_micPending;
    QByteArray m_loopbackPending;
};

#endif // Q_OS_WIN

#endif // WASAPIAUDIOCAPTUREENGINE_H
