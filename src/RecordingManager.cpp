#include "RecordingManager.h"
#include "RecordingRegionSelector.h"
#include "RecordingControlBar.h"
#include "RecordingBoundaryOverlay.h"
#include "RecordingInitTask.h"
#include "video/CountdownOverlay.h"
#include "encoding/NativeGifEncoder.h"
#include "encoding/EncodingWorker.h"
#include "IVideoEncoder.h"
#include "encoding/EncoderFactory.h"
#include "capture/ICaptureEngine.h"
#include "capture/IAudioCaptureEngine.h"
#include "platform/WindowLevel.h"
#include "utils/ResourceCleanupHelper.h"
#include "utils/CoordinateHelper.h"
#include "utils/FilenameTemplateEngine.h"
#include "settings/Settings.h"
#include "settings/FileSettingsManager.h"



#include <QGuiApplication>
#include <QCursor>
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
constexpr int kDefaultAudioSampleRate = 48000;
constexpr int kDefaultAudioChannels = 2;
constexpr int kDefaultAudioBitsPerSample = 16;
constexpr int kOverlayRenderDelay = 100;  // ms, delay before capture to allow overlay render
constexpr int kDurationUpdate = 100;      // ms, recording duration UI update interval

QScreen* resolveTargetScreen(QScreen* preferred)
{
    if (preferred) {
        return preferred;
    }

    QScreen* targetScreen = QGuiApplication::screenAt(QCursor::pos());
    if (!targetScreen) {
        targetScreen = QGuiApplication::primaryScreen();
    }
    return targetScreen;
}

bool isScreenAvailable(QScreen* screen)
{
    return screen && QGuiApplication::screens().contains(screen);
}

IAudioCaptureEngine::AudioSource resolveAudioSource(int audioSourceSetting)
{
    switch (audioSourceSetting) {
    case 1: return IAudioCaptureEngine::AudioSource::SystemAudio;
    case 2: return IAudioCaptureEngine::AudioSource::Both;
    default: return IAudioCaptureEngine::AudioSource::Microphone;
    }
}
}

