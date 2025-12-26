#include "RecordingManager.h"
#include "RecordingRegionSelector.h"
#include "RecordingControlBar.h"
#include "RecordingBoundaryOverlay.h"
#include "FFmpegEncoder.h"
#include "IVideoEncoder.h"
#include "encoding/EncoderFactory.h"
#include "capture/ICaptureEngine.h"
#include "capture/IAudioCaptureEngine.h"
#include "AudioFileWriter.h"
#include "platform/WindowLevel.h"
#include "utils/ResourceCleanupHelper.h"
#include "utils/CoordinateHelper.h"

#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QRandomGenerator>
#include <QMutexLocker>
#include <QUuid>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#endif

// Delay before starting frame capture to allow overlay to render fully
// This prevents the border from flashing in the initial frames
static constexpr int OVERLAY_RENDER_DELAY_MS = 100;

static void syncFile(const QString &path)
{
#ifdef Q_OS_WIN
    HANDLE hFile = CreateFileW(
        reinterpret_cast<LPCWSTR>(path.utf16()),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (hFile != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(hFile);
        CloseHandle(hFile);
    }
#else
    int fd = open(path.toLocal8Bit().constData(), O_RDONLY);
    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }
#endif
}

RecordingManager::RecordingManager(QObject *parent)
    : QObject(parent)
    , m_encoder(nullptr)
    , m_nativeEncoder(nullptr)
    , m_usingNativeEncoder(false)
    , m_captureEngine(nullptr)
    , m_captureTimer(nullptr)
    , m_durationTimer(nullptr)
    , m_targetScreen(nullptr)
    , m_state(State::Idle)
    , m_frameRate(30)
    , m_frameCount(0)
    , m_pausedDuration(0)
    , m_pauseStartTime(0)
    , m_audioEngine(nullptr)
    , m_audioWriter(nullptr)
    , m_audioEnabled(false)
    , m_audioSource(0)
{
    cleanupStaleTempFiles();
}

RecordingManager::~RecordingManager()
{
    cleanupRecording();
}

void RecordingManager::setState(State newState)
{
    if (m_state != newState) {
        m_state = newState;
        emit stateChanged(m_state);
    }
}

bool RecordingManager::isActive() const
{
    return isSelectingRegion() || isRecording() || isPaused() || (m_state == State::Encoding);
}

bool RecordingManager::isRecording() const
{
    return m_state == State::Recording;
}

bool RecordingManager::isSelectingRegion() const
{
    return m_regionSelector && m_regionSelector->isVisible();
}

bool RecordingManager::isPaused() const
{
    return m_state == State::Paused;
}

void RecordingManager::startRegionSelection()
{
    if (m_regionSelector && m_regionSelector->isVisible()) {
        qDebug() << "RecordingManager: Already selecting region";
        return;
    }

    if (m_state == State::Recording || m_state == State::Paused || m_state == State::Encoding) {
        qDebug() << "RecordingManager: Already recording or encoding";
        return;
    }

    // Clean up any existing selector
    if (m_regionSelector) {
        m_regionSelector->close();
    }

    // Determine target screen (cursor location)
    QScreen *targetScreen = QGuiApplication::screenAt(QCursor::pos());
    if (!targetScreen) {
        targetScreen = QGuiApplication::primaryScreen();
    }

    qDebug() << "RecordingManager: Starting region selection on screen:" << targetScreen->name();

    setState(State::Selecting);

    m_regionSelector = new RecordingRegionSelector();
    m_regionSelector->setAttribute(Qt::WA_DeleteOnClose);

    connect(m_regionSelector, &RecordingRegionSelector::regionSelected,
            this, &RecordingManager::onRegionSelected);
    connect(m_regionSelector, &RecordingRegionSelector::cancelledWithRegion,
            this, &RecordingManager::onRegionCancelledWithRegion);
    connect(m_regionSelector, &RecordingRegionSelector::cancelled,
            this, &RecordingManager::onRegionCancelled);

    qDebug() << "=== RecordingManager: Setting up region selector ===";
    qDebug() << "Target screen geometry:" << targetScreen->geometry();
    m_regionSelector->setGeometry(targetScreen->geometry());
    qDebug() << "Widget geometry after setGeometry:" << m_regionSelector->geometry();
    m_regionSelector->initializeForScreen(targetScreen);
    m_regionSelector->show();
    qDebug() << "Widget geometry after show:" << m_regionSelector->geometry();
    raiseWindowAboveMenuBar(m_regionSelector);
    m_regionSelector->activateWindow();
    m_regionSelector->raise();
}

