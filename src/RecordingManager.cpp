#include "RecordingManager.h"
#include "RecordingRegionSelector.h"
#include "RecordingControlBar.h"
#include "RecordingBoundaryOverlay.h"
#include "RecordingAnnotationOverlay.h"
#include "RecordingInitTask.h"
#include "encoding/NativeGifEncoder.h"
#include "IVideoEncoder.h"
#include "encoding/EncoderFactory.h"
#include "capture/ICaptureEngine.h"
#include "capture/IAudioCaptureEngine.h"
#include "AudioFileWriter.h"
#include "platform/WindowLevel.h"
#include "utils/ResourceCleanupHelper.h"
#include "utils/CoordinateHelper.h"
#include "settings/Settings.h"
#include "AnnotationController.h"

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
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>

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
    , m_gifEncoder(nullptr)
    , m_nativeEncoder(nullptr)
    , m_usingNativeEncoder(false)
    , m_captureEngine(nullptr)
    , m_annotationEnabled(false)
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
    , m_initTask(nullptr)
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
    return isSelectingRegion() || isRecording() || isPaused() || isPreviewing() ||
           (m_state == State::Preparing) || (m_state == State::Encoding);
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

bool RecordingManager::isPreviewing() const
{
    return m_state == State::Previewing;
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
    auto settings = SnapTray::getSettings();
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
    auto settings = SnapTray::getSettings();
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

    // Safety check: clean up any existing encoders
    if (m_gifEncoder) {
        qWarning() << "RecordingManager: Previous GIF encoder still exists, cleaning up";
        disconnect(m_gifEncoder, nullptr, this, nullptr);
        m_gifEncoder->deleteLater();
        m_gifEncoder = nullptr;
    }
    if (m_nativeEncoder) {
        qWarning() << "RecordingManager: Previous native encoder still exists, cleaning up";
        disconnect(m_nativeEncoder, nullptr, this, nullptr);
        m_nativeEncoder->deleteLater();
        m_nativeEncoder = nullptr;
    }
    m_usingNativeEncoder = false;

    // Set state to Preparing and show UI immediately
    setState(State::Preparing);
    emit preparationStarted();

    // Show boundary overlay immediately (UI stays responsive)
    qDebug() << "RecordingManager::startFrameCapture() - Creating boundary overlay...";

    // Clean up any existing overlay first to prevent resource leak
    if (m_boundaryOverlay) {
        qWarning() << "RecordingManager: Previous boundary overlay still exists, cleaning up";
        m_boundaryOverlay->close();
    }

    m_boundaryOverlay = new RecordingBoundaryOverlay();
    m_boundaryOverlay->setAttribute(Qt::WA_DeleteOnClose);
    m_boundaryOverlay->setRegion(m_recordingRegion);
    m_boundaryOverlay->show();
    raiseWindowAboveMenuBar(m_boundaryOverlay);
    qDebug() << "RecordingManager::startFrameCapture() - Boundary overlay shown";

    // Show control bar in preparing state
    qDebug() << "RecordingManager::startFrameCapture() - Creating control bar...";

    // Clean up any existing control bar first to prevent resource leak
    if (m_controlBar) {
        qWarning() << "RecordingManager: Previous control bar still exists, cleaning up";
        disconnect(m_controlBar, nullptr, this, nullptr);
        m_controlBar->close();
    }

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

    // Load annotation setting and setup overlay if enabled
    auto settings = SnapTray::getSettings();
    m_annotationEnabled = settings.value("recording/annotationEnabled", false).toBool();

    if (m_annotationEnabled) {
        qDebug() << "RecordingManager::startFrameCapture() - Creating annotation overlay...";

        // Clean up any existing annotation overlay
        if (m_annotationOverlay) {
            m_annotationOverlay->close();
        }

        m_annotationOverlay = new RecordingAnnotationOverlay();
        m_annotationOverlay->setAttribute(Qt::WA_DeleteOnClose);
        m_annotationOverlay->setRegion(m_recordingRegion);

        // Initialize overlay with control bar's default color and width
        m_annotationOverlay->setColor(m_controlBar->annotationColor());
        m_annotationOverlay->setWidth(m_controlBar->annotationWidth());

        // Connect control bar annotation signals to overlay
        connect(m_controlBar, &RecordingControlBar::toolChanged,
                this, [this](RecordingControlBar::AnnotationTool tool) {
            if (m_annotationOverlay) {
                // Map RecordingControlBar::AnnotationTool to AnnotationController::Tool
                int overlayTool = static_cast<int>(tool);
                m_annotationOverlay->setCurrentTool(overlayTool);
            }
        });

        // Connect color change requests
        connect(m_controlBar, &RecordingControlBar::colorChangeRequested,
                this, [this]() {
            if (m_annotationOverlay) {
                m_annotationOverlay->setColor(m_controlBar->annotationColor());
            }
        });

        // Connect width change requests
        connect(m_controlBar, &RecordingControlBar::widthChangeRequested,
                this, [this]() {
            if (m_annotationOverlay) {
                m_annotationOverlay->setWidth(m_controlBar->annotationWidth());
            }
        });

        // Enable annotation mode on control bar
        m_controlBar->setAnnotationEnabled(true);

        qDebug() << "RecordingManager::startFrameCapture() - Annotation overlay created";
    }

    // Show in preparing state (buttons disabled until ready)
    m_controlBar->setPreparing(true);

    m_controlBar->positionNear(m_recordingRegion);
    m_controlBar->show();
    raiseWindowAboveMenuBar(m_controlBar);
    qDebug() << "RecordingManager::startFrameCapture() - Control bar shown";

    // Start async initialization
    beginAsyncInitialization();

    qDebug() << "RecordingManager::startFrameCapture() - END (async initialization started)";
}