static void syncFile(const QString &path)
{
#ifdef Q_OS_WIN
    const std::wstring widePath = path.toStdWString();
    HANDLE hFile = CreateFileW(
        widePath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (hFile != INVALID_HANDLE_VALUE) {
        if (!FlushFileBuffers(hFile)) {
            qWarning() << "Failed to flush file buffers for" << path;
        }
        if (!CloseHandle(hFile)) {
            qWarning() << "Failed to close file handle for" << path;
        }
    }
#else
    int fd = open(path.toLocal8Bit().constData(), O_RDONLY);
    if (fd >= 0) {
        if (fsync(fd) == -1) {
            qWarning() << "Failed to sync file" << path;
        }
        if (close(fd) == -1) {
            qWarning() << "Failed to close file" << path;
        }
    }
#endif
}

RecordingManager::RecordingManager(QObject *parent)
    : QObject(parent)
    , m_usingNativeEncoder(false)
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
    if (m_initTask) {
        m_initTask->discardResult();
        m_initTask.clear();
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
           (m_state == State::Preparing) || (m_state == State::Encoding) ||
           m_initFuture.isRunning();
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
        return nullptr;
    }

    if (m_state == State::Recording || m_state == State::Paused ||
        m_state == State::Encoding || m_state == State::Preparing ||
        m_initFuture.isRunning()) {
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

void RecordingManager::loadAndValidateFrameRate()
{
    auto settings = SnapTray::getSettings();
    m_frameRate = settings.value("recording/framerate", kDefaultFrameRate).toInt();
    if (m_frameRate <= 0 || m_frameRate > kMaxFrameRate) {
        int invalidRate = m_frameRate;
        m_frameRate = kDefaultFrameRate;
        qWarning() << "RecordingManager: Invalid frame rate" << invalidRate << ", using default" << kDefaultFrameRate << "FPS";
    }
}

void RecordingManager::resetPauseTracking()
{
    m_pausedDuration = 0;
    m_pauseStartTime = 0;
}

void RecordingManager::startRegionSelectionWithPreset(const QRect &region, QScreen *screen)
{
    QScreen* targetScreen = resolveTargetScreen(screen);
    if (!targetScreen) {
        emit recordingError(tr("No screen available for region selection."));
        return;
    }

    auto* selector = createRegionSelector();
    if (!selector) {
        return;
    }

    selector->setGeometry(targetScreen->geometry());
    selector->initializeWithRegion(targetScreen, region);
    selector->show();
    raiseWindowAboveMenuBar(selector);
    selector->activateWindow();
    selector->raise();
}

void RecordingManager::startFullScreenRecording(QScreen* screen)
{
    // Check if already active
    if (m_state == State::Recording || m_state == State::Paused ||
        m_state == State::Encoding || m_state == State::Preparing ||
        m_state == State::Countdown || m_initFuture.isRunning()) {
        return;
    }

    if (m_regionSelector && m_regionSelector->isVisible()) {
        return;
    }

    // Use provided screen or determine from cursor position.
    QScreen* targetScreen = resolveTargetScreen(screen);
    if (!targetScreen) {
        emit recordingError(tr("No screen available for recording."));
        setState(State::Idle);
        return;
    }

    // Use the entire screen as the recording region
    m_recordingRegion = targetScreen->geometry();
    m_targetScreen = targetScreen;

    loadAndValidateFrameRate();
    resetPauseTracking();

    // Skip region selection and start recording directly
    startFrameCapture();
}

void RecordingManager::onRegionSelected(const QRect &region, QScreen *screen)
{
    m_recordingRegion = region;
    m_targetScreen = screen;

    loadAndValidateFrameRate();
    resetPauseTracking();

    // Start recording immediately after selection
    startFrameCapture();
}

void RecordingManager::onRegionCancelledWithRegion(const QRect &region, QScreen *screen)
{
    setState(State::Idle);
    emit selectionCancelledWithRegion(region, screen);
}

void RecordingManager::onRegionCancelled()
{
    setState(State::Idle);
}

void RecordingManager::startFrameCapture()
{
    if (m_initFuture.isRunning()) {
        qWarning() << "RecordingManager: Initialization already in progress";
        return;
    }

    if (!isScreenAvailable(m_targetScreen.data())) {
        m_targetScreen = nullptr;
        setState(State::Idle);
        emit recordingError(tr("Recording screen is no longer available."));
        return;
    }

    // Safety check: clean up any existing encoding worker
    if (m_encodingWorker) {
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
    // Show boundary overlay immediately (UI stays responsive)
    // Clean up any existing overlay first to prevent resource leak
    if (m_boundaryOverlay) {
        m_boundaryOverlay->close();
        m_boundaryOverlay = nullptr;
    }

    m_boundaryOverlay = new RecordingBoundaryOverlay();
    m_boundaryOverlay->setAttribute(Qt::WA_DeleteOnClose);
    m_boundaryOverlay->setRegion(m_recordingRegion);
    m_boundaryOverlay->show();
    raiseWindowAboveMenuBar(m_boundaryOverlay);
    setWindowClickThrough(m_boundaryOverlay, true);

    // Show control bar in preparing state
    // Clean up any existing control bar first to prevent resource leak
    if (m_controlBar) {
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

    // Show in preparing state (buttons disabled until ready)
    m_controlBar->setPreparing(true);

    m_controlBar->show();
    raiseWindowAboveMenuBar(m_controlBar);
    // Position after show() to avoid Qt/macOS adjusting the position
    m_controlBar->positionNear(m_recordingRegion);

    // Start async initialization
    beginAsyncInitialization();
}

void RecordingManager::beginAsyncInitialization()
{
    if (m_initFuture.isRunning()) {
        qWarning() << "RecordingManager: beginAsyncInitialization called while future is still running";
        return;
    }

    QScreen* targetScreen = m_targetScreen.data();
    if (!isScreenAvailable(targetScreen)) {
        qWarning() << "RecordingManager: Target screen is no longer available before initialization";
        m_targetScreen = nullptr;
        stopFrameCapture();
        setState(State::Idle);
        emit recordingError(tr("Recording screen is no longer available."));
        return;
    }

    // Load settings for initialization config
    auto settings = SnapTray::getSettings();
    int formatInt = settings.value("recording/outputFormat", 0).toInt();
    bool showPreview = settings.value("recording/showPreview", true).toBool();

    auto outputFormat = EncoderFactory::Format::MP4;
    if (formatInt == 1) outputFormat = EncoderFactory::Format::GIF;
    else if (formatInt == 2) outputFormat = EncoderFactory::Format::WebP;

    // When preview is enabled, always record as MP4 (preview player only supports MP4).
    // The user's preferred format is pre-selected in the preview's format widget.
    auto recordingFormat = showPreview ? EncoderFactory::Format::MP4 : outputFormat;
    m_preferredFormat = formatInt;

    m_audioEnabled = settings.value("recording/audioEnabled", false).toBool();
    m_audioSource = settings.value("recording/audioSource", 0).toInt();
    m_audioDevice = settings.value("recording/audioDevice").toString();

    // Probe actual input format so encoder format matches captured PCM data.
    int audioSampleRate = kDefaultAudioSampleRate;
    int audioChannels = kDefaultAudioChannels;
    int audioBitsPerSample = kDefaultAudioBitsPerSample;
    const bool shouldConfigureAudio = m_audioEnabled && (recordingFormat == EncoderFactory::Format::MP4);
    if (shouldConfigureAudio) {
        std::unique_ptr<IAudioCaptureEngine> probeEngine(IAudioCaptureEngine::createBestEngine(nullptr));
        if (!probeEngine) {
            qWarning() << "RecordingManager: Failed to probe audio format. Using defaults.";
        } else {
            const auto source = resolveAudioSource(m_audioSource);
            if (!probeEngine->setAudioSource(source)) {
                qWarning() << "RecordingManager: Failed to apply configured audio source for format probing";
            }
            if (!m_audioDevice.isEmpty() && !probeEngine->setDevice(m_audioDevice)) {
                qWarning() << "RecordingManager: Failed to apply configured audio device for format probing";
            }

            const auto probedFormat = probeEngine->audioFormat();
            if (probedFormat.sampleRate > 0
                && probedFormat.channels > 0
                && probedFormat.bitsPerSample > 0) {
                audioSampleRate = probedFormat.sampleRate;
                audioChannels = probedFormat.channels;
                audioBitsPerSample = probedFormat.bitsPerSample;
            } else {
                qWarning() << "RecordingManager: Audio format probe returned invalid values. Using defaults.";
            }
        }
    }

    // Use physical pixel size for Retina/HiDPI displays
    qreal scale = CoordinateHelper::getDevicePixelRatio(targetScreen);
    QSize physicalSize = CoordinateHelper::toEvenPhysicalSize(m_recordingRegion.size(), scale);

    // Create initialization config
    RecordingInitTask::Config config;
    config.region = m_recordingRegion;
    config.screen = targetScreen;
    config.frameRate = m_frameRate;
    config.audioEnabled = shouldConfigureAudio;
    config.audioSource = m_audioSource;
    config.audioDevice = m_audioDevice;
    config.audioSampleRate = audioSampleRate;
    config.audioChannels = audioChannels;
    config.audioBitsPerSample = audioBitsPerSample;
    config.outputPath = generateOutputPath();
    config.useNativeEncoder = true;
    config.outputFormat = recordingFormat;
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

    const quint64 generation = ++m_initGeneration;

    // Create the init task
    auto taskDeleter = [](RecordingInitTask *task) {
        if (task) {
            task->deleteLater();
        }
    };
    m_initTask = QSharedPointer<RecordingInitTask>(new RecordingInitTask(config, nullptr), taskDeleter);

    // Connect progress signal for UI feedback
    connect(m_initTask.data(), &RecordingInitTask::progress,
            this, [this, generation](const QString &step) {
        if (generation != m_initGeneration || m_state != State::Preparing) {
            return;
        }

        if (m_controlBar) {
            m_controlBar->setPreparingStatus(step);
        }
    }, Qt::QueuedConnection);

    // Run initialization in background thread
    // Use QPointer to guard against this being deleted while task runs
    QPointer<RecordingManager> guard(this);
    QFutureWatcher<void> *watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [guard, watcher, task = m_initTask, generation]() {
        if (guard) {
            guard->onInitializationComplete(task, generation);
        } else if (task) {
            task->discardResult();
        }
        watcher->deleteLater();
    });

    // Capture task shared pointer to ensure lifetime is valid in background thread.
    auto task = m_initTask;
    m_initFuture = QtConcurrent::run([task]() {
        if (task) {
            task->run();
        }
    });
    watcher->setFuture(m_initFuture);
}

void RecordingManager::onInitializationComplete(const QSharedPointer<RecordingInitTask> &task,
                                                quint64 generation)
{
    if (!task) {
        return;
    }

    const bool isCurrentTask = m_initTask && (m_initTask.data() == task.data());

    // Ignore stale completion callbacks from cancelled/superseded generations.
    if (generation != m_initGeneration) {
        task->discardResult();
        if (isCurrentTask) {
            m_initTask.clear();
        }
        return;
    }

    // Safety check: ensure we're still in Preparing state
    if (m_state != State::Preparing) {
        task->discardResult();
        if (isCurrentTask) {
            m_initTask.clear();
        }
        return;
    }

    // Safety check: ensure the completion callback is for the active task
    if (!isCurrentTask) {
        task->discardResult();
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
    RecordingInitTask::Result &result = task->result();

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

        m_initTask.clear();

        setState(State::Idle);
        return;
    }

    // Take ownership of created resources (already moved to main thread in worker)
    m_captureEngine.reset(result.releaseCaptureEngine());
    if (m_captureEngine) {
        // IMPORTANT: Do NOT set parent to this, as we use std::unique_ptr for ownership
        
        // Forward capture engine warnings to log
        connect(m_captureEngine.get(), &ICaptureEngine::warning,
                this, [](const QString &msg) {
            qWarning() << "RecordingManager: Capture engine warning:" << msg;
        });

        // Connect capture engine error signal
        connect(m_captureEngine.get(), &ICaptureEngine::error,
                this, [this](const QString &msg) {
            stopRecording();
            emit recordingError(msg);
        });
    }

    m_usingNativeEncoder = result.usingNativeEncoder;

    // Create encoding worker to offload encoding from main thread
    m_encodingWorker = std::make_unique<EncodingWorker>(nullptr);

    // Transfer encoder ownership to worker
    const bool hasNativeEncoder = (result.nativeEncoder != nullptr);
    const bool hasGifEncoder = (result.gifEncoder != nullptr);
    const bool hasWebPEncoder = (result.webpEncoder != nullptr);

    if (hasNativeEncoder) {
        m_encodingWorker->setVideoEncoder(result.releaseNativeEncoder());
        m_encodingWorker->setEncoderType(EncodingWorker::EncoderType::Video);
    }
    if (hasGifEncoder) {
        m_encodingWorker->setGifEncoder(result.releaseGifEncoder());
        if (!hasNativeEncoder) {
            m_encodingWorker->setEncoderType(EncodingWorker::EncoderType::Gif);
        }
    }
    if (hasWebPEncoder) {
        m_encodingWorker->setWebPEncoder(result.releaseWebpEncoder());
        if (!hasNativeEncoder) {
            m_encodingWorker->setEncoderType(EncodingWorker::EncoderType::WebP);
        }
    }

    // Connect worker signals
    connect(m_encodingWorker.get(), &EncodingWorker::finished,
            this, &RecordingManager::onEncodingFinished);
    connect(m_encodingWorker.get(), &EncodingWorker::error,
            this, &RecordingManager::onEncodingError);

    // Start the encoding worker
    if (!m_encodingWorker->start()) {
        qWarning() << "RecordingManager: Failed to start encoding worker";
        emit recordingError(tr("Failed to start encoding worker"));
        m_encodingWorker.reset();
        m_initTask.clear();
        setState(State::Idle);
        return;
    }

    // Create audio engine on main thread (macOS audio APIs require main thread)
    // Audio engine was NOT created in worker thread due to thread affinity requirements
    if (m_audioEnabled) {
        // Pass nullptr as parent since we use custom deleter with unique_ptr
        m_audioEngine.reset(IAudioCaptureEngine::createBestEngine(nullptr));
        if (m_audioEngine) {
            // Set audio source
            const auto source = resolveAudioSource(m_audioSource);
            m_audioEngine->setAudioSource(source);

            if (!m_audioDevice.isEmpty()) {
                m_audioEngine->setDevice(m_audioDevice);
            }

            // Connect audio error/warning signals
            connect(m_audioEngine.get(), &IAudioCaptureEngine::error,
                    this, [this](const QString &msg) {
                qWarning() << "RecordingManager: Audio error:" << msg;
            });
            connect(m_audioEngine.get(), &IAudioCaptureEngine::warning,
                    this, [](const QString &msg) {
                qWarning() << "RecordingManager: Audio warning:" << msg;
            });
        } else {
            qWarning() << "RecordingManager: Failed to create audio engine";
            m_audioEnabled = false;
        }
    }

    // Connect audio capture to encoding worker
    if (m_audioEngine && m_encodingWorker) {
        bool useNativeAudio = m_usingNativeEncoder;

        if (useNativeAudio) {
            // Connect audio data directly to encoding worker (bypasses Main Thread)
            // EncodingWorker::writeAudioSamples is thread-safe (uses internal queue)
            connect(m_audioEngine.get(), &IAudioCaptureEngine::audioDataReady,
                    m_encodingWorker.get(), &EncodingWorker::writeAudioSamples,
                    Qt::DirectConnection);
            // NOTE: Audio capture is started in startRecordingAfterCountdown()
            // to synchronize timestamps with video
        } else {
            // GIF format does not support audio - this shouldn't happen as
            // audio is disabled for GIF in beginAsyncInitialization()
            qWarning() << "RecordingManager: Audio not supported for current format";
            cleanupAudio();
        }
    }

    // Update control bar to show ready state
    if (m_controlBar) {
        m_controlBar->setPreparing(false);
        m_controlBar->setAudioEnabled(m_audioEngine != nullptr && m_audioEnabled);
    }

    // Clean up init task
    m_initTask.clear();

    // Load countdown settings
    auto settings = SnapTray::getSettings();
    m_countdownEnabled = settings.value("recording/countdownEnabled", true).toBool();
    m_countdownSeconds = settings.value("recording/countdownSeconds", 3).toInt();

    // Start countdown if enabled, otherwise go directly to recording
    if (m_countdownEnabled && m_countdownSeconds > 0) {
        startCountdown();
    } else {
        startRecordingAfterCountdown();
    }
}

void RecordingManager::startCountdown()
{
    if (!isScreenAvailable(m_targetScreen.data())) {
        qWarning() << "RecordingManager: Target screen unavailable before countdown";
        m_targetScreen = nullptr;
        emit recordingError(tr("Recording screen is no longer available."));
        cancelRecording();
        return;
    }

    setState(State::Countdown);

    m_countdownOverlay = new CountdownOverlay();
    m_countdownOverlay->setAttribute(Qt::WA_DeleteOnClose);
    m_countdownOverlay->setRegion(m_recordingRegion, m_targetScreen.data());
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

    if (!isScreenAvailable(m_targetScreen.data())) {
        qWarning() << "RecordingManager: Target screen unavailable after countdown";
        m_targetScreen = nullptr;
        emit recordingError(tr("Recording screen is no longer available."));
        cancelRecording();
        return;
    }

    // Initialize state and counters
    m_elapsedTimer.start();
    setState(State::Recording);
    m_frameCount = 0;

    // Start audio capture here to synchronize with video timer
    if (m_audioEngine) {
        if (!m_audioEngine->start()) {
            qWarning() << "RecordingManager: Failed to start audio capture";
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
    QTimer::singleShot(kOverlayRenderDelay, this, [this]() {
        startCaptureTimers();
    });

    emit recordingStarted();
}

void RecordingManager::onCountdownCancelled()
{
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
        return;
    }

    // Safety check: ensure timers don't already exist (shouldn't happen, but be defensive)
    if (m_captureTimer) {
        m_captureTimer->stop();
        m_captureTimer.reset();
    }
    if (m_durationTimer) {
        m_durationTimer->stop();
        m_durationTimer.reset();
    }

    // Start frame capture timer
    // No parent, managed by unique_ptr
    m_captureTimer = std::make_unique<QTimer>(nullptr);
    connect(m_captureTimer.get(), &QTimer::timeout, this, &RecordingManager::captureFrame);
    m_captureTimer->start(1000 / m_frameRate);

    // Start duration timer (update UI at regular intervals)
    // No parent, managed by unique_ptr
    m_durationTimer = std::make_unique<QTimer>(nullptr);
    connect(m_durationTimer.get(), &QTimer::timeout, this, &RecordingManager::updateDuration);
    m_durationTimer->start(kDurationUpdate);
}

void RecordingManager::captureFrame()
{
    if (m_state != State::Recording) {
        return;
    }

    if (!isScreenAvailable(m_targetScreen.data())) {
        qWarning() << "RecordingManager: Target screen disconnected during capture";
        m_targetScreen = nullptr;
        emit recordingError(tr("Recording screen disconnected during capture."));
        cancelRecording();
        return;
    }

    // Store local copies (raw pointers) to avoid race conditions
    // where unique_ptrs might be reset during execution
    ICaptureEngine *captureEngine = m_captureEngine.get();
    EncodingWorker *encodingWorker = m_encodingWorker.get();

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

        // Enqueue frame for encoding (non-blocking)
        // Watermark is applied by the worker thread
        EncodingWorker::FrameData frameData{frame, elapsedMs};
        encodingWorker->enqueueFrame(frameData);

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

    // Cancel countdown overlay if active
    if (m_countdownOverlay) {
        m_countdownOverlay->close();
        m_countdownOverlay = nullptr;
    }

    // Cancel any pending initialization
    if (m_initTask || m_initFuture.isRunning()) {
        ++m_initGeneration;
    }
    if (m_initTask) {
        m_initTask->cancel();
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
    // Stop timers
    if (m_captureTimer) {
        m_captureTimer->stop();
        m_captureTimer.reset();
    }

    if (m_durationTimer) {
        m_durationTimer->stop();
        m_durationTimer.reset();
    }

    // Stop audio capture and finalize audio file
    // Custom deleter handles disconnect + stop() + deleteLater()
    if (m_audioEngine) {
        m_audioEngine.reset();
    }

    // Stop capture engine
    // Custom deleter handles disconnect + stop() + deleteLater()
    m_captureEngine.reset();

    // Close UI overlays
    if (m_boundaryOverlay) {
        m_boundaryOverlay->close();
        m_boundaryOverlay = nullptr;
    }

    if (m_controlBar) {
        m_controlBar->close();
        m_controlBar = nullptr;
    }
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
    ResourceCleanupHelper::removeTempFile(m_tempAudioPath);
}

void RecordingManager::onEncodingFinished(bool success, const QString &outputPath)
{
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
            int preferredFormat = m_preferredFormat;
            QMetaObject::invokeMethod(this, [this, tempPath, preferredFormat]() {
                m_tempVideoPath = tempPath;
                setState(State::Previewing);
                emit previewRequested(tempPath, preferredFormat);
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
}

void RecordingManager::triggerSaveDialog(const QString &videoPath)
{
    showSaveDialog(videoPath);
}

void RecordingManager::showSaveDialog(const QString &tempOutputPath)
{
    // Get settings from FileSettingsManager
    auto& fileSettings = FileSettingsManager::instance();

    // Check auto-save setting
    bool autoSave = fileSettings.loadAutoSaveRecordings();
    QString outputDir = fileSettings.loadRecordingPath();

    // Get extension from current file
    QFileInfo fileInfo(tempOutputPath);
    QString extension = fileInfo.suffix();

    int monitorIndex = -1;
    if (m_targetScreen) {
        monitorIndex = QGuiApplication::screens().indexOf(m_targetScreen.data());
    }

    FilenameTemplateEngine::Context context;
    context.type = QStringLiteral("Recording");
    context.prefix = fileSettings.loadFilenamePrefix();
    context.width = m_recordingRegion.width();
    context.height = m_recordingRegion.height();
    context.monitor = monitorIndex >= 0 ? QString::number(monitorIndex) : QStringLiteral("unknown");
    context.windowTitle = QString();
    context.appName = QString();
    context.regionIndex = -1;
    context.ext = extension;
    context.dateFormat = fileSettings.loadDateFormat();
    context.outputDir = outputDir;
    const QString templateValue = fileSettings.loadFilenameTemplate();

    if (autoSave) {
        QString renderError;
        QString savePath = FilenameTemplateEngine::buildUniqueFilePath(
            outputDir, templateValue, context, 100, &renderError);

        // Try rename first. If path collides between render and rename, retry with a new unique path.
        const int maxAttempts = 100;
        int attempt = 0;
        while (attempt < maxAttempts) {
            if (QFile::rename(tempOutputPath, savePath)) {
                syncFile(savePath);
                emit recordingStopped(savePath);
                return;
            }

            if (QFile::exists(savePath)) {
                savePath = FilenameTemplateEngine::buildUniqueFilePath(
                    outputDir, templateValue, context, 100, &renderError);
                ++attempt;
                continue;
            }

            // Rename failed for non-collision reason.
            break;
        }

        // Try copy + delete if rename fails (cross-filesystem)
        if (QFile::exists(savePath)) {
            savePath = FilenameTemplateEngine::buildUniqueFilePath(
                outputDir, templateValue, context, 100, &renderError);
        }
        if (QFile::copy(tempOutputPath, savePath)) {
            QFile::remove(tempOutputPath);
            syncFile(savePath);
            emit recordingStopped(savePath);
            return;
        }

        if (!renderError.isEmpty()) {
            qWarning() << "RecordingManager: template warning:" << renderError;
        }
        // If auto-save fails, fall through to show dialog
        qWarning() << "RecordingManager: Auto-save failed, showing save dialog";
    }

    // Show save dialog
    QString filter;
    if (extension == "gif") filter = "GIF Files (*.gif)";
    else if (extension == "webp") filter = "WebP Files (*.webp)";
    else filter = "MP4 Files (*.mp4)";
    const QString defaultFileName = FilenameTemplateEngine::buildUniqueFilePath(
        outputDir, templateValue, context, 1);

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
                emit recordingStopped(savePath);
            } else {
                // Try copy + delete if rename fails (cross-filesystem)
                if (QFile::copy(tempOutputPath, savePath)) {
                    QFile::remove(tempOutputPath);
                    syncFile(savePath);
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
    bool showPreview = settings.value("recording/showPreview", true).toBool();

    // When preview is enabled, always record as MP4 (preview player only supports MP4)
    QString extension;
    if (showPreview) {
        extension = "mp4";
    } else if (formatInt == 1) {
        extension = "gif";
    } else if (formatInt == 2) {
        extension = "webp";
    } else {
        extension = "mp4";
    }

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
    filters << "SnapTray_Recording_*.mp4" << "SnapTray_Recording_*.gif" << "SnapTray_Recording_*.webp";

    QFileInfoList files = dir.entryInfoList(filters, QDir::Files);
    QDateTime threshold = QDateTime::currentDateTime().addDays(-1);

    int cleanedCount = 0;
    for (const QFileInfo &file : files) {
        if (file.lastModified() < threshold) {
            if (QFile::remove(file.absoluteFilePath())) {
                cleanedCount++;
            } else {
                qWarning() << "RecordingManager: Failed to clean up temp file:" << file.fileName();
            }
        }
    }

    if (cleanedCount > 0) {
        qDebug() << "RecordingManager: Cleaned up" << cleanedCount << "stale temp files";
    }
}
