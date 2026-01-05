#ifndef RECORDINGINITTASK_H
#define RECORDINGINITTASK_H

#include <QObject>
#include <QRect>
#include <QSize>
#include <QString>
#include <QAtomicInt>
#include <QList>
#include <qwindowdefs.h>

class QScreen;
class ICaptureEngine;
class IVideoEncoder;
class NativeGifEncoder;
class IAudioCaptureEngine;
class AudioFileWriter;

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
        QString outputPath;
        bool useNativeEncoder = true;
        bool useGif = false;
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
        ICaptureEngine *captureEngine = nullptr;
        IVideoEncoder *nativeEncoder = nullptr;
        NativeGifEncoder *gifEncoder = nullptr;
        IAudioCaptureEngine *audioEngine = nullptr;
        AudioFileWriter *audioWriter = nullptr;

        bool usingNativeEncoder = false;
        QString tempAudioPath;

        // Track started state for safe cleanup
        bool captureEngineStarted = false;

        // Cleanup on failure
        void cleanup();
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
    const Result &result() const { return m_result; }

    /**
     * @brief Cancel the initialization if in progress
     */
    void cancel();

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