void RecordingManager::startRegionSelectionWithPreset(const QRect &region, QScreen *screen)
{
    if (m_regionSelector && m_regionSelector->isVisible()) {
        qDebug() << "RecordingManager: Already selecting region";
        return;
    }

    if (m_state == State::Recording || m_state == State::Paused || m_state == State::Encoding) {
        qDebug() << "RecordingManager: Already recording or encoding";
        return;
    }

    // Clean up any existing selector
    if (m_regionSelector) {
        m_regionSelector->close();
    }

    qDebug() << "RecordingManager: Starting region selection with preset region:" << region
             << "on screen:" << screen->name();

    setState(State::Selecting);

    m_regionSelector = new RecordingRegionSelector();
    m_regionSelector->setAttribute(Qt::WA_DeleteOnClose);

    connect(m_regionSelector, &RecordingRegionSelector::regionSelected,
            this, &RecordingManager::onRegionSelected);
    connect(m_regionSelector, &RecordingRegionSelector::cancelledWithRegion,
            this, &RecordingManager::onRegionCancelledWithRegion);
    connect(m_regionSelector, &RecordingRegionSelector::cancelled,
            this, &RecordingManager::onRegionCancelled);

    m_regionSelector->setGeometry(screen->geometry());
    m_regionSelector->initializeWithRegion(screen, region);
    m_regionSelector->show();
    raiseWindowAboveMenuBar(m_regionSelector);
    m_regionSelector->activateWindow();
    m_regionSelector->raise();
}

void RecordingManager::startFullScreenRecording()
{
    // Check if already active
    if (m_state == State::Recording || m_state == State::Paused || m_state == State::Encoding) {
        qDebug() << "RecordingManager: Already recording or encoding";
        return;
    }

    if (m_regionSelector && m_regionSelector->isVisible()) {
        qDebug() << "RecordingManager: Already selecting region";
        return;
    }

    // Determine target screen based on cursor position
    QScreen *targetScreen = QGuiApplication::screenAt(QCursor::pos());
    if (!targetScreen) {
        targetScreen = QGuiApplication::primaryScreen();
    }

    qDebug() << "RecordingManager: Starting full-screen recording on screen:" << targetScreen->name();

    // Use the entire screen as the recording region
    m_recordingRegion = targetScreen->geometry();
    m_targetScreen = targetScreen;

    // Load frame rate from settings with validation
    QSettings settings("Victor Fu", "SnapTray");
    m_frameRate = settings.value("recording/framerate", 30).toInt();
    if (m_frameRate <= 0 || m_frameRate > 120) {
        qWarning() << "RecordingManager: Invalid frame rate" << m_frameRate << ", using default 30";
        m_frameRate = 30;
    }

    // Reset pause tracking
    m_pausedDuration = 0;
    m_pauseStartTime = 0;

    // Skip region selection and start recording directly
    startFrameCapture();
}

void RecordingManager::onRegionSelected(const QRect &region, QScreen *screen)
{
    qDebug() << "RecordingManager: Region selected:" << region << "on screen:" << screen->name();

    m_recordingRegion = region;
    m_targetScreen = screen;

    // Load frame rate from settings with validation
    QSettings settings("Victor Fu", "SnapTray");
    m_frameRate = settings.value("recording/framerate", 30).toInt();
    if (m_frameRate <= 0 || m_frameRate > 120) {
        qWarning() << "RecordingManager: Invalid frame rate" << m_frameRate << ", using default 30";
        m_frameRate = 30;
    }

    // Reset pause tracking
    m_pausedDuration = 0;
    m_pauseStartTime = 0;

    // Start recording immediately after selection
    startFrameCapture();
}

void RecordingManager::onRegionCancelledWithRegion(const QRect &region, QScreen *screen)
{
    qDebug() << "RecordingManager: Region selection cancelled, returning to capture";
    setState(State::Idle);
    emit selectionCancelledWithRegion(region, screen);
}

void RecordingManager::onRegionCancelled()
{
    qDebug() << "RecordingManager: Region selection cancelled without valid region";
    setState(State::Idle);
    // No signal emitted - just return to idle state
}

void RecordingManager::startFrameCapture()
{
    qDebug() << "RecordingManager::startFrameCapture() - BEGIN";

    // Initialize capture engine (auto-selects best available)
    qDebug() << "RecordingManager::startFrameCapture() - Creating capture engine...";
    m_captureEngine = ICaptureEngine::createBestEngine(nullptr);
    qDebug() << "RecordingManager::startFrameCapture() - Capture engine created:" << (m_captureEngine ? "valid" : "NULL");

    // Forward capture engine warnings to UI
    connect(m_captureEngine, &ICaptureEngine::warning,
            this, &RecordingManager::recordingWarning);

    // Connect capture engine error signal
    connect(m_captureEngine, &ICaptureEngine::error,
            this, [this](const QString &msg) {
        qDebug() << "RecordingManager: Capture engine error:" << msg;
        stopRecording();
        emit recordingError(msg);
    });

    qDebug() << "RecordingManager::startFrameCapture() - Setting region:" << m_recordingRegion
             << "screen:" << (m_targetScreen ? m_targetScreen->name() : "NULL");
    if (!m_captureEngine->setRegion(m_recordingRegion, m_targetScreen)) {
        emit recordingError("Failed to configure capture region");
        disconnect(m_captureEngine, nullptr, this, nullptr);
        m_captureEngine->deleteLater();
        m_captureEngine = nullptr;
        setState(State::Idle);
        return;
    }
    qDebug() << "RecordingManager::startFrameCapture() - Region set successfully";

    qDebug() << "RecordingManager::startFrameCapture() - Setting frame rate:" << m_frameRate;
    m_captureEngine->setFrameRate(m_frameRate);

    qDebug() << "RecordingManager::startFrameCapture() - Starting capture engine...";
    if (!m_captureEngine->start()) {
        emit recordingError("Failed to start capture engine");
        disconnect(m_captureEngine, nullptr, this, nullptr);
        m_captureEngine->deleteLater();
        m_captureEngine = nullptr;
        setState(State::Idle);
        return;
    }
    qDebug() << "RecordingManager::startFrameCapture() - Capture engine started successfully";

    qDebug() << "RecordingManager: Using capture engine:" << m_captureEngine->engineName();

    // Safety check: clean up any existing encoders
    if (m_encoder) {
        qWarning() << "RecordingManager: Previous FFmpeg encoder still exists, cleaning up";
        disconnect(m_encoder, nullptr, this, nullptr);
        m_encoder->deleteLater();
        m_encoder = nullptr;
    }
    if (m_nativeEncoder) {
        qWarning() << "RecordingManager: Previous native encoder still exists, cleaning up";
        disconnect(m_nativeEncoder, nullptr, this, nullptr);
        m_nativeEncoder->deleteLater();
        m_nativeEncoder = nullptr;
    }
    m_usingNativeEncoder = false;

    QString outputPath = generateOutputPath();
    qDebug() << "RecordingManager::startFrameCapture() - Output path:" << outputPath;

    // Get output format from settings (0 = MP4, 1 = GIF)
    QSettings settings("Victor Fu", "SnapTray");
    int formatInt = settings.value("recording/outputFormat", 0).toInt();
    bool useGif = (formatInt == 1);
    qDebug() << "RecordingManager::startFrameCapture() - Output format:" << (useGif ? "GIF" : "MP4");

    // ========== Initialize audio capture EARLY (before encoder) ==========
    // This must happen BEFORE starting the encoder so we can configure audio format
    m_audioEnabled = settings.value("recording/audioEnabled", false).toBool();
    m_audioSource = settings.value("recording/audioSource", 0).toInt();
    m_audioDevice = settings.value("recording/audioDevice").toString();

    // Only enable audio for MP4 format (GIF doesn't support audio)
    if (m_audioEnabled && !useGif) {
        qDebug() << "RecordingManager: Audio enabled, source:" << m_audioSource << "- initializing early";

        // Create audio capture engine
        m_audioEngine = IAudioCaptureEngine::createBestEngine(nullptr);
        if (m_audioEngine) {
            // Set audio source (0=Microphone, 1=SystemAudio, 2=Both)
            IAudioCaptureEngine::AudioSource source;
            switch (m_audioSource) {
                case 1: source = IAudioCaptureEngine::AudioSource::SystemAudio; break;
                case 2: source = IAudioCaptureEngine::AudioSource::Both; break;
                default: source = IAudioCaptureEngine::AudioSource::Microphone; break;
            }
            m_audioEngine->setAudioSource(source);

            if (!m_audioDevice.isEmpty()) {
                m_audioEngine->setDevice(m_audioDevice);
            }

            qDebug() << "RecordingManager: Audio engine created:" << m_audioEngine->engineName();
        } else {
            qWarning() << "RecordingManager: No audio capture engine available";
            m_audioEnabled = false;
        }
    }

    // Use physical pixel size for Retina/HiDPI displays
    qreal scale = CoordinateHelper::getDevicePixelRatio(m_targetScreen);
    QSize physicalSize = CoordinateHelper::toEvenPhysicalSize(m_recordingRegion.size(), scale);
    qDebug() << "RecordingManager::startFrameCapture() - Physical size:" << physicalSize
             << "(scale:" << scale << ", logical:" << m_recordingRegion.size() << ")";

    // Configure encoder using EncoderFactory
    EncoderFactory::EncoderConfig encoderConfig;
    encoderConfig.format = useGif ? EncoderFactory::Format::GIF : EncoderFactory::Format::MP4;
    encoderConfig.priority = EncoderFactory::Priority::NativeFirst;
    encoderConfig.frameSize = physicalSize;
    encoderConfig.frameRate = m_frameRate;
    encoderConfig.outputPath = outputPath;
    encoderConfig.quality = settings.value("recording/quality", 55).toInt();
    encoderConfig.preset = settings.value("recording/preset", "ultrafast").toString();
    encoderConfig.crf = settings.value("recording/crf", 23).toInt();

    // Configure audio settings for native encoder
    if (m_audioEngine) {
        auto format = m_audioEngine->audioFormat();
        encoderConfig.enableAudio = true;
        encoderConfig.audioSampleRate = format.sampleRate;
        encoderConfig.audioChannels = format.channels;
        encoderConfig.audioBitsPerSample = format.bitsPerSample;
    }

    // Create encoder
    auto encoderResult = EncoderFactory::create(encoderConfig, this);

    if (!encoderResult.success) {
        emit recordingError(encoderResult.errorMessage);
        if (m_audioEngine) {
            m_audioEngine->deleteLater();
            m_audioEngine = nullptr;
        }
        m_captureEngine->stop();
        disconnect(m_captureEngine, nullptr, this, nullptr);
        m_captureEngine->deleteLater();
        m_captureEngine = nullptr;
        setState(State::Idle);
        return;
    }

    // Store encoder references
    m_usingNativeEncoder = encoderResult.isNative;
    m_nativeEncoder = encoderResult.nativeEncoder;
    m_encoder = encoderResult.ffmpegEncoder;

    // Connect encoder signals
    if (m_nativeEncoder) {
        connect(m_nativeEncoder, &IVideoEncoder::finished,
                this, &RecordingManager::onEncodingFinished);
        connect(m_nativeEncoder, &IVideoEncoder::error,
                this, &RecordingManager::onEncodingError);
    }
    if (m_encoder) {
        connect(m_encoder, &FFmpegEncoder::finished,
                this, &RecordingManager::onEncodingFinished);
        connect(m_encoder, &FFmpegEncoder::error,
                this, &RecordingManager::onEncodingError);
    }

    qDebug() << "RecordingManager::startFrameCapture() - Encoder started successfully";

    // ========== Connect and start audio capture ==========
    // Audio engine was already created earlier (before encoder start) to configure audio format
    if (m_audioEngine) {
        // Check if native encoder supports audio (preferred path)
        bool useNativeAudio = m_usingNativeEncoder && m_nativeEncoder && m_nativeEncoder->isAudioSupported();

        if (useNativeAudio) {
            // Connect audio data directly to native encoder
            connect(m_audioEngine, &IAudioCaptureEngine::audioDataReady,
                    this, [this](const QByteArray &data, qint64 timestamp) {
                if (m_nativeEncoder) {
                    m_nativeEncoder->writeAudioSamples(data, timestamp);
                }
            }, Qt::QueuedConnection);
            qDebug() << "RecordingManager: Audio connected to native encoder";
        } else {
            // Fallback: Use WAV file + FFmpeg muxing
            QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
            QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
            QString uuid = QUuid::createUuid().toString(QUuid::Id128).left(8);
            m_tempAudioPath = QString("%1/SnapTray_Audio_%2_%3.wav")
                .arg(tempDir).arg(timestamp).arg(uuid);

            // Create audio file writer
            m_audioWriter = new AudioFileWriter(nullptr);
            AudioFileWriter::AudioFormat audioFormat;
            audioFormat.sampleRate = m_audioEngine->audioFormat().sampleRate;
            audioFormat.channels = m_audioEngine->audioFormat().channels;
            audioFormat.bitsPerSample = m_audioEngine->audioFormat().bitsPerSample;

            if (m_audioWriter->start(m_tempAudioPath, audioFormat)) {
                // Connect audio data signal to writer
                connect(m_audioEngine, &IAudioCaptureEngine::audioDataReady,
                        this, [this](const QByteArray &data, qint64 /*timestamp*/) {
                    if (m_audioWriter) {
                        m_audioWriter->writeAudioData(data);
                    }
                }, Qt::QueuedConnection);

                // Set audio file path on FFmpeg encoder for muxing
                if (m_encoder) {
                    m_encoder->setAudioFilePath(m_tempAudioPath);
                }
                qDebug() << "RecordingManager: Using WAV file for audio (FFmpeg muxing)";
            } else {
                qWarning() << "RecordingManager: Failed to create audio file";
                emit recordingWarning("Failed to create audio file. Recording without audio.");
                cleanupAudio();
            }
        }

        // Connect audio error/warning signals
        connect(m_audioEngine, &IAudioCaptureEngine::error,
                this, [this](const QString &msg) {
            qWarning() << "RecordingManager: Audio error:" << msg;
            emit recordingWarning("Audio error: " + msg);
        });
        connect(m_audioEngine, &IAudioCaptureEngine::warning,
                this, &RecordingManager::recordingWarning);

        // Start audio capture
        if (m_audioEngine->start()) {
            qDebug() << "RecordingManager: Audio capture started using" << m_audioEngine->engineName();
        } else {
            qWarning() << "RecordingManager: Failed to start audio capture";
            emit recordingWarning("Failed to start audio capture. Recording without audio.");
            cleanupAudio();
        }
    }

    // Show boundary overlay
    qDebug() << "RecordingManager::startFrameCapture() - Creating boundary overlay...";
    m_boundaryOverlay = new RecordingBoundaryOverlay();
    m_boundaryOverlay->setAttribute(Qt::WA_DeleteOnClose);
    m_boundaryOverlay->setRegion(m_recordingRegion);
    m_boundaryOverlay->show();
    raiseWindowAboveMenuBar(m_boundaryOverlay);
    qDebug() << "RecordingManager::startFrameCapture() - Boundary overlay shown";

    // Show control bar
    qDebug() << "RecordingManager::startFrameCapture() - Creating control bar...";
    m_controlBar = new RecordingControlBar();
    m_controlBar->setAttribute(Qt::WA_DeleteOnClose);
    connect(m_controlBar, &RecordingControlBar::stopRequested,
            this, &RecordingManager::stopRecording);
    connect(m_controlBar, &RecordingControlBar::cancelRequested,
            this, &RecordingManager::cancelRecording);
    connect(m_controlBar, &RecordingControlBar::pauseRequested,
            this, &RecordingManager::pauseRecording);
    connect(m_controlBar, &RecordingControlBar::resumeRequested,
            this, &RecordingManager::resumeRecording);

    // Set initial region size
    m_controlBar->updateRegionSize(m_recordingRegion.width(), m_recordingRegion.height());
    m_controlBar->updateFps(m_frameRate);

    // Show audio indicator if audio is enabled
    m_controlBar->setAudioEnabled(m_audioEngine != nullptr);

    m_controlBar->positionNear(m_recordingRegion);
    m_controlBar->show();
    raiseWindowAboveMenuBar(m_controlBar);
    qDebug() << "RecordingManager::startFrameCapture() - Control bar shown";

    // Initialize state and counters before starting timers
    m_elapsedTimer.start();
    setState(State::Recording);
    m_frameCount = 0;

    // Delay frame capture start to allow boundary overlay to render fully
    qDebug() << "RecordingManager::startFrameCapture() - Delaying capture start for overlay rendering...";
    QTimer::singleShot(OVERLAY_RENDER_DELAY_MS, this, [this]() {
        // Safety check: ensure we're still in recording state
        // Object could have been cancelled or destroyed during the delay
        if (m_state != State::Recording) {
            qDebug() << "RecordingManager: Capture start cancelled, state changed during delay";
            return;
        }

        // Safety check: ensure timers don't already exist (shouldn't happen, but be defensive)
        if (m_captureTimer) {
            qWarning() << "RecordingManager: Capture timer already exists, cleaning up";
            m_captureTimer->stop();
            delete m_captureTimer;
            m_captureTimer = nullptr;
        }
        if (m_durationTimer) {
            qWarning() << "RecordingManager: Duration timer already exists, cleaning up";
            m_durationTimer->stop();
            delete m_durationTimer;
            m_durationTimer = nullptr;
        }

        // Start frame capture timer
        qDebug() << "RecordingManager::startFrameCapture() - Starting capture timer...";
        m_captureTimer = new QTimer(this);
        connect(m_captureTimer, &QTimer::timeout, this, &RecordingManager::captureFrame);
        m_captureTimer->start(1000 / m_frameRate);
        qDebug() << "RecordingManager::startFrameCapture() - Capture timer started";

        // Start duration timer (update UI every 100ms)
        qDebug() << "RecordingManager::startFrameCapture() - Starting duration timer...";
        m_durationTimer = new QTimer(this);
        connect(m_durationTimer, &QTimer::timeout, this, &RecordingManager::updateDuration);
        m_durationTimer->start(100);
        qDebug() << "RecordingManager::startFrameCapture() - Duration timer started";
    });

    qDebug() << "RecordingManager: Recording started at" << m_frameRate << "FPS";
    qDebug() << "RecordingManager::startFrameCapture() - END (success)";
    emit recordingStarted();
}