void RecordingManager::beginAsyncInitialization()
{
    qDebug() << "RecordingManager::beginAsyncInitialization() - BEGIN";

    // Load settings for initialization config
    auto settings = SnapTray::getSettings();
    int formatInt = settings.value("recording/outputFormat", 0).toInt();
    bool useGif = (formatInt == 1);

    m_audioEnabled = settings.value("recording/audioEnabled", false).toBool();
    m_audioSource = settings.value("recording/audioSource", 0).toInt();
    m_audioDevice = settings.value("recording/audioDevice").toString();

    // Use physical pixel size for Retina/HiDPI displays
    qreal scale = CoordinateHelper::getDevicePixelRatio(m_targetScreen);
    QSize physicalSize = CoordinateHelper::toEvenPhysicalSize(m_recordingRegion.size(), scale);

    // Create initialization config
    RecordingInitTask::Config config;
    config.region = m_recordingRegion;
    config.screen = m_targetScreen;
    config.frameRate = m_frameRate;
    config.audioEnabled = m_audioEnabled && !useGif;  // GIF doesn't support audio
    config.audioSource = m_audioSource;
    config.audioDevice = m_audioDevice;
    config.outputPath = generateOutputPath();
    config.useNativeEncoder = true;
    config.useGif = useGif;
    config.frameSize = physicalSize;
    config.quality = settings.value("recording/quality", 55).toInt();

    qDebug() << "RecordingManager: Config - region:" << config.region
             << "frameSize:" << config.frameSize
             << "frameRate:" << config.frameRate
             << "format:" << (config.useGif ? "GIF" : "MP4")
             << "audio:" << config.audioEnabled;

    // Create the init task
    m_initTask = new RecordingInitTask(config, nullptr);

    // Connect progress signal for UI feedback
    connect(m_initTask, &RecordingInitTask::progress,
            this, [this](const QString &step) {
        emit preparationProgress(step);
        if (m_controlBar) {
            m_controlBar->setPreparingStatus(step);
        }
    }, Qt::QueuedConnection);

    // Run initialization in background thread
    QFutureWatcher<void> *watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher]() {
        onInitializationComplete();
        watcher->deleteLater();
    });

    QFuture<void> future = QtConcurrent::run([this]() {
        m_initTask->run();
    });
    watcher->setFuture(future);

    qDebug() << "RecordingManager::beginAsyncInitialization() - END";
}

