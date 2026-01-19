#include "RecordingManager.h"
#include "RecordingRegionSelector.h"
#include "RecordingControlBar.h"
#include "RecordingBoundaryOverlay.h"
#include "RecordingAnnotationOverlay.h"
#include "RecordingInitTask.h"
#include "video/InPlacePreviewOverlay.h"
#include "video/CountdownOverlay.h"
#include "video/IVideoPlayer.h"
#include "encoding/NativeGifEncoder.h"
#include "encoding/EncodingWorker.h"
#include "IVideoEncoder.h"
#include "encoding/EncoderFactory.h"
#include "capture/ICaptureEngine.h"
#include "capture/IAudioCaptureEngine.h"
#include "AudioFileWriter.h"
#include "platform/WindowLevel.h"
#include "utils/ResourceCleanupHelper.h"
#include "utils/CoordinateHelper.h"
#include "settings/Settings.h"
#include "settings/FileSettingsManager.h"



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

// Local constants for recording (moved from Constants.h)
namespace {
constexpr int kDefaultFrameRate = 30;
constexpr int kMaxFrameRate = 120;
constexpr int kOverlayRenderDelay = 100;  // ms, delay before capture to allow overlay render
constexpr int kDurationUpdate = 100;      // ms, recording duration UI update interval
}

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
    , m_usingNativeEncoder(false)
    , m_clickHighlightEnabled(false)
    , m_targetScreen(nullptr)
    , m_state(State::Idle)
    , m_frameRate(kDefaultFrameRate)
    , m_frameCount(0)
    , m_pausedDuration(0)
    , m_pauseStartTime(0)
    , m_audioEnabled(false)
    , m_audioSource(0)
    , m_countdownEnabled(true)
    , m_countdownSeconds(3)
{
    cleanupStaleTempFiles();
}