void RecordingManager::captureFrame()
{
    if (m_state != State::Recording) {
        return;
    }

    // Store local copy to avoid race condition where pointer becomes null
    // between the check and actual use
    ICaptureEngine *captureEngine = m_captureEngine;

    if (!captureEngine) {
        return;
    }

    // Check that we have an encoder available
    if (!m_usingNativeEncoder && !m_encoder) {
        return;
    }
    if (m_usingNativeEncoder && !m_nativeEncoder) {
        return;
    }

    // Capture frame using the capture engine
    QImage frame = captureEngine->captureFrame();

    if (!frame.isNull()) {
        // Use real elapsed time for timestamps to keep playback speed aligned with recording time
        qint64 elapsedMs = 0;
        {
            QMutexLocker locker(&m_durationMutex);
            qint64 rawElapsed = m_elapsedTimer.elapsed();
            elapsedMs = rawElapsed - m_pausedDuration;
        }
        if (elapsedMs < 0) {
            elapsedMs = 0;
        }

        // Pass QImage to the appropriate encoder with elapsed timestamp
        if (m_usingNativeEncoder && m_nativeEncoder) {
            m_nativeEncoder->writeFrame(frame, elapsedMs);
        } else if (m_encoder) {
            m_encoder->writeFrame(frame);  // FFmpeg encoder uses frame count internally
        }
        m_frameCount++;
    }
}

void RecordingManager::updateDuration()
{
    if (m_state == State::Recording || m_state == State::Paused) {
        qint64 effectiveElapsed;
        {
            QMutexLocker locker(&m_durationMutex);
            qint64 rawElapsed = m_elapsedTimer.elapsed();

            // Calculate current pause duration if paused
            qint64 currentPause = (m_state == State::Paused)
                ? (rawElapsed - m_pauseStartTime) : 0;

            // Effective elapsed = raw - total paused - current pause
            effectiveElapsed = rawElapsed - m_pausedDuration - currentPause;
        }

        emit durationChanged(effectiveElapsed);

        if (m_controlBar) {
            m_controlBar->updateDuration(effectiveElapsed);
        }
    }
}

void RecordingManager::pauseRecording()
{
    if (m_state != State::Recording) {
        return;
    }

    qDebug() << "RecordingManager: Pausing recording";

    // Stop frame capture timer
    if (m_captureTimer) {
        m_captureTimer->stop();
    }

    // Pause audio capture
    if (m_audioEngine) {
        m_audioEngine->pause();
    }

    // Record when pause started (protected by mutex)
    {
        QMutexLocker locker(&m_durationMutex);
        m_pauseStartTime = m_elapsedTimer.elapsed();
    }

    setState(State::Paused);

    if (m_controlBar) {
        m_controlBar->setPaused(true);
    }

    emit recordingPaused();
}