void RecordingManager::onInitializationComplete()
{
    qDebug() << "RecordingManager::onInitializationComplete() - BEGIN";

    // Safety check: ensure we're still in Preparing state
    if (m_state != State::Preparing) {
        qDebug() << "RecordingManager: Initialization completed but state changed, cleaning up";
        if (m_initTask) {
            delete m_initTask;
            m_initTask = nullptr;
        }
        return;
    }

    // Get the result from the init task
    const RecordingInitTask::Result &result = m_initTask->result();

    if (!result.success) {
        qWarning() << "RecordingManager: Initialization failed:" << result.error;
        emit recordingError(result.error);

        // Clean up UI
        if (m_boundaryOverlay) {
            m_boundaryOverlay->close();
        }
        if (m_controlBar) {
            m_controlBar->close();
        }

        delete m_initTask;
        m_initTask = nullptr;

        setState(State::Idle);
        return;
    }

    qDebug() << "RecordingManager: Initialization successful, taking ownership of resources";

    // Take ownership of created resources (already moved to main thread in worker)
    m_captureEngine = result.captureEngine;
    if (m_captureEngine) {
        m_captureEngine->setParent(this);

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

        qDebug() << "RecordingManager: Using capture engine:" << m_captureEngine->engineName();
    }

    m_usingNativeEncoder = result.usingNativeEncoder;
    m_nativeEncoder = result.nativeEncoder;
    m_gifEncoder = result.gifEncoder;

    if (m_nativeEncoder) {
        m_nativeEncoder->setParent(this);
        connect(m_nativeEncoder, &IVideoEncoder::finished,
                this, &RecordingManager::onEncodingFinished);
        connect(m_nativeEncoder, &IVideoEncoder::error,
                this, &RecordingManager::onEncodingError);
    }
    if (m_gifEncoder) {
        m_gifEncoder->setParent(this);
        connect(m_gifEncoder, &NativeGifEncoder::finished,
                this, &RecordingManager::onEncodingFinished);
        connect(m_gifEncoder, &NativeGifEncoder::error,
                this, &RecordingManager::onEncodingError);
    }

    // Create audio engine on main thread (macOS audio APIs require main thread)
    // Audio engine was NOT created in worker thread due to thread affinity requirements
    if (m_audioEnabled) {
        qDebug() << "RecordingManager: Creating audio engine on main thread...";
        m_audioEngine = IAudioCaptureEngine::createBestEngine(this);
        if (m_audioEngine) {
            // Set audio source
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

            // Connect audio error/warning signals
            connect(m_audioEngine, &IAudioCaptureEngine::error,
                    this, [this](const QString &msg) {
                qWarning() << "RecordingManager: Audio error:" << msg;
                emit recordingWarning("Audio error: " + msg);
            });
            connect(m_audioEngine, &IAudioCaptureEngine::warning,
                    this, &RecordingManager::recordingWarning);

            qDebug() << "RecordingManager: Audio engine created:" << m_audioEngine->engineName();
        } else {
            qWarning() << "RecordingManager: Failed to create audio engine";
            m_audioEnabled = false;
        }
    }

    // Connect audio capture to encoder/writer
    if (m_audioEngine) {
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
            // Start audio capture
            if (m_audioEngine->start()) {
                qDebug() << "RecordingManager: Audio capture started using" << m_audioEngine->engineName();
            } else {
                qWarning() << "RecordingManager: Failed to start audio capture";
                emit recordingWarning("Failed to start audio capture. Recording without audio.");
                cleanupAudio();
            }
        } else {
            // GIF format does not support audio - this shouldn't happen as
            // audio is disabled for GIF in beginAsyncInitialization()
            qWarning() << "RecordingManager: Audio not supported for current format";
            emit recordingWarning("Audio is not supported for GIF format.");
            cleanupAudio();
        }
    }

    // Update control bar to show ready state
    if (m_controlBar) {
        m_controlBar->setPreparing(false);
        m_controlBar->setAudioEnabled(m_audioEngine != nullptr);
    }

    // Clean up init task
    delete m_initTask;
    m_initTask = nullptr;

    // Initialize state and counters
    m_elapsedTimer.start();
    setState(State::Recording);
    m_frameCount = 0;

    // Delay frame capture start to allow boundary overlay to render fully
    qDebug() << "RecordingManager: Delaying capture start for overlay rendering...";
    QTimer::singleShot(OVERLAY_RENDER_DELAY_MS, this, [this]() {
        startCaptureTimers();
    });

    qDebug() << "RecordingManager: Recording started at" << m_frameRate << "FPS";
    qDebug() << "RecordingManager::onInitializationComplete() - END (success)";
    emit recordingStarted();
}