RecordingManager::~RecordingManager()
{
    // Cancel and wait for any pending async initialization
    if (m_initTask) {
        m_initTask->cancel();
    }
    if (m_initFuture.isRunning()) {
        m_initFuture.waitForFinished();
    }
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

RecordingRegionSelector* RecordingManager::createRegionSelector()
{
    if (m_regionSelector && m_regionSelector->isVisible()) {
        qDebug() << "RecordingManager: Already selecting region";
        return nullptr;
    }

    if (m_state == State::Recording || m_state == State::Paused || m_state == State::Encoding) {
        qDebug() << "RecordingManager: Already recording or encoding";
        return nullptr;
    }

    // Clean up any existing selector
    if (m_regionSelector) {
        m_regionSelector->close();
    }

    setState(State::Selecting);

    m_regionSelector = new RecordingRegionSelector();
    m_regionSelector->setAttribute(Qt::WA_DeleteOnClose);

    connect(m_regionSelector, &RecordingRegionSelector::regionSelected,
            this, &RecordingManager::onRegionSelected);
    connect(m_regionSelector, &RecordingRegionSelector::cancelledWithRegion,
            this, &RecordingManager::onRegionCancelledWithRegion);
    connect(m_regionSelector, &RecordingRegionSelector::cancelled,
            this, &RecordingManager::onRegionCancelled);

    return m_regionSelector;
}

void RecordingManager::startRegionSelection()
{
    auto* selector = createRegionSelector();
    if (!selector) {
        return;
    }

    // Determine target screen (cursor location)
    QScreen *targetScreen = QGuiApplication::screenAt(QCursor::pos());
    if (!targetScreen) {
        targetScreen = QGuiApplication::primaryScreen();
    }

    qDebug() << "RecordingManager: Starting region selection on screen:" << targetScreen->name();
    qDebug() << "=== RecordingManager: Setting up region selector ===";
    qDebug() << "Target screen geometry:" << targetScreen->geometry();

    selector->setGeometry(targetScreen->geometry());
    qDebug() << "Widget geometry after setGeometry:" << selector->geometry();
    selector->initializeForScreen(targetScreen);
    selector->show();
    qDebug() << "Widget geometry after show:" << selector->geometry();
    raiseWindowAboveMenuBar(selector);
    selector->activateWindow();
    selector->raise();
}

void RecordingManager::startRegionSelectionWithPreset(const QRect &region, QScreen *screen)
{
    auto* selector = createRegionSelector();
    if (!selector) {
        return;
    }

    qDebug() << "RecordingManager: Starting region selection with preset region:" << region
             << "on screen:" << screen->name();

    selector->setGeometry(screen->geometry());
    selector->initializeWithRegion(screen, region);
    selector->show();
    raiseWindowAboveMenuBar(selector);
    selector->activateWindow();
    selector->raise();
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
    m_frameRate = settings.value("recording/framerate", kDefaultFrameRate).toInt();
    if (m_frameRate <= 0 || m_frameRate > kMaxFrameRate) {
        int invalidRate = m_frameRate;
        m_frameRate = kDefaultFrameRate;
        qDebug() << "RecordingManager: Invalid frame rate" << invalidRate << ", using default" << kDefaultFrameRate;
        emit recordingWarning(tr("Invalid frame rate %1, using default %2 FPS.").arg(invalidRate).arg(kDefaultFrameRate));
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
    m_frameRate = settings.value("recording/framerate", kDefaultFrameRate).toInt();
    if (m_frameRate <= 0 || m_frameRate > kMaxFrameRate) {
        int invalidRate = m_frameRate;
        m_frameRate = kDefaultFrameRate;
        qDebug() << "RecordingManager: Invalid frame rate" << invalidRate << ", using default" << kDefaultFrameRate;
        emit recordingWarning(tr("Invalid frame rate %1, using default %2 FPS.").arg(invalidRate).arg(kDefaultFrameRate));
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

    // Safety check: clean up any existing encoding worker
    if (m_encodingWorker) {
        qDebug() << "RecordingManager: Previous encoding worker still exists, cleaning up";
        disconnect(m_encodingWorker.get(), &EncodingWorker::finished,
                   this, &RecordingManager::onEncodingFinished);
        disconnect(m_encodingWorker.get(), &EncodingWorker::error,
                   this, &RecordingManager::onEncodingError);
        m_encodingWorker->stop();
        m_encodingWorker.reset();
    }
    m_usingNativeEncoder = false;

    // Set state to Preparing and show UI immediately
    setState(State::Preparing);
    emit preparationStarted();

    // Show boundary overlay immediately (UI stays responsive)
    qDebug() << "RecordingManager::startFrameCapture() - Creating boundary overlay...";

    // Clean up any existing overlay first to prevent resource leak
    if (m_boundaryOverlay) {
        qDebug() << "RecordingManager: Previous boundary overlay still exists, cleaning up";
        m_boundaryOverlay->close();
        m_boundaryOverlay = nullptr;
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
        qDebug() << "RecordingManager: Previous control bar still exists, cleaning up";
        disconnect(m_controlBar, nullptr, this, nullptr);
        m_controlBar->close();
        m_controlBar = nullptr;
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

    // Load click highlight settings
    auto settings = SnapTray::getSettings();
    m_clickHighlightEnabled = settings.value("recording/clickHighlightEnabled", false).toBool();

    // Create effects overlay if click highlight or visual effects are enabled
    // Clean up any existing annotation overlay
    if (m_annotationOverlay) {
        m_annotationOverlay->close();
    }

    m_annotationOverlay = new RecordingAnnotationOverlay();
    m_annotationOverlay->setAttribute(Qt::WA_DeleteOnClose);
    m_annotationOverlay->setRegion(m_recordingRegion);

    // Enable click highlight if configured
    if (m_clickHighlightEnabled) {
        bool started = m_annotationOverlay->setClickHighlightEnabled(true);
        if (!started) {
            qWarning() << "RecordingManager: Click highlight requires Accessibility permission. "
                       << "Grant permission in System Preferences > Security & Privacy > Privacy > Accessibility";
            // Continue recording without click highlight - don't block the user
        }
    }

    // Connect visual effect signals from control bar
    connect(m_controlBar, &RecordingControlBar::laserPointerToggled,
            this, [this](bool enabled) {
        if (m_annotationOverlay) {
            m_annotationOverlay->setLaserPointerEnabled(enabled);
        }
    });

    connect(m_controlBar, &RecordingControlBar::cursorHighlightToggled,
            this, [this](bool enabled) {
        if (m_annotationOverlay) {
            m_annotationOverlay->setCursorHighlightEnabled(enabled);
        }
    });

    connect(m_controlBar, &RecordingControlBar::spotlightToggled,
            this, [this](bool enabled) {
        if (m_annotationOverlay) {
            m_annotationOverlay->setSpotlightEnabled(enabled);
        }
    });

    connect(m_controlBar, &RecordingControlBar::compositeIndicatorsToggled,
            this, [this](bool enabled) {
        if (m_annotationOverlay) {
            m_annotationOverlay->setCompositeIndicatorsToVideo(enabled);
        }
    });

    // Sync control bar state with click highlight
    m_controlBar->setClickHighlightEnabled(m_clickHighlightEnabled);

    qDebug() << "RecordingManager::startFrameCapture() - Effects overlay created";

    // Show in preparing state (buttons disabled until ready)
    m_controlBar->setPreparing(true);

    m_controlBar->show();
    raiseWindowAboveMenuBar(m_controlBar);
    // Position after show() to avoid Qt/macOS adjusting the position
    m_controlBar->positionNear(m_recordingRegion);
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

    // Collect UI window IDs to exclude from capture
    // These are set after show() to ensure Qt has created the native window
    if (m_boundaryOverlay) {
        config.excludedWindowIds.append(m_boundaryOverlay->winId());
        setWindowExcludedFromCapture(m_boundaryOverlay, true);
    }
    if (m_controlBar) {
        config.excludedWindowIds.append(m_controlBar->winId());
        setWindowExcludedFromCapture(m_controlBar, true);
    }
    if (m_annotationOverlay) {
        config.excludedWindowIds.append(m_annotationOverlay->winId());
        setWindowExcludedFromCapture(m_annotationOverlay, true);
    }
    qDebug() << "RecordingManager: Excluding" << config.excludedWindowIds.size() << "windows from capture";

    qDebug() << "RecordingManager: Config - region:" << config.region
             << "frameSize:" << config.frameSize
             << "frameRate:" << config.frameRate
             << "format:" << (config.useGif ? "GIF" : "MP4")
             << "audio:" << config.audioEnabled;

    // Create the init task
    m_initTask = std::make_unique<RecordingInitTask>(config, nullptr);

    // Connect progress signal for UI feedback
    connect(m_initTask.get(), &RecordingInitTask::progress,
            this, [this](const QString &step) {
        emit preparationProgress(step);
        if (m_controlBar) {
            m_controlBar->setPreparingStatus(step);
        }
    }, Qt::QueuedConnection);

    // Run initialization in background thread
    // Use QPointer to guard against this being deleted while task runs
    QPointer<RecordingManager> guard(this);
    QFutureWatcher<void> *watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [guard, watcher]() {
        if (guard) {
            guard->onInitializationComplete();
        }
        watcher->deleteLater();
    });

    // Capture task pointer to avoid accessing this in background thread
    RecordingInitTask *task = m_initTask.get();
    m_initFuture = QtConcurrent::run([task]() {
        if (task) {
            task->run();
        }
    });
    watcher->setFuture(m_initFuture);

    qDebug() << "RecordingManager::beginAsyncInitialization() - END";
}

void RecordingManager::onInitializationComplete()
{
    qDebug() << "RecordingManager::onInitializationComplete() - BEGIN";

    // Safety check: ensure we're still in Preparing state
    if (m_state != State::Preparing) {
        qDebug() << "RecordingManager: Initialization completed but state changed, cleaning up";
        m_initTask.reset();
        return;
    }

    // Safety check: ensure m_initTask is valid
    if (!m_initTask) {
        qWarning() << "RecordingManager: onInitializationComplete called with null initTask";
        setState(State::Idle);
        emit recordingError(tr("Internal error: initialization task is null"));
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

        m_initTask.reset();

        setState(State::Idle);
        return;
    }

    qDebug() << "RecordingManager: Initialization successful, taking ownership of resources";

    // Take ownership of created resources (already moved to main thread in worker)
    m_captureEngine.reset(result.captureEngine);
    if (m_captureEngine) {
        // IMPORTANT: Do NOT set parent to this, as we use std::unique_ptr for ownership
        
        // Forward capture engine warnings to UI
        connect(m_captureEngine.get(), &ICaptureEngine::warning,
                this, &RecordingManager::recordingWarning);

        // Connect capture engine error signal
        connect(m_captureEngine.get(), &ICaptureEngine::error,
                this, [this](const QString &msg) {
            qDebug() << "RecordingManager: Capture engine error:" << msg;
            stopRecording();
            emit recordingError(msg);
        });

        qDebug() << "RecordingManager: Using capture engine:" << m_captureEngine->engineName();
    }

    m_usingNativeEncoder = result.usingNativeEncoder;

    // Create encoding worker to offload encoding from main thread
    m_encodingWorker = std::make_unique<EncodingWorker>(nullptr);

    // Transfer encoder ownership to worker
    if (result.nativeEncoder) {
        m_encodingWorker->setVideoEncoder(result.nativeEncoder);
        m_encodingWorker->setEncoderType(EncodingWorker::EncoderType::Video);
    }
    if (result.gifEncoder) {
        m_encodingWorker->setGifEncoder(result.gifEncoder);
        if (!result.nativeEncoder) {
            m_encodingWorker->setEncoderType(EncodingWorker::EncoderType::Gif);
        }
    }

    // Configure worker frame size
    m_encodingWorker->setFrameSize(m_recordingRegion.size());

    // Connect worker signals
    connect(m_encodingWorker.get(), &EncodingWorker::finished,
            this, &RecordingManager::onEncodingFinished);
    connect(m_encodingWorker.get(), &EncodingWorker::error,
            this, &RecordingManager::onEncodingError);
    connect(m_encodingWorker.get(), &EncodingWorker::queuePressure,
            this, [this](int depth, int max) {
        qDebug() << "RecordingManager: Encoder queue pressure:" << depth << "/" << max;
    });

    // Start the encoding worker
    if (!m_encodingWorker->start()) {
        qWarning() << "RecordingManager: Failed to start encoding worker";
        emit recordingError(tr("Failed to start encoding worker"));
        m_encodingWorker.reset();
        setState(State::Idle);
        return;
    }

    // Create audio engine on main thread (macOS audio APIs require main thread)
    // Audio engine was NOT created in worker thread due to thread affinity requirements
    if (m_audioEnabled) {
        qDebug() << "RecordingManager: Creating audio engine on main thread...";
        // Pass nullptr as parent since we use custom deleter with unique_ptr
        m_audioEngine.reset(IAudioCaptureEngine::createBestEngine(nullptr));
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
            connect(m_audioEngine.get(), &IAudioCaptureEngine::error,
                    this, [this](const QString &msg) {
                qWarning() << "RecordingManager: Audio error:" << msg;
                emit recordingWarning("Audio error: " + msg);
            });
            connect(m_audioEngine.get(), &IAudioCaptureEngine::warning,
                    this, &RecordingManager::recordingWarning);

            qDebug() << "RecordingManager: Audio engine created:" << m_audioEngine->engineName();
        } else {
            qWarning() << "RecordingManager: Failed to create audio engine";
            m_audioEnabled = false;
            emit recordingWarning(tr("Failed to initialize audio capture. Recording without audio."));
        }
    }

    // Connect audio capture to encoding worker
    if (m_audioEngine && m_encodingWorker) {
        bool useNativeAudio = m_usingNativeEncoder;

        if (useNativeAudio) {
            // Connect audio data to encoding worker (which forwards to encoder)
            connect(m_audioEngine.get(), &IAudioCaptureEngine::audioDataReady,
                    this, [this](const QByteArray &data, qint64 timestamp) {
                if (m_encodingWorker) {
                    m_encodingWorker->writeAudioSamples(data, timestamp);
                }
            }, Qt::QueuedConnection);
            qDebug() << "RecordingManager: Audio connected to encoding worker";
            // NOTE: Audio capture is started in startRecordingAfterCountdown()
            // to synchronize timestamps with video
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
        m_controlBar->setAudioEnabled(m_audioEngine != nullptr && m_audioEnabled);
    }

    // Clean up init task
    m_initTask.reset();

    // Load countdown settings
    auto settings = SnapTray::getSettings();
    m_countdownEnabled = settings.value("recording/countdownEnabled", true).toBool();
    m_countdownSeconds = settings.value("recording/countdownSeconds", 3).toInt();

    // Start countdown if enabled, otherwise go directly to recording
    if (m_countdownEnabled && m_countdownSeconds > 0) {
        qDebug() << "RecordingManager: Starting countdown for" << m_countdownSeconds << "seconds";
        startCountdown();
    } else {
        qDebug() << "RecordingManager: Countdown disabled, starting recording directly";
        startRecordingAfterCountdown();
    }

    qDebug() << "RecordingManager::onInitializationComplete() - END (success)";
}

void RecordingManager::startCountdown()
{
    setState(State::Countdown);

    m_countdownOverlay = new CountdownOverlay();
    m_countdownOverlay->setAttribute(Qt::WA_DeleteOnClose);
    m_countdownOverlay->setRegion(m_recordingRegion, m_targetScreen);
    m_countdownOverlay->setCountdownSeconds(m_countdownSeconds);

    connect(m_countdownOverlay, &CountdownOverlay::countdownFinished,
            this, &RecordingManager::startRecordingAfterCountdown);
    connect(m_countdownOverlay, &CountdownOverlay::countdownCancelled,
            this, &RecordingManager::onCountdownCancelled);

    m_countdownOverlay->show();
    raiseWindowAboveMenuBar(m_countdownOverlay);
    m_countdownOverlay->start();
}

void RecordingManager::startRecordingAfterCountdown()
{
    // Clean up countdown overlay if it exists
    if (m_countdownOverlay) {
        m_countdownOverlay->close();
        m_countdownOverlay = nullptr;
    }

    // Initialize state and counters
    m_elapsedTimer.start();
    setState(State::Recording);
    m_frameCount = 0;

    // Start audio capture here to synchronize with video timer
    if (m_audioEngine) {
        if (m_audioEngine->start()) {
            qDebug() << "RecordingManager: Audio capture started using" << m_audioEngine->engineName();
        } else {
            qWarning() << "RecordingManager: Failed to start audio capture";
            emit recordingWarning("Failed to start audio capture. Recording without audio.");
            cleanupAudio();
        }
    }

    // Load watermark settings for recording and configure worker
    m_watermarkSettings = WatermarkSettingsManager::instance().load();
    if (m_watermarkSettings.applyToRecording && !m_watermarkSettings.imagePath.isEmpty()) {
        m_watermarkSettings.enabled = true;  // Enable for render() to work
        if (m_encodingWorker) {
            m_encodingWorker->setWatermarkSettings(m_watermarkSettings);
        }
    }

    // Delay frame capture start to allow boundary overlay to render fully
    qDebug() << "RecordingManager: Delaying capture start for overlay rendering...";
    QTimer::singleShot(kOverlayRenderDelay, this, [this]() {
        startCaptureTimers();
    });

    qDebug() << "RecordingManager: Recording started at" << m_frameRate << "FPS";
    emit recordingStarted();
}

void RecordingManager::onCountdownCancelled()
{
    qDebug() << "RecordingManager: Countdown cancelled by user";

    // Clean up countdown overlay
    m_countdownOverlay = nullptr;  // Already deleted via WA_DeleteOnClose

    // Cancel the recording
    cancelRecording();
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
        qDebug() << "RecordingManager: Capture timer already exists, cleaning up";
        m_captureTimer->stop();
        m_captureTimer.reset();
    }
    if (m_durationTimer) {
        qDebug() << "RecordingManager: Duration timer already exists, cleaning up";
        m_durationTimer->stop();
        m_durationTimer.reset();
    }

    // Start frame capture timer
    qDebug() << "RecordingManager: Starting capture timer...";
    // No parent, managed by unique_ptr
    m_captureTimer = std::make_unique<QTimer>(nullptr);
    connect(m_captureTimer.get(), &QTimer::timeout, this, &RecordingManager::captureFrame);
    m_captureTimer->start(1000 / m_frameRate);
    qDebug() << "RecordingManager: Capture timer started";

    // Start duration timer (update UI at regular intervals)
    qDebug() << "RecordingManager: Starting duration timer...";
    // No parent, managed by unique_ptr
    m_durationTimer = std::make_unique<QTimer>(nullptr);
    connect(m_durationTimer.get(), &QTimer::timeout, this, &RecordingManager::updateDuration);
    m_durationTimer->start(kDurationUpdate);
    qDebug() << "RecordingManager: Duration timer started";
}

void RecordingManager::captureFrame()
{
    if (m_state != State::Recording) {
        return;
    }

    // Store local copies (raw pointers) to avoid race conditions
    // where unique_ptrs might be reset during execution
    ICaptureEngine *captureEngine = m_captureEngine.get();
    EncodingWorker *encodingWorker = m_encodingWorker.get();
    RecordingAnnotationOverlay *annotationOverlay = m_annotationOverlay;

    if (!captureEngine || !encodingWorker) {
        return;
    }

    // Capture frame using the capture engine (fast - returns cached frame)
    QImage frame = captureEngine->captureFrame();

    if (!frame.isNull()) {
        // Calculate timestamp (keep on main thread for accuracy)
        qint64 elapsedMs = 0;
        {
            QMutexLocker locker(&m_durationMutex);
            qint64 rawElapsed = m_elapsedTimer.elapsed();
            elapsedMs = rawElapsed - m_pausedDuration;
        }
        if (elapsedMs < 0) {
            elapsedMs = 0;
        }

        // Composite cursor effects on main thread (fast, needs live cursor state)
        if (annotationOverlay) {
            qreal scale = CoordinateHelper::getDevicePixelRatio(m_targetScreen);
            annotationOverlay->compositeOntoFrame(frame, scale);
        }

        // Enqueue frame for encoding (non-blocking)
        // Watermark is applied by the worker thread
        EncodingWorker::FrameData frameData{frame, elapsedMs};
        if (!encodingWorker->enqueueFrame(frameData)) {
            // Queue full - frame dropped (logged by worker)
            qDebug() << "RecordingManager: Encoder queue full, dropping frame";
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

    // Request encoding worker to finish (processes remaining queue then finishes encoder)
    if (m_encodingWorker) {
        m_encodingWorker->requestFinish();
        // Worker will emit finished() when encoding is complete
    }
}

void RecordingManager::cancelRecording()
{
    if (m_state != State::Recording && m_state != State::Paused &&
        m_state != State::Encoding && m_state != State::Preparing &&
        m_state != State::Countdown) {
        return;
    }

    qDebug() << "RecordingManager: Cancelling recording (state:" << static_cast<int>(m_state) << ")";

    // Cancel countdown overlay if active
    if (m_countdownOverlay) {
        m_countdownOverlay->close();
        m_countdownOverlay = nullptr;
    }

    // Cancel any pending initialization
    if (m_initTask) {
        m_initTask->cancel();
        // Note: The init task will be cleaned up when onInitializationComplete() runs
        // and sees that state has changed
    }

    stopFrameCapture();

    // Stop encoding worker (aborts encoding)
    if (m_encodingWorker) {
        m_encodingWorker->stop();
        m_encodingWorker.reset();
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
        m_captureTimer.reset();
    }

    if (m_durationTimer) {
        m_durationTimer->stop();
        m_durationTimer.reset();
    }
    qDebug() << "RecordingManager: Timers stopped";

    // Calculate final recording duration
    qint64 finalDuration = 0;
    {
        QMutexLocker locker(&m_durationMutex);
        finalDuration = m_elapsedTimer.elapsed() - m_pausedDuration;
    }
    if (finalDuration < 0) finalDuration = 0;

    // Stop audio capture and finalize audio file
    // Custom deleter handles disconnect + stop() + deleteLater()
    qDebug() << "RecordingManager: Stopping audio engine, m_audioEngine=" << (m_audioEngine ? "exists" : "null");
    if (m_audioEngine) {
        m_audioEngine.reset();
        qDebug() << "RecordingManager: Audio engine stopped and scheduled for deletion";
    }

    qDebug() << "RecordingManager: Finishing audio writer...";
    if (m_audioWriter) {
        m_audioWriter->finish();
        qDebug() << "RecordingManager: Audio file written:" << m_tempAudioPath
                 << "Duration:" << m_audioWriter->durationMs() << "ms";
        m_audioWriter.reset();
    }
    qDebug() << "RecordingManager: Audio writer finished";

    // Stop capture engine
    // Custom deleter handles disconnect + stop() + deleteLater()
    qDebug() << "RecordingManager: Stopping capture engine...";
    m_captureEngine.reset();
    qDebug() << "RecordingManager: Capture engine stopped";

    // Close UI overlays
    qDebug() << "RecordingManager: Closing UI overlays...";
    if (m_boundaryOverlay) {
        m_boundaryOverlay->close();
        m_boundaryOverlay = nullptr;
    }

    if (m_annotationOverlay) {
        m_annotationOverlay->close();
        m_annotationOverlay = nullptr;  // Clear pointer since WA_DeleteOnClose will delete it
    }

    if (m_controlBar) {
        m_controlBar->close();
        m_controlBar = nullptr;
    }
    qDebug() << "RecordingManager::stopFrameCapture() END";
}

void RecordingManager::cleanupRecording()
{
    stopFrameCapture();

    // Stop and clean up encoding worker
    if (m_encodingWorker) {
        m_encodingWorker->stop();
        m_encodingWorker.reset();
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
    // Custom deleter handles disconnect + stop() + deleteLater()
    m_audioEngine.reset();
    m_audioWriter.reset();
    ResourceCleanupHelper::removeTempFile(m_tempAudioPath);
}

void RecordingManager::onEncodingFinished(bool success, const QString &outputPath)
{
    qDebug() << "RecordingManager: Encoding finished, success:" << success << "path:" << outputPath;

    // Get error message if encoding failed
    QString errorMsg;
    if (!success) {
        errorMsg = "Failed to encode video";
    }

    // Clean up encoding worker
    m_encodingWorker.reset();
    m_usingNativeEncoder = false;

    if (success) {
        auto settings = SnapTray::getSettings();
        bool showPreview = settings.value("recording/showPreview", true).toBool();

        if (showPreview) {
            QString tempPath = outputPath;
            QMetaObject::invokeMethod(this, [this, tempPath]() {
                m_tempVideoPath = tempPath;
                setState(State::Previewing);
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
    m_tempVideoPath.clear();

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
    // Get settings from FileSettingsManager
    auto& fileSettings = FileSettingsManager::instance();

    // Check auto-save setting
    bool autoSave = fileSettings.loadAutoSaveRecordings();
    QString outputDir = fileSettings.loadRecordingPath();
    QString dateFormat = fileSettings.loadDateFormat();
    QString prefix = fileSettings.loadFilenamePrefix();

    // Get extension from current file
    QFileInfo fileInfo(tempOutputPath);
    QString extension = fileInfo.suffix();

    if (autoSave) {
        // Auto-save: generate filename and save directly without dialog
        // Use atomic rename approach to avoid TOCTOU race condition
        QString timestamp = QDateTime::currentDateTime().toString(dateFormat);
        QString baseName;
        if (prefix.isEmpty()) {
            baseName = QString("Recording_%1").arg(timestamp);
        } else {
            baseName = QString("%1_Recording_%2").arg(prefix).arg(timestamp);
        }
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

    // Clean up encoding worker
    if (m_encodingWorker) {
        m_encodingWorker->stop();
        m_encodingWorker.reset();
    }
    m_usingNativeEncoder = false;

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

// ============================================================================
// In-Place Preview Mode Implementation
// ============================================================================

void RecordingManager::transitionToPreviewMode(const QString &videoPath)
{
    qDebug() << "RecordingManager: Transitioning to preview mode with" << videoPath;

    m_tempVideoPath = videoPath;
    setState(State::Previewing);

    // Hide annotation overlay
    if (m_annotationOverlay) {
        m_annotationOverlay->hide();
        m_annotationOverlay->deleteLater();
        m_annotationOverlay = nullptr;
    }

    // 1. Keep boundary overlay but change to Paused mode and show it
    if (m_boundaryOverlay) {
        m_boundaryOverlay->setBorderMode(RecordingBoundaryOverlay::BorderMode::Paused);
        m_boundaryOverlay->show();
    }

    // 2. Switch control bar to Preview mode
    if (m_controlBar) {
        m_controlBar->setMode(RecordingControlBar::Mode::Preview);
        connectPreviewSignals();

        // Reposition after width change (320px -> 480px) and ensure visibility
        m_controlBar->show();
        raiseWindowAboveMenuBar(m_controlBar);
        // Position after show() to avoid Qt/macOS adjusting the position
        m_controlBar->positionNear(m_recordingRegion);
    }

    // 3. Create and show preview overlay
    m_previewOverlay = new InPlacePreviewOverlay();
    m_previewOverlay->setAttribute(Qt::WA_DeleteOnClose);
    m_previewOverlay->setRegion(m_recordingRegion, m_targetScreen);

    // Connect preview overlay signals
    connect(m_previewOverlay, &InPlacePreviewOverlay::positionChanged,
            this, &RecordingManager::onPreviewPositionChanged);
    connect(m_previewOverlay, &InPlacePreviewOverlay::durationChanged,
            this, &RecordingManager::onPreviewDurationChanged);
    connect(m_previewOverlay, &InPlacePreviewOverlay::stateChanged,
            this, [this](IVideoPlayer::State state) {
                onPreviewStateChanged(static_cast<int>(state));
            });
    connect(m_previewOverlay, &InPlacePreviewOverlay::errorOccurred,
            this, [this](const QString &msg) {
                qWarning() << "Preview error:" << msg;
                emit recordingError("Preview error: " + msg);
            });

    if (m_previewOverlay->loadVideo(m_tempVideoPath)) {
        m_previewOverlay->show();
        raiseWindowAboveMenuBar(m_previewOverlay);

        // Raise control bar again to ensure it's above the preview overlay
        if (m_controlBar) {
            raiseWindowAboveMenuBar(m_controlBar);
        }

        // Enable looping for preview
        m_previewOverlay->setLooping(true);

        // Start playback automatically
        m_previewOverlay->play();
    } else {
        qWarning() << "RecordingManager: Failed to load video for preview";
        emit recordingError("Failed to load video preview");
        cleanupPreviewMode();
        setState(State::Idle);
    }
}

void RecordingManager::cleanupPreviewMode()
{
    qDebug() << "RecordingManager: Cleaning up preview mode";

    // Close and delete preview overlay
    if (m_previewOverlay) {
        m_previewOverlay->stop();
        m_previewOverlay->close();
        m_previewOverlay = nullptr;
    }

    // Hide and delete boundary overlay
    if (m_boundaryOverlay) {
        m_boundaryOverlay->close();
        m_boundaryOverlay = nullptr;
    }

    // Hide and delete control bar
    if (m_controlBar) {
        m_controlBar->close();
        m_controlBar = nullptr;
    }
}

void RecordingManager::connectPreviewSignals()
{
    if (!m_controlBar) return;

    // Disconnect any existing recording mode connections first
    // (they're automatically handled by Qt's signal-slot mechanism)

    // Connect preview mode signals
    connect(m_controlBar, &RecordingControlBar::playRequested,
            this, &RecordingManager::onPreviewPlayRequested);
    connect(m_controlBar, &RecordingControlBar::pauseRequested,
            this, &RecordingManager::onPreviewPauseRequested);
    connect(m_controlBar, &RecordingControlBar::seekRequested,
            this, &RecordingManager::onPreviewSeekRequested);
    connect(m_controlBar, &RecordingControlBar::volumeToggled,
            this, &RecordingManager::onPreviewVolumeToggled);
    connect(m_controlBar, &RecordingControlBar::annotateRequested,
            this, &RecordingManager::onPreviewAnnotateRequested);
    connect(m_controlBar, &RecordingControlBar::savePreviewRequested,
            this, &RecordingManager::onPreviewSaveRequested);
    connect(m_controlBar, &RecordingControlBar::discardPreviewRequested,
            this, &RecordingManager::onPreviewDiscardRequested);
}

void RecordingManager::onPreviewPlayRequested()
{
    if (m_previewOverlay) {
        m_previewOverlay->play();
    }
}

void RecordingManager::onPreviewPauseRequested()
{
    if (m_previewOverlay) {
        m_previewOverlay->pause();
    }
}

void RecordingManager::onPreviewSeekRequested(qint64 positionMs)
{
    if (m_previewOverlay) {
        m_previewOverlay->seek(positionMs);
    }
}

void RecordingManager::onPreviewVolumeToggled()
{
    if (m_previewOverlay) {
        bool newMuted = !m_previewOverlay->isMuted();
        m_previewOverlay->setMuted(newMuted);
        if (m_controlBar) {
            m_controlBar->setMuted(newMuted);
        }
    }
}

void RecordingManager::onPreviewAnnotateRequested()
{
    if (m_state != State::Previewing) {
        return;
    }

    QString tempPath = m_tempVideoPath;
    cleanupPreviewMode();
    if (!tempPath.isEmpty()) {
        emit annotatePreviewRequested(tempPath);
    } else {
        qWarning() << "RecordingManager: No preview video path for annotation";
    }
}

void RecordingManager::onPreviewSaveRequested()
{
    qDebug() << "RecordingManager: Save preview requested";

    // Store video path before cleanup
    QString videoPath = m_tempVideoPath;

    // Stop playback and cleanup preview UI
    if (m_previewOverlay) {
        m_previewOverlay->stop();
    }
    cleanupPreviewMode();
    setState(State::Idle);

    showSaveDialog(videoPath);
}

void RecordingManager::onPreviewDiscardRequested()
{
    qDebug() << "RecordingManager: Discard preview requested";

    // Store path before cleanup
    QString videoPath = m_tempVideoPath;

    // Stop playback and cleanup
    if (m_previewOverlay) {
        m_previewOverlay->stop();
    }
    cleanupPreviewMode();

    // Delete temp file
    if (!videoPath.isEmpty() && QFile::exists(videoPath)) {
        if (QFile::remove(videoPath)) {
            qDebug() << "RecordingManager: Removed temp file:" << videoPath;
        } else {
            qWarning() << "RecordingManager: Failed to remove temp file:" << videoPath;
        }
    }
    m_tempVideoPath.clear();

    setState(State::Idle);
    emit recordingCancelled();
}

void RecordingManager::onPreviewPositionChanged(qint64 positionMs)
{
    if (m_controlBar) {
        m_controlBar->updatePreviewPosition(positionMs);
    }
}

void RecordingManager::onPreviewDurationChanged(qint64 durationMs)
{
    if (m_controlBar) {
        m_controlBar->updatePreviewDuration(durationMs);
    }
}

void RecordingManager::onPreviewStateChanged(int state)
{
    IVideoPlayer::State playerState = static_cast<IVideoPlayer::State>(state);
    bool isPlaying = (playerState == IVideoPlayer::State::Playing);

    if (m_controlBar) {
        m_controlBar->setPlaying(isPlaying);
    }

    if (m_boundaryOverlay) {
        if (isPlaying) {
            m_boundaryOverlay->setBorderMode(RecordingBoundaryOverlay::BorderMode::Playing);
        } else {
            m_boundaryOverlay->setBorderMode(RecordingBoundaryOverlay::BorderMode::Paused);
        }
    }
}