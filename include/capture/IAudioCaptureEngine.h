#ifndef IAUDIOCAPTUREENGINE_H
#define IAUDIOCAPTUREENGINE_H

#include <QObject>
#include <QByteArray>
#include <QStringList>

/**
 * @brief Abstract interface for cross-platform audio capture
 *
 * This interface provides a unified API for capturing audio from
 * microphones and system audio on different platforms.
 */
class IAudioCaptureEngine : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Audio source options
     */
    enum class AudioSource {
        None = 0,           // No audio capture
        Microphone = 1,     // Capture from microphone/input device
        SystemAudio = 2,    // Capture system audio (loopback)
        Both = 3            // Mix microphone and system audio
    };
    Q_ENUM(AudioSource)

    /**
     * @brief Audio format specification
     */
    struct AudioFormat {
        int sampleRate = 48000;     // Sample rate in Hz (48kHz for AAC compatibility)
        int channels = 2;            // Number of channels (stereo)
        int bitsPerSample = 16;      // Bits per sample (16-bit PCM)

        int bytesPerSample() const { return bitsPerSample / 8; }
        int bytesPerFrame() const { return bytesPerSample() * channels; }
        int bytesPerSecond() const { return sampleRate * bytesPerFrame(); }
    };

    /**
     * @brief Audio device information
     */
    struct AudioDevice {
        QString id;          // Unique device identifier
        QString name;        // Human-readable device name
        bool isDefault;      // True if this is the default device
    };

    explicit IAudioCaptureEngine(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~IAudioCaptureEngine() = default;

    // ========== Configuration ==========

    /**
     * @brief Set the audio source to capture
     * @param source The audio source type
     * @return true if the source is supported and configured
     */
    virtual bool setAudioSource(AudioSource source) = 0;

    /**
     * @brief Get the current audio source
     */
    virtual AudioSource audioSource() const = 0;

    /**
     * @brief Set the audio input device for microphone capture
     * @param deviceId Device ID from availableInputDevices(), empty for default
     * @return true if the device was set successfully
     */
    virtual bool setDevice(const QString &deviceId) = 0;

    /**
     * @brief Get the current audio format
     */
    virtual AudioFormat audioFormat() const = 0;

    // ========== Device Enumeration ==========

    /**
     * @brief Get list of available input devices
     * @return List of audio device information
     */
    virtual QList<AudioDevice> availableInputDevices() const = 0;

    /**
     * @brief Get the default input device ID
     */
    virtual QString defaultInputDevice() const = 0;

    // ========== Capture Control ==========

    /**
     * @brief Start audio capture
     * @return true if capture started successfully
     */
    virtual bool start() = 0;

    /**
     * @brief Stop audio capture
     */
    virtual void stop() = 0;

    /**
     * @brief Pause audio capture (for sync with video pause)
     */
    virtual void pause() = 0;

    /**
     * @brief Resume audio capture after pause
     */
    virtual void resume() = 0;

    /**
     * @brief Check if audio capture is currently running
     */
    virtual bool isRunning() const = 0;

    /**
     * @brief Check if audio capture is paused
     */
    virtual bool isPaused() const = 0;

    // ========== Engine Information ==========

    /**
     * @brief Get the engine name (e.g., "WASAPI", "CoreAudio")
     */
    virtual QString engineName() const = 0;

    /**
     * @brief Check if this engine is available on the current system
     */
    virtual bool isAvailable() const = 0;

    /**
     * @brief Check if system audio capture is supported
     */
    virtual bool isSystemAudioSupported() const = 0;

    // ========== Factory Methods ==========

    /**
     * @brief Create the best available audio capture engine for the platform
     * @param parent Parent QObject
     * @return Pointer to audio capture engine, or nullptr if none available
     */
    static IAudioCaptureEngine* createBestEngine(QObject *parent = nullptr);

    /**
     * @brief Check if any native audio capture engine is available
     */
    static bool isNativeEngineAvailable();

signals:
    /**
     * @brief Emitted when audio data is available
     * @param data Raw PCM audio data
     * @param timestampMs Timestamp in milliseconds since capture start
     */
    void audioDataReady(const QByteArray &data, qint64 timestampMs);

    /**
     * @brief Emitted when an error occurs
     * @param message Error description
     */
    void error(const QString &message);

    /**
     * @brief Emitted for non-fatal warnings
     * @param message Warning description
     */
    void warning(const QString &message);

    /**
     * @brief Emitted when the audio device is disconnected
     */
    void deviceLost();

    /**
     * @brief Emitted with audio level for UI visualization
     * @param level Audio level from 0.0 to 1.0
     */
    void levelChanged(float level);

protected:
    AudioSource m_source = AudioSource::None;
    AudioFormat m_format;
    QString m_deviceId;
};

#endif // IAUDIOCAPTUREENGINE_H