void RecordingManager::resumeRecording()
{
    if (m_state != State::Paused) {
        return;
    }

    qDebug() << "RecordingManager: Resuming recording";

    // Add pause duration to total (protected by mutex)
    {
        QMutexLocker locker(&m_durationMutex);
        m_pausedDuration += m_elapsedTimer.elapsed() - m_pauseStartTime;
    }

    // Resume audio capture
    if (m_audioEngine) {
        m_audioEngine->resume();
    }

    // Resume frame capture timer
    if (m_captureTimer) {
        m_captureTimer->start(1000 / m_frameRate);
    }

    setState(State::Recording);

    if (m_controlBar) {
        m_controlBar->setPaused(false);
    }

    emit recordingResumed();
}

void RecordingManager::togglePause()
{
    if (m_state == State::Recording) {
        pauseRecording();
    } else if (m_state == State::Paused) {
        resumeRecording();
    }
}

void RecordingManager::stopRecording()
{
    if (m_state != State::Recording && m_state != State::Paused) {
        return;
    }

    qDebug() << "RecordingManager: Stopping recording, frames captured:" << m_frameCount;

    stopFrameCapture();
    setState(State::Encoding);

    // Finish encoding
    if (m_usingNativeEncoder && m_nativeEncoder) {
        m_nativeEncoder->finish();
    } else if (m_encoder) {
        m_encoder->finish();
    }
}

void RecordingManager::cancelRecording()
{
    if (m_state != State::Recording && m_state != State::Paused && m_state != State::Encoding) {
        return;
    }

    qDebug() << "RecordingManager: Cancelling recording (state:" << static_cast<int>(m_state) << ")";

    stopFrameCapture();

    // Abort encoding and remove output file
    // Use deleteLater to avoid use-after-free when called from signal handler
    if (m_nativeEncoder) {
        m_nativeEncoder->abort();
        m_nativeEncoder->deleteLater();
        m_nativeEncoder = nullptr;
    }
    if (m_encoder) {
        m_encoder->abort();
        m_encoder->deleteLater();
        m_encoder = nullptr;
    }
    m_usingNativeEncoder = false;

    setState(State::Idle);
    emit recordingCancelled();
}

void RecordingManager::stopFrameCapture()
{
    qDebug() << "RecordingManager::stopFrameCapture() START";

    // Stop timers
    qDebug() << "RecordingManager: Stopping timers...";
    if (m_captureTimer) {
        m_captureTimer->stop();
        delete m_captureTimer;
        m_captureTimer = nullptr;
    }

    if (m_durationTimer) {
        m_durationTimer->stop();
        delete m_durationTimer;
        m_durationTimer = nullptr;
    }
    qDebug() << "RecordingManager: Timers stopped";

    // Stop audio capture and finalize audio file
    // Disconnect BEFORE stopping to prevent new signals from being queued during shutdown
    qDebug() << "RecordingManager: Stopping audio engine, m_audioEngine=" << (m_audioEngine ? "exists" : "null");
    if (m_audioEngine) {
        ResourceCleanupHelper::stopAndDelete(m_audioEngine);
        qDebug() << "RecordingManager: Audio engine stopped and scheduled for deletion";
    }

    qDebug() << "RecordingManager: Finishing audio writer...";
    if (m_audioWriter) {
        m_audioWriter->finish();
        qDebug() << "RecordingManager: Audio file written:" << m_tempAudioPath
                 << "Duration:" << m_audioWriter->durationMs() << "ms";
        delete m_audioWriter;
        m_audioWriter = nullptr;
    }
    qDebug() << "RecordingManager: Audio writer finished";

    // Stop capture engine
    // Use deleteLater to avoid use-after-free when called from signal handler
    qDebug() << "RecordingManager: Stopping capture engine...";
    ResourceCleanupHelper::stopAndDelete(m_captureEngine);
    qDebug() << "RecordingManager: Capture engine stopped";

    // Close UI overlays
    qDebug() << "RecordingManager: Closing UI overlays...";
    if (m_boundaryOverlay) {
        m_boundaryOverlay->close();
    }

    if (m_controlBar) {
        m_controlBar->close();
    }
    qDebug() << "RecordingManager::stopFrameCapture() END";
}

void RecordingManager::cleanupRecording()
{
    stopFrameCapture();

    if (m_nativeEncoder) {
        m_nativeEncoder->abort();
        delete m_nativeEncoder;
        m_nativeEncoder = nullptr;
    }
    if (m_encoder) {
        m_encoder->abort();
        delete m_encoder;
        m_encoder = nullptr;
    }
    m_usingNativeEncoder = false;

    // Clean up temp audio file
    ResourceCleanupHelper::removeTempFile(m_tempAudioPath);

    if (m_regionSelector) {
        m_regionSelector->close();
    }
}

void RecordingManager::cleanupAudio()
{
    ResourceCleanupHelper::stopAndDelete(m_audioEngine);
    delete m_audioWriter;
    m_audioWriter = nullptr;
    ResourceCleanupHelper::removeTempFile(m_tempAudioPath);
}

void RecordingManager::onEncodingFinished(bool success, const QString &outputPath)
{
    qDebug() << "RecordingManager: Encoding finished, success:" << success << "path:" << outputPath;

    // Get error message before deleting encoder
    QString errorMsg;
    if (!success) {
        if (m_usingNativeEncoder && m_nativeEncoder) {
            errorMsg = m_nativeEncoder->lastError();
        } else if (m_encoder) {
            errorMsg = m_encoder->lastError();
        }
        if (errorMsg.isEmpty()) {
            errorMsg = "Failed to encode video";
        }
    }

    // Use deleteLater to avoid deleting during signal emission
    if (m_nativeEncoder) {
        m_nativeEncoder->deleteLater();
        m_nativeEncoder = nullptr;
    }
    if (m_encoder) {
        m_encoder->deleteLater();
        m_encoder = nullptr;
    }
    m_usingNativeEncoder = false;

    setState(State::Idle);

    if (success) {
        // Defer the save dialog to avoid issues during signal emission
        // Store the path and process it after the current event
        QString tempPath = outputPath;
        QMetaObject::invokeMethod(this, [this, tempPath]() {
            showSaveDialog(tempPath);
        }, Qt::QueuedConnection);
    } else {
        emit recordingError(errorMsg);
    }
}

void RecordingManager::showSaveDialog(const QString &tempOutputPath)
{
    QSettings settings("Victor Fu", "SnapTray");

    // Check auto-save setting
    bool autoSave = settings.value("recording/autoSave", false).toBool();
    QString outputDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);

    // Get extension from current file
    QFileInfo fileInfo(tempOutputPath);
    QString extension = fileInfo.suffix();

    if (autoSave) {
        // Auto-save: generate filename and save directly without dialog
        // Use atomic rename approach to avoid TOCTOU race condition
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
        QString baseName = QString("SnapTray_Recording_%1").arg(timestamp);
        QString fileName = QString("%1.%2").arg(baseName).arg(extension);
        QString savePath = QDir(outputDir).filePath(fileName);

        // Try rename directly - if it fails due to existing file, add counter
        int counter = 0;
        const int maxAttempts = 100;
        bool renamed = false;

        while (!renamed && counter < maxAttempts) {
            if (QFile::rename(tempOutputPath, savePath)) {
                renamed = true;
            } else if (QFile::exists(savePath)) {
                // File exists, try with counter
                counter++;
                fileName = QString("%1_%2.%3").arg(baseName).arg(counter).arg(extension);
                savePath = QDir(outputDir).filePath(fileName);
            } else {
                // Rename failed for other reason, break and try copy
                break;
            }
        }

        if (renamed) {
            syncFile(savePath);
            qDebug() << "RecordingManager: Auto-saved recording to:" << savePath;
            emit recordingStopped(savePath);
            return;
        } else {
            // Try copy + delete if rename fails (cross-filesystem)
            if (QFile::copy(tempOutputPath, savePath)) {
                QFile::remove(tempOutputPath);
                syncFile(savePath);
                qDebug() << "RecordingManager: Auto-saved (copied) recording to:" << savePath;
                emit recordingStopped(savePath);
                return;
            }
            // If auto-save fails, fall through to show dialog
            qWarning() << "RecordingManager: Auto-save failed, showing save dialog";
        }
    }

    // Show save dialog
    QString filter = (extension == "gif") ? "GIF Files (*.gif)" : "MP4 Files (*.mp4)";
    QString defaultFileName = QDir(outputDir).filePath(fileInfo.fileName());

    QString savePath = QFileDialog::getSaveFileName(
        nullptr,
        "Save Recording",
        defaultFileName,
        filter
    );

    if (savePath.isEmpty()) {
        // User cancelled - delete the temp file
        QFile::remove(tempOutputPath);
        emit recordingCancelled();
    } else {
        // Ensure correct extension
        if (!savePath.endsWith("." + extension, Qt::CaseInsensitive)) {
            savePath += "." + extension;
        }

        // Move/rename the file to chosen location
        if (savePath != tempOutputPath) {
            // Remove existing file if it exists
            if (QFile::exists(savePath)) {
                QFile::remove(savePath);
            }
            if (QFile::rename(tempOutputPath, savePath)) {
                syncFile(savePath);
                qDebug() << "RecordingManager: Saved recording to:" << savePath;
                emit recordingStopped(savePath);
            } else {
                // Try copy + delete if rename fails (cross-filesystem)
                if (QFile::copy(tempOutputPath, savePath)) {
                    QFile::remove(tempOutputPath);
                    syncFile(savePath);
                    qDebug() << "RecordingManager: Copied recording to:" << savePath;
                    emit recordingStopped(savePath);
                } else {
                    qWarning() << "RecordingManager: Failed to save recording to:" << savePath;
                    emit recordingError("Failed to save recording to selected location");
                }
            }
        } else {
            syncFile(tempOutputPath);
            emit recordingStopped(tempOutputPath);
        }
    }
}

void RecordingManager::onEncodingError(const QString &error)
{
    qDebug() << "RecordingManager: Encoding error:" << error;

    // Stop all capture-related resources (timers, capture engine, UI overlays)
    // This is needed when error occurs during recording (not just during finish/encoding)
    stopFrameCapture();

    // Get output path for cleanup before deleting encoder
    QString outputPath;
    if (m_usingNativeEncoder && m_nativeEncoder) {
        outputPath = m_nativeEncoder->outputPath();
        m_nativeEncoder->deleteLater();
        m_nativeEncoder = nullptr;
    }
    if (m_encoder) {
        if (outputPath.isEmpty()) {
            outputPath = m_encoder->outputPath();
        }
        m_encoder->deleteLater();
        m_encoder = nullptr;
    }
    m_usingNativeEncoder = false;

    // Clean up temp file on error
    if (!outputPath.isEmpty() && QFile::exists(outputPath)) {
        if (QFile::remove(outputPath)) {
            qDebug() << "RecordingManager: Removed temp file after error:" << outputPath;
        } else {
            qWarning() << "RecordingManager: Failed to remove temp file:" << outputPath;
        }
    }

    setState(State::Idle);
    emit recordingError(error);
}

QString RecordingManager::generateOutputPath() const
{
    // Use system temp directory for recording (file is moved to user-selected location after save dialog)
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);

    // Get output format from settings
    QSettings settings("Victor Fu", "SnapTray");
    int formatInt = settings.value("recording/outputFormat", 0).toInt();
    FFmpegEncoder::OutputFormat format = static_cast<FFmpegEncoder::OutputFormat>(formatInt);
    QString extension = (format == FFmpegEncoder::OutputFormat::GIF) ? "gif" : "mp4";

    // Generate filename with timestamp + UUID for guaranteed uniqueness
    // This prevents file collisions when a new recording starts while save dialog is open
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss-zzz");
    QString uuid = QUuid::createUuid().toString(QUuid::Id128).left(8);
    QString filename = QString("SnapTray_Recording_%1_%2.%3")
                           .arg(timestamp)
                           .arg(uuid)
                           .arg(extension);

    return QDir(tempDir).filePath(filename);
}

void RecordingManager::cleanupStaleTempFiles()
{
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QDir dir(tempDir);

    // Clean up SnapTray recording temp files older than 24 hours
    QStringList filters;
    filters << "SnapTray_Recording_*.mp4" << "SnapTray_Recording_*.gif";

    QFileInfoList files = dir.entryInfoList(filters, QDir::Files);
    QDateTime threshold = QDateTime::currentDateTime().addDays(-1);

    int cleanedCount = 0;
    for (const QFileInfo &file : files) {
        if (file.lastModified() < threshold) {
            if (QFile::remove(file.absoluteFilePath())) {
                qDebug() << "RecordingManager: Cleaned up stale temp file:" << file.fileName();
                cleanedCount++;
            } else {
                qWarning() << "RecordingManager: Failed to clean up temp file:" << file.fileName();
            }
        }
    }

    if (cleanedCount > 0) {
        qDebug() << "RecordingManager: Cleaned up" << cleanedCount << "stale temp file(s)";
    }
}
