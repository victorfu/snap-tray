#ifndef RECORDINGINITTASK_H
#define RECORDINGINITTASK_H

#include <QObject>
#include <QRect>
#include <QSize>
#include <QString>
#include <QAtomicInt>
#include <QList>
#include <qwindowdefs.h>
#include <memory>

#include "encoding/EncoderFactory.h"

class QScreen;
class ICaptureEngine;
class IVideoEncoder;
class NativeGifEncoder;
class WebPAnimationEncoder;
class IAudioCaptureEngine;

/**
 * @brief Handles async initialization of recording components
 *
 * This class manages the heavy initialization work that would otherwise
 * block the UI thread when starting a recording. It creates and configures:
 * - Capture engine (DXGI/ScreenCaptureKit/Qt)
 * - Video encoder (native or FFmpeg)
 * - Audio capture engine (if enabled)
 */
class RecordingInitTask : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Configuration for initialization
     */
    struct Config {
        QRect region;
        QScreen *screen = nullptr;
        int frameRate = 30;
        bool audioEnabled = false;
        int audioSource = 0;  // 0=Microphone, 1=SystemAudio, 2=Both
        QString audioDevice;
        // Audio format for encoder configuration (must match captured PCM format).
        int audioSampleRate = 48000;
        int audioChannels = 2;
        int audioBitsPerSample = 16;
        QString outputPath;
        bool useNativeEncoder = true;
        EncoderFactory::Format outputFormat = EncoderFactory::Format::MP4;
        QSize frameSize;
        int quality = 55;
        QList<WId> excludedWindowIds;  // Windows to exclude from capture (e.g., recording UI)
    };

    /**
     * @brief Result of initialization
     */
    struct Result {
        bool success = false;
        QString error;

        // Created components (ownership transfers to caller on success)
        std::unique_ptr<ICaptureEngine> captureEngine;
        std::unique_ptr<IVideoEncoder> nativeEncoder;
        std::unique_ptr<NativeGifEncoder> gifEncoder;
        std::unique_ptr<WebPAnimationEncoder> webpEncoder;
        std::unique_ptr<IAudioCaptureEngine> audioEngine;

        bool usingNativeEncoder = false;
        QString tempAudioPath;

        // Track started state for safe cleanup
        bool captureEngineStarted = false;

        Result();
        ~Result();

        Result(Result&&) = default;
        Result& operator=(Result&&) = default;
        Result(const Result&) = delete;
        Result& operator=(const Result&) = delete;

        // Cleanup on failure
        void cleanup();

        // Transfer ownership of initialized resources
        ICaptureEngine* releaseCaptureEngine() { return captureEngine.release(); }
        IVideoEncoder* releaseNativeEncoder() { return nativeEncoder.release(); }
        NativeGifEncoder* releaseGifEncoder() { return gifEncoder.release(); }
        WebPAnimationEncoder* releaseWebpEncoder() { return webpEncoder.release(); }
        IAudioCaptureEngine* releaseAudioEngine() { return audioEngine.release(); }
    };

    explicit RecordingInitTask(const Config &config, QObject *parent = nullptr);
    ~RecordingInitTask();

    /**
     * @brief Run the initialization synchronously (call from worker thread)
     */
    void run();

    /**
     * @brief Get the result after run() completes
     */
    Result &result() { return m_result; }
    const Result &result() const { return m_result; }

    /**
     * @brief Cancel the initialization if in progress
     */
    void cancel();

    /**
     * @brief Discard any initialized resources when result is no longer needed
     */
    void discardResult();

    /**
     * @brief Check if initialization was cancelled
     */
    bool isCancelled() const { return m_cancelled.loadAcquire() != 0; }

signals:
    /**
     * @brief Emitted during initialization with progress updates
     */
    void progress(const QString &step);

    /**
     * @brief Emitted when initialization is complete
     */
    void finished();

private:
    bool initializeCaptureEngine();
    bool initializeAudioEngine();
    bool initializeEncoder();

    Config m_config;
    Result m_result;
    QAtomicInt m_cancelled;
};

#endif // RECORDINGINITTASK_H