void RecordingManager::startCaptureTimers()
{
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
    qDebug() << "RecordingManager: Starting capture timer...";
    m_captureTimer = new QTimer(this);
    connect(m_captureTimer, &QTimer::timeout, this, &RecordingManager::captureFrame);
    m_captureTimer->start(1000 / m_frameRate);
    qDebug() << "RecordingManager: Capture timer started";

    // Start duration timer (update UI every 100ms)
    qDebug() << "RecordingManager: Starting duration timer...";
    m_durationTimer = new QTimer(this);
    connect(m_durationTimer, &QTimer::timeout, this, &RecordingManager::updateDuration);
    m_durationTimer->start(100);
    qDebug() << "RecordingManager: Duration timer started";
}

void RecordingManager::captureFrame()
{
    if (m_state != State::Recording) {
        return;
    }

    // Store local copies to avoid race conditions where pointers become null
    // between the check and actual use (TOCTOU protection)
    ICaptureEngine *captureEngine = m_captureEngine;
    IVideoEncoder *nativeEncoder = m_nativeEncoder;
    NativeGifEncoder *gifEncoder = m_gifEncoder;
    RecordingAnnotationOverlay *annotationOverlay = m_annotationOverlay;
    bool usingNative = m_usingNativeEncoder;
    bool annotationEnabled = m_annotationEnabled;

    if (!captureEngine) {
        return;
    }

    // Check that we have an encoder available using local copies
    if (!usingNative && !gifEncoder) {
        return;
    }
    if (usingNative && !nativeEncoder) {
        return;
    }

    // Capture frame using the capture engine
    QImage frame = captureEngine->captureFrame();

    if (!frame.isNull()) {
        // Composite annotations onto frame if annotation overlay is active
        // Use local copies for thread safety
        if (annotationEnabled && annotationOverlay) {
            qreal scale = CoordinateHelper::getDevicePixelRatio(m_targetScreen);
            annotationOverlay->compositeOntoFrame(frame, scale);
        }

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
        // Use local copies for thread safety
        if (usingNative && nativeEncoder) {
            nativeEncoder->writeFrame(frame, elapsedMs);
        } else if (gifEncoder) {
            gifEncoder->writeFrame(frame, elapsedMs);
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
    } else if (m_gifEncoder) {
        m_gifEncoder->finish();
    }
}

void RecordingManager::cancelRecording()
{
    if (m_state != State::Recording && m_state != State::Paused &&
        m_state != State::Encoding && m_state != State::Preparing) {
        return;
    }

    qDebug() << "RecordingManager: Cancelling recording (state:" << static_cast<int>(m_state) << ")";

    // Cancel any pending initialization
    if (m_initTask) {
        m_initTask->cancel();
        // Note: The init task will be cleaned up when onInitializationComplete() runs
        // and sees that state has changed
    }

    stopFrameCapture();

    // Abort encoding and remove output file
    // Use deleteLater to avoid use-after-free when called from signal handler
    if (m_nativeEncoder) {
        m_nativeEncoder->abort();
        m_nativeEncoder->deleteLater();
        m_nativeEncoder = nullptr;
    }
    if (m_gifEncoder) {
        m_gifEncoder->abort();
        m_gifEncoder->deleteLater();
        m_gifEncoder = nullptr;
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

    // Disconnect annotation signals before closing overlay to prevent dangling connections
    if (m_annotationOverlay && m_controlBar) {
        disconnect(m_controlBar, &RecordingControlBar::toolChanged, this, nullptr);
        disconnect(m_controlBar, &RecordingControlBar::colorChangeRequested, this, nullptr);
        disconnect(m_controlBar, &RecordingControlBar::widthChangeRequested, this, nullptr);
    }

    if (m_annotationOverlay) {
        m_annotationOverlay->close();
        m_annotationOverlay = nullptr;  // Clear pointer since WA_DeleteOnClose will delete it
    }

    if (m_controlBar) {
        m_controlBar->close();
    }
    qDebug() << "RecordingManager::stopFrameCapture() END";
}

void RecordingManager::cleanupRecording()
{
    stopFrameCapture();

    // Use deleteLater() consistently to avoid double-delete if deleteLater()
    // was already called from cancelRecording() or onEncodingError()
    if (m_nativeEncoder) {
        m_nativeEncoder->abort();
        m_nativeEncoder->deleteLater();
        m_nativeEncoder = nullptr;
    }
    if (m_gifEncoder) {
        m_gifEncoder->abort();
        m_gifEncoder->deleteLater();
        m_gifEncoder = nullptr;
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
        } else if (m_gifEncoder) {
            errorMsg = m_gifEncoder->lastError();
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
    if (m_gifEncoder) {
        m_gifEncoder->deleteLater();
        m_gifEncoder = nullptr;
    }
    m_usingNativeEncoder = false;

    if (success) {
        auto settings = SnapTray::getSettings();
        bool showPreview = settings.value("recording/showPreview", true).toBool();

        if (showPreview) {
            // Enter preview state and emit signal
            setState(State::Previewing);
            QString tempPath = outputPath;
            QMetaObject::invokeMethod(this, [this, tempPath]() {
                emit previewRequested(tempPath);
            }, Qt::QueuedConnection);
        } else {
            // Existing flow: go directly to save dialog
            setState(State::Idle);
            QString tempPath = outputPath;
            QMetaObject::invokeMethod(this, [this, tempPath]() {
                showSaveDialog(tempPath);
            }, Qt::QueuedConnection);
        }
    } else {
        setState(State::Idle);
        emit recordingError(errorMsg);
    }
}

void RecordingManager::onPreviewClosed(bool saved)
{
    if (m_state != State::Previewing) {
        qWarning() << "RecordingManager::onPreviewClosed called in unexpected state:"
                   << static_cast<int>(m_state);
        return;
    }

    setState(State::Idle);

    if (saved) {
        qDebug() << "RecordingManager: Preview closed, recording was saved";
    } else {
        qDebug() << "RecordingManager: Preview closed, recording was discarded";
    }
}

void RecordingManager::triggerSaveDialog(const QString &videoPath)
{
    qDebug() << "RecordingManager: Triggering save dialog for:" << videoPath;
    showSaveDialog(videoPath);
}

void RecordingManager::showSaveDialog(const QString &tempOutputPath)
{
    auto settings = SnapTray::getSettings();

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
    if (m_gifEncoder) {
        if (outputPath.isEmpty()) {
            outputPath = m_gifEncoder->outputPath();
        }
        m_gifEncoder->deleteLater();
        m_gifEncoder = nullptr;
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
    auto settings = SnapTray::getSettings();
    int formatInt = settings.value("recording/outputFormat", 0).toInt();
    QString extension = (formatInt == 1) ? "gif" : "mp4";

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
