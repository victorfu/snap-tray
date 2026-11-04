#include "platform/CaptureExclusionPolicy.h"
#include "platform/PlatformCapabilities.h"
#include "RecordingManager.h"
#include "recording/RecordingProject.h"
#include "recording/ScreenSourceService.h"
#include "recording/RecordingFileUtils.h"
#include "recording/WindowTimelineSidecar.h"
#include "recording/CursorTimeline.h"
#include "recording/KeyTimeline.h"
#include "qml/QmlRecordingControlBar.h"
#include "qml/QmlRecordingBoundary.h"
#include "recording/RecordingArea.h"
#include "hotkey/HotkeyManager.h"
#include "RecordingInitTask.h"
#include "RecordingRegionNormalizer.h"
#include "qml/QmlCountdownOverlay.h"
#include "encoding/NativeGifEncoder.h"
#include "encoding/WebPAnimEncoder.h"
#include "encoding/EncodingWorker.h"
#include "IVideoEncoder.h"
#include "encoding/EncoderFactory.h"
#include "capture/ICaptureEngine.h"
#include "capture/IAudioCaptureEngine.h"
#include "capture/TimestampedPcmMixer.h"
#include "utils/ResourceCleanupHelper.h"
#include "utils/CoordinateHelper.h"
#include "utils/FilenameTemplateEngine.h"
#include "utils/NativeFileDialogUtils.h"
#include "settings/FileSettingsManager.h"
#include "settings/RecordingSettingsManager.h"

#include <QGuiApplication>
#include <QCoreApplication>
#include <QCursor>
#include <QScreen>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QStorageInfo>
#include "video/IVideoTranscoder.h"
#include <QImageReader>
#include <QLockFile>
#include <QLocale>
#include "encoding/IntermediateQuality.h"
#include <QFile>
#include <QMutexLocker>
#include <QScopeGuard>
#include <QUuid>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QThread>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#endif

// Local constants for recording (moved from Constants.h)
namespace {

// Display text for a recording hotkey that is active, or empty.
QString registeredHotkeyText(SnapTray::HotkeyAction action)
{
    const auto config = SnapTray::HotkeyManager::instance().getConfig(action);
    if (!config.enabled || config.status != SnapTray::HotkeyStatus::Registered) {
        return {};
    }
    return SnapTray::HotkeyManager::formatKeySequence(config.keySequence);
}

constexpr int kDefaultFrameRate = 30;
constexpr int kMaxFrameRate = 120;
constexpr int kDefaultAudioSampleRate = 48000;
constexpr int kDefaultAudioChannels = 2;
constexpr int kDefaultAudioBitsPerSample = 16;
constexpr int kOverlayRenderDelay = 100;  // ms, delay before capture to allow overlay render
constexpr int kDurationUpdate = 100;      // ms, recording duration UI update interval
constexpr int kDiskCheckIntervalMs = 2000;
constexpr int kMicrophoneSource = 0;
constexpr int kSystemAudioSource = 1;
constexpr int kBothAudioSources = 2;
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

QRect normalizeRecordingRegion(const QRect& region, const CaptureScreenInfo& screenInfo)
{
    return normalizeToEvenPhysicalRegion(
        region,
        screenInfo.geometry,
        screenInfo.devicePixelRatio,
        screenInfo.physicalGeometry);
}

QRect normalizeRecordingRegionForScreen(const QRect& region, QScreen* screen)
{
    if (!screen) {
        return region;
    }

    return normalizeRecordingRegion(region, CaptureScreenInfo::fromScreen(screen));
}

QRect physicalRecordingRegion(const QRect& logicalRegion,
                              const CaptureScreenInfo& screenInfo)
{
    if (!screenInfo.physicalGeometry.isEmpty()) {
        return CoordinateHelper::toPhysicalScreenRect(
            logicalRegion,
            screenInfo.geometry,
            screenInfo.physicalGeometry,
            screenInfo.devicePixelRatio);
    }

    return QRect(QPoint(0, 0), CoordinateHelper::toPhysical(
        logicalRegion.size(), screenInfo.devicePixelRatio));
}

bool isEvenPhysicalSize(const QSize& physicalSize)
{
    return (physicalSize.width() % 2 == 0) && (physicalSize.height() % 2 == 0);
}

IAudioCaptureEngine::AudioSource resolveAudioSource(int audioSourceSetting)
{
    switch (audioSourceSetting) {
    case kSystemAudioSource: return IAudioCaptureEngine::AudioSource::SystemAudio;
    case kBothAudioSources: return IAudioCaptureEngine::AudioSource::Both;
    default: return IAudioCaptureEngine::AudioSource::Microphone;
    }
}

bool recordingSupportsAudioCapture(int outputFormat, bool showPreview)
{
    return SnapTray::recordingCapturesAudio(outputFormat, showPreview);
}

bool audioSourceUsesInputDevice(int audioSourceSetting)
{
    return audioSourceSetting != kSystemAudioSource;
}

QString forceFileExtension(QString filePath, const QString& extension)
{
    if (filePath.isEmpty() || extension.isEmpty()) {
        return filePath;
    }

    const QString normalizedExtension = extension.startsWith(QLatin1Char('.'))
        ? extension.mid(1)
        : extension;
    if (normalizedExtension.isEmpty()) {
        return filePath;
    }

    const int slashIndex = qMax(filePath.lastIndexOf(QLatin1Char('/')),
                                filePath.lastIndexOf(QLatin1Char('\\')));
    const int dotIndex = filePath.lastIndexOf(QLatin1Char('.'));
    if (dotIndex > slashIndex) {
        return filePath.left(dotIndex + 1) + normalizedExtension;
    }

    return filePath + QLatin1Char('.') + normalizedExtension;
}

QString configuredAudioInputDeviceOrDefault(IAudioCaptureEngine& engine,
                                            int audioSourceSetting,
                                            const QString& configuredDeviceId)
{
    if (!audioSourceUsesInputDevice(audioSourceSetting) || configuredDeviceId.isEmpty()) {
        return QString();
    }

    const auto devices = engine.availableInputDevices();
    for (const auto& device : devices) {
        if (device.id == configuredDeviceId) {
            return configuredDeviceId;
        }
    }

    qWarning() << "RecordingManager: Configured audio input device not found, using default input instead";
    return QString();
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

bool RecordingManager::chooseIntermediateQuality(const QString& outputDirectory, const QSize& frameSize)
{
    const qint64 freeBytes = m_freeBytesForPath ? m_freeBytesForPath(outputDirectory) : -1;
    if (SnapTray::IntermediateQuality::hasRoomForIntermediate(freeBytes, frameSize, m_frameRate)) {
        return true;
    }
    qWarning() << "RecordingManager: free space" << freeBytes << "bytes in" << outputDirectory
               << "is below" << SnapTray::IntermediateQuality::kMinimumRecordingMinutes
               << "minutes of intermediate recording; using the selected quality";
    emit recordingWarning(tr("Not enough free disk space for high-quality recording. Recording at the selected quality instead."));
    return false;
}

bool RecordingManager::decideIntermediateQuality(bool showPreview, const QString& outputDirectory,
                                                 const QSize& frameSize)
{
    m_recordedAsIntermediate = showPreview && chooseIntermediateQuality(outputDirectory, frameSize);
    return m_recordedAsIntermediate;
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
    , m_audioSource(kMicrophoneSource)
    , m_countdownEnabled(true)
    , m_countdownSeconds(3)
{
    m_isRecordingPlayable = [](const QString& path) {
        const auto transcoder = IVideoTranscoder::create();
        return transcoder && transcoder->probe(path).valid;
    };
    m_freeBytesForPath = [](const QString& path) { return QStorageInfo(path).bytesAvailable(); };
    m_captureControlsMayBeVisible = [] {
        return SnapTray::requiresVisibleRecordingControls(QOperatingSystemVersion::current());
    };
    m_requiresDirectMp4Recording = [] {
        return SnapTray::currentPlatformCapabilities().recordingDirectMp4Only;
    };

    cleanupStaleTempFiles();
}

RecordingManager::~RecordingManager()
{
    ++m_startGeneration;
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
        // The area's frame (#194) follows the recording, amber while paused.
        if (m_areaBoundary) {
            if (m_state == State::Recording) m_areaBoundary->setBorderMode(SnapTray::QmlRecordingBoundary::BorderMode::Recording);
            else if (m_state == State::Paused) m_areaBoundary->setBorderMode(SnapTray::QmlRecordingBoundary::BorderMode::Paused);
            else if (m_state != State::Preparing && m_state != State::Countdown) closeAreaBoundary();
        }
        if (m_state == State::Idle) {
            setAudioStatus({});
        }
        emit stateChanged(m_state);
    }
}

void RecordingManager::showAreaBoundary()
{
    closeAreaBoundary();
    if (m_preselectedArea.isEmpty() || !m_showAreaBoundary) return;
    const bool editMode = m_startSettings.showPreview;
    // Edit mode records the whole screen: where the capture cannot leave the
    // frame out (X11), it would be in the video around the area and show
    // when the crop changes. No frame there (PR #205 review).
    const bool canExclude = m_captureExcludesOverlays ? m_captureExcludesOverlays()
                                                      : !SnapTray::currentPlatformCapabilities().recordingControlsInTray;
    if (editMode && !canExclude) return;
    m_areaBoundary = new SnapTray::QmlRecordingBoundary(this);
    // Quick mode: around what is recorded, the area grown to even pixels,
    // so the frame is always outside the capture (PR #205 review).
    m_areaBoundary->setRegion(editMode ? m_preselectedArea : m_recordingRegion);
    m_areaBoundary->show();
    m_areaBoundary->setExcludedFromCapture(true);
}

void RecordingManager::closeAreaBoundary()
{
    if (!m_areaBoundary) return;
    m_areaBoundary->close();
    m_areaBoundary->deleteLater();
    m_areaBoundary = nullptr;
}

void RecordingManager::setAudioStatus(const SnapTray::RecordingAudioStatus& status)
{
    if (m_audioStatus == status) {
        return;
    }
    m_audioStatus = status;
    emit audioStatusChanged();
}

void RecordingManager::transitionAudioSources(SnapTray::AudioSourceState from,
                                              SnapTray::AudioSourceState to)
{
    auto status = m_audioStatus;
    for (auto* state : {&status.microphone, &status.systemAudio}) {
        if (*state == from) {
            *state = to;
        }
    }
    setAudioStatus(status);
}

bool RecordingManager::isActive() const
{
    return isRecording() || isPaused() || isPreviewing() ||
           (m_state == State::Preparing) || (m_state == State::Countdown) ||
           (m_state == State::Encoding) ||
           m_initFuture.isRunning();
}

bool RecordingManager::blocksCapture() const
{
    // Screenshots, pins and the canvas can run while the preview window is
    // open: it is an ordinary window, not an in-flight recording.
    return isRecording() || isPaused() ||
           (m_state == State::Preparing) || (m_state == State::Countdown) ||
           (m_state == State::Encoding) ||
           m_initFuture.isRunning();
}

bool RecordingManager::isRecording() const
{
    return m_state == State::Recording;
}

bool RecordingManager::isPaused() const
{
    return m_state == State::Paused;
}

bool RecordingManager::isPreviewing() const
{
    return m_state == State::Previewing;
}

QString RecordingManager::activeScreenSummary() const
{
    switch (m_state) {
    case State::Preparing:
    case State::Countdown:
    case State::Recording:
    case State::Paused:
        break;
    case State::Idle:
    case State::Encoding:
    case State::Previewing:
        return {};
    }

    QScreen* screen = m_targetScreen.data();
    if (!isScreenAvailable(screen)) {
        return {};
    }

    const QList<QScreen*> screens = QGuiApplication::screens();
    const int index = screens.indexOf(screen);
    return QStringLiteral("%1 (%2)")
        .arg(SnapTray::ScreenSourceService::displayName(screen, qMax(index, 0)))
        .arg(SnapTray::ScreenSourceService::resolutionText(
            screen->geometry(),
            screen->devicePixelRatio() > 0.0 ? screen->devicePixelRatio() : 1.0));
}

void RecordingManager::startScreenRecording(QScreen* screen, const QRect& area)
{
    if (m_state == State::Recording || m_state == State::Paused ||
        m_state == State::Encoding || m_state == State::Preparing ||
        m_state == State::Countdown || m_initFuture.isRunning()) {
        return;
    }

    QScreen* targetScreen = resolveTargetScreen(screen);
    if (!targetScreen) {
        emit recordingError(tr("No screen available for recording."));
        setState(State::Idle);
        return;
    }

    m_recordingRegion = normalizeRecordingRegionForScreen(targetScreen->geometry(), targetScreen);
    if (m_recordingRegion.isEmpty()) {
        emit recordingError(tr("Failed to normalize recording region."));
        setState(State::Idle);
        return;
    }
    m_targetScreen = targetScreen;
    // An area chosen before recording (#194), kept on this screen.
    m_preselectedArea = area.isEmpty()
        ? QRect()
        : SnapTray::RecordingArea::clampToScreen(area, targetScreen->geometry(), targetScreen->devicePixelRatio());
    // The whole screen chosen ("Full Screen") is no area at all.
    if (m_preselectedArea == targetScreen->geometry()) m_preselectedArea = {};
    m_pendingInitialCrop = {};

    loadAndValidateFrameRate();
    resetPauseTracking();

    startFrameCapture();
}

void RecordingManager::loadAndValidateFrameRate()
{
    auto& recMgr = RecordingSettingsManager::instance();
    m_frameRate = recMgr.frameRate();
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
    m_mediaClockPaused = false;
}

bool RecordingManager::shouldUseDedicatedEncodingThread(bool hasNativeEncoder) const
{
    Q_UNUSED(hasNativeEncoder);
    return true;
}

void RecordingManager::teardownEncodingWorker(bool abortEncoding)
{
    {
        QMutexLocker lock(&m_durationMutex);
        m_acceptAsyncFrames = false;
        // The recorded length, for the recovery manifest (a finished
        // animation's duration comes from it, #172 review).
        if (m_elapsedTimer.isValid()) {
            const qint64 raw = m_elapsedTimer.elapsed();
            const qint64 currentPause = m_state == State::Paused ? raw - m_pauseStartTime : 0;
            m_session.recordedMs = qMax<qint64>(0, raw - m_pausedDuration - currentPause);
        }
    }
    // Invalidate any queued callbacks from previous encoding workers.
    ++m_encodingGeneration;

    EncodingWorker* worker = m_encodingWorker.get();
    if (worker && abortEncoding) {
        worker->stop();
    }

    if (worker
        && m_encodingThread
        && worker->thread() == m_encodingThread.get()
        && m_encodingThread->isRunning()) {
        QCoreApplication* app = QCoreApplication::instance();
        if (!app) {
            qWarning() << "RecordingManager: QCoreApplication is unavailable during teardown";
        } else {
            QThread* mainThread = app->thread();
            worker->moveOwnedEncodersToThread(mainThread);
            const bool moved = QMetaObject::invokeMethod(worker, [worker, mainThread]() {
                worker->moveToThread(mainThread);
            }, Qt::BlockingQueuedConnection);
            if (!moved) {
                qWarning() << "RecordingManager: Failed to move EncodingWorker back to main thread";
            }
        }
    }

    if (m_encodingThread) {
        m_encodingThread->requestInterruption();
        m_encodingThread->quit();
        if (!m_encodingThread->wait(5000)) {
            qWarning() << "RecordingManager: Encoding thread exit timed out; detaching"
                       << "thread object to avoid blocking teardown";
            QThread* detachedThread = m_encodingThread.release();
            QObject::connect(detachedThread, &QThread::finished,
                             detachedThread, &QObject::deleteLater,
                             Qt::QueuedConnection);
        } else {
            m_encodingThread.reset();
        }
    }

    m_encodingWorker.reset();
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
    if (m_encodingWorker || m_encodingThread) {
        teardownEncodingWorker(true);
    }
    m_usingNativeEncoder = false;

    // Set state to Preparing and show UI immediately
    const quint64 startGeneration = m_startGeneration + 1;
    initializeStartState();
    if (m_state != State::Preparing || startGeneration != m_startGeneration) return;
    // Quick mode records the chosen area itself (#194): there is no preview
    // to crop it in. Edit mode keeps the full screen and crops afterwards.
    if (!m_preselectedArea.isEmpty() && !m_startSettings.showPreview) {
        const QRect region = normalizeRecordingRegionForScreen(m_preselectedArea, m_targetScreen.data());
        if (!region.isEmpty()) m_recordingRegion = region;
    }
    showAreaBoundary();
    // Show control bar in preparing state
    // Clean up any existing control bar first to prevent resource leak
    if (m_controlBar) {
        disconnect(m_controlBar, nullptr, this, nullptr);
        m_controlBar->close();
        m_controlBar->deleteLater();
        m_controlBar = nullptr;
    }

    m_controlBar = new SnapTray::QmlRecordingControlBar();
    connect(m_controlBar, &SnapTray::QmlRecordingControlBar::stopRequested,
            this, &RecordingManager::stopRecording);
    connect(m_controlBar, &SnapTray::QmlRecordingControlBar::cancelRequested,
            this, &RecordingManager::cancelRecording);
    connect(m_controlBar, &SnapTray::QmlRecordingControlBar::pauseRequested,
            this, &RecordingManager::pauseRecording);
    connect(m_controlBar, &SnapTray::QmlRecordingControlBar::resumeRequested,
            this, &RecordingManager::resumeRecording);

    m_controlBar->setStopOpensPreview(m_startSettings.showPreview);
    m_controlBar->setShortcutHints(registeredHotkeyText(SnapTray::HotkeyAction::PauseResumeRecording),
                                   registeredHotkeyText(SnapTray::HotkeyAction::RecordFullScreen));

    // Set initial region size
    m_controlBar->updateRegionSize(m_recordingRegion.width(), m_recordingRegion.height());
    m_controlBar->updateFps(m_frameRate);

    // Show in preparing state (buttons disabled until ready)
    m_controlBar->setPreparing(true);

    m_controlBar->show();
    m_controlBar->raiseAboveMenuBar();
    // Position after show() to avoid Qt/macOS adjusting the position
    m_controlBar->positionNear(m_recordingRegion);

    warnAboutVisibleCaptureControls();
    prepareAudioPermission();
}

void RecordingManager::initializeStartState()
{
    auto& settings = RecordingSettingsManager::instance();
    m_startSettings = {settings.outputFormat(), settings.showPreview(), settings.quality(),
                      settings.audioEnabled(), settings.audioSource(), settings.audioDevice(),
                      settings.countdownEnabled(), settings.countdownSeconds(),
                      settings.windowsGraphicsCapture(), settings.nativeFrameEncoding(),
                      settings.keyRecordingMode()};
    if (m_requiresDirectMp4Recording()) {
        // Apply backend limits to this request, without overwriting saved
        // preferences from a platform with audio and preview support.
        m_startSettings.outputFormat = 0;
        m_startSettings.showPreview = false;
        m_startSettings.audioEnabled = false;
    }
    m_defaultOutputFormat = m_startSettings.outputFormat;
    m_audioEnabled = m_startSettings.audioEnabled
        && recordingSupportsAudioCapture(m_startSettings.outputFormat, m_startSettings.showPreview);
    auto requestedAudio = SnapTray::requestedAudioStatus(
        settings.audioEnabled(), settings.audioSource(),
        m_startSettings.outputFormat, m_startSettings.showPreview);
    if (settings.audioEnabled() && !m_startSettings.audioEnabled) {
        // The platform backend records without audio (direct MP4 only).
        requestedAudio = SnapTray::requestedAudioStatus(
            true, settings.audioSource(), RecordingSettingsManager::kDefaultOutputFormat, true);
        for (auto* state : {&requestedAudio.microphone, &requestedAudio.systemAudio}) {
            if (*state != SnapTray::AudioSourceState::Off)
                *state = SnapTray::AudioSourceState::Unavailable;
        }
    }
    setAudioStatus(requestedAudio);
    m_audioSource = m_startSettings.audioSource;
    m_audioDevice = m_startSettings.audioDevice;
    m_countdownEnabled = m_startSettings.countdownEnabled;
    m_countdownSeconds = m_startSettings.countdownSeconds;
    ++m_startGeneration;
    m_recordedAsIntermediate = false;
    m_permissionPending = false;
    m_captureExclusionWarningShown = false;
    m_startupAudioWarnings.clear();
    m_reportedStartupAudioWarnings.clear();
    m_collectStartupAudioWarnings = true;
    m_elapsedTimer.invalidate();
    m_sessionClock.reset();
    m_stopReason.reset();
    m_frameCount = 0;
    resetPauseTracking();
    setState(State::Preparing);
}

void RecordingManager::warnAboutVisibleCaptureControls()
{
    if (m_state != State::Preparing || m_captureExclusionWarningShown
        || !m_captureControlsMayBeVisible || !m_captureControlsMayBeVisible()) return;
    m_captureExclusionWarningShown = true;
    emit recordingWarning(m_requiresDirectMp4Recording()
        ? tr("Recording controls and tooltips may appear in videos on this platform.")
        : tr("Recording controls and tooltips may appear in videos on this Windows version."));
}

void RecordingManager::addStartupAudioWarning(const QString& warning)
{
    if (!warning.isEmpty() && !m_startupAudioWarnings.contains(warning)
        && !m_reportedStartupAudioWarnings.contains(warning))
        m_startupAudioWarnings.append(warning);
}

void RecordingManager::flushStartupAudioWarnings()
{
    if (m_startupAudioWarnings.isEmpty()) return;
    const QString message = m_startupAudioWarnings.join(QChar('\n'));
    m_reportedStartupAudioWarnings.append(m_startupAudioWarnings);
    m_startupAudioWarnings.clear();
    emit recordingWarning(message);
}

void RecordingManager::reportAudioWarning(const QString& warning)
{
    if (m_collectStartupAudioWarnings) addStartupAudioWarning(warning);
    else emit recordingWarning(warning);
}

void RecordingManager::applyEncoderAudioResult(bool audioEnabled, const QString& warning)
{
    if (!m_audioEnabled || audioEnabled) return;
    m_audioEnabled = false;
    transitionAudioSources(SnapTray::AudioSourceState::Starting, SnapTray::AudioSourceState::Unavailable);
    cleanupAudio();
    if (m_controlBar) m_controlBar->setAudioEnabled(false);
    addStartupAudioWarning(warning.isEmpty()
        ? tr("Audio encoding is unavailable. This recording will be silent.") : warning);
}

void RecordingManager::prepareAudioPermission()
{
    if (m_state != State::Preparing || m_permissionPending) return;
    const quint64 generation = m_startGeneration;
    if (!m_audioEnabled || !audioSourceUsesInputDevice(m_audioSource)) {
        m_permissionPending = true;
        finishAudioPermission(generation, true);
        return;
    }
    using Permission = IAudioCaptureEngine::MicrophonePermission;
    const auto permission = m_checkMicrophonePermission();
    m_permissionPending = true;
    if (permission != Permission::NotDetermined) {
        finishAudioPermission(generation, permission == Permission::Authorized);
        return;
    }
    if (m_controlBar)
        m_controlBar->setPreparingStatus(tr("Waiting for microphone permission..."));
    const QPointer<RecordingManager> guard(this);
    m_requestMicrophonePermission([guard, generation](bool granted) {
        if (!guard) return;
        QMetaObject::invokeMethod(guard, [guard, generation, granted] {
            if (guard) guard->finishAudioPermission(generation, granted);
        }, Qt::QueuedConnection);
    });
}

void RecordingManager::finishAudioPermission(quint64 generation, bool granted)
{
    if (generation != m_startGeneration || m_state != State::Preparing || !m_permissionPending)
        return;
    m_permissionPending = false;
    if (!isScreenAvailable(m_targetScreen.data())) {
        cancelRecording();
        emit recordingError(tr("Recording screen is no longer available."));
        return;
    }
    if (!granted && m_audioEnabled) {
        auto status = m_audioStatus;
        status.microphone = SnapTray::AudioSourceState::PermissionDenied;
        setAudioStatus(status);
        if (m_audioSource == kBothAudioSources) {
            m_audioSource = kSystemAudioSource;
            m_audioDevice.clear();
            addStartupAudioWarning(tr("Microphone permission is unavailable. Recording system audio only."));
        } else {
            m_audioEnabled = false;
            addStartupAudioWarning(tr("Microphone permission is unavailable. This recording will be silent."));
        }
        if (m_controlBar) m_controlBar->setAudioEnabled(m_audioEnabled);
    }
    if (m_controlBar) m_controlBar->setPreparingStatus(tr("Preparing recording..."));
    m_initializeRecording();
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

    const CaptureScreenInfo screenInfo = CaptureScreenInfo::fromScreen(targetScreen);
    if (!screenInfo.isValid()) {
        qWarning() << "RecordingManager: Failed to snapshot target screen geometry";
        stopFrameCapture();
        setState(State::Idle);
        emit recordingError(tr("Recording screen information is unavailable."));
        return;
    }

    m_recordingRegion = normalizeRecordingRegion(m_recordingRegion, screenInfo);
    if (m_areaBoundary && !m_startSettings.showPreview) m_areaBoundary->setRegion(m_recordingRegion);
    if (m_recordingRegion.isEmpty()) {
        qWarning() << "RecordingManager: Normalized region is empty";
        stopFrameCapture();
        setState(State::Idle);
        emit recordingError(tr("Recording region is invalid."));
        return;
    }


    // Use the request snapshot, including across an asynchronous permission prompt.
    const int formatInt = m_startSettings.outputFormat;
    const bool showPreview = m_startSettings.showPreview;

    auto outputFormat = EncoderFactory::Format::MP4;
    if (formatInt == 1) outputFormat = EncoderFactory::Format::GIF;
    else if (formatInt == 2) outputFormat = EncoderFactory::Format::WebP;

    // When preview is enabled, always record as MP4 (preview player only supports MP4).
    // The user's default output format is pre-selected in the preview's format widget.
    auto recordingFormat = showPreview ? EncoderFactory::Format::MP4 : outputFormat;
    m_defaultOutputFormat = formatInt;

    // Probe actual input format so encoder format matches captured PCM data.
    int audioSampleRate = kDefaultAudioSampleRate;
    int audioChannels = kDefaultAudioChannels;
    int audioBitsPerSample = kDefaultAudioBitsPerSample;
    const bool shouldConfigureAudio = m_audioEnabled && (recordingFormat == EncoderFactory::Format::MP4);
    if (shouldConfigureAudio) {
        const auto canonicalFormat = SnapTray::Audio::TimestampedPcmMixer::outputFormat();
        audioSampleRate = canonicalFormat.sampleRate;
        audioChannels = canonicalFormat.channels;
        audioBitsPerSample = canonicalFormat.bitsPerSample;
    }

    // Derive the encoder size from the same native mapping captured for the
    // engine. Rounded Qt logical sizes (for example 1707 at 150%) must not
    // disagree with a 2560-pixel DXGI output.
    const QRect mappedPhysicalRegion = physicalRecordingRegion(
        m_recordingRegion, screenInfo);
    const QSize physicalSize = mappedPhysicalRegion.size();
    // Without native desktop bounds (macOS) the mapped region sits at (0, 0); window rects are
    // relative to the screen, so restore the region's offset to keep them in one coordinate space.
    m_windowFrameMapping = SnapTray::WindowFrameMapping::fromCapture(
        m_recordingRegion, screenInfo.geometry, screenInfo.physicalGeometry, screenInfo.devicePixelRatio,
        mappedPhysicalRegion);
    if (physicalSize.isEmpty()) {
        qWarning() << "RecordingManager: Failed to map recording region to physical pixels";
        stopFrameCapture();
        setState(State::Idle);
        emit recordingError(tr("Recording region could not be mapped to the target screen."));
        return;
    }
    if (!isEvenPhysicalSize(physicalSize)) {
        qWarning() << "RecordingManager: Region normalization produced odd physical size"
                   << physicalSize << "- encoder fallback may rescale frames.";
    }

    // Create initialization config
    RecordingInitTask::Config config;
    config.region = m_recordingRegion;
    config.screenInfo = screenInfo;
    config.frameRate = m_frameRate;
    config.audioEnabled = shouldConfigureAudio;
    config.audioSampleRate = audioSampleRate;
    config.audioChannels = audioChannels;
    config.audioBitsPerSample = audioBitsPerSample;
    config.outputPath = generateOutputPath();
    config.outputFormat = recordingFormat;
    config.frameSize = physicalSize;
    config.quality = m_startSettings.quality;
    config.preferGraphicsCapture = m_startSettings.windowsGraphicsCapture;
    config.gpuEncoding = m_startSettings.nativeFrameEncoding;
    config.intermediateQuality = decideIntermediateQuality(
        showPreview, QFileInfo(config.outputPath).absolutePath(), physicalSize);

    // Too little room to record even the first seconds: say so instead of
    // starting a recording that stops (or fails) right away.
    {
        const QString directory = QFileInfo(config.outputPath).absolutePath();
        const qint64 freeBytes = m_freeBytesForPath ? m_freeBytesForPath(directory) : -1;
        SnapTray::DiskSpaceGuard::Input input;
        input.bytesPerSecond = SnapTray::IntermediateQuality::estimatedBytesPerMinute(
            SnapTray::IntermediateQuality::intermediateBitrate(physicalSize, m_frameRate)) / 60;
        if (freeBytes >= 0 && freeBytes <= SnapTray::DiskSpaceGuard::safeStopThreshold(input)) {
            qWarning() << "RecordingManager: only" << freeBytes << "bytes free in" << directory;
            cancelRecording();
            emit recordingError(tr("Not enough free disk space to start recording (%1 free).")
                                    .arg(QLocale().formattedDataSize(freeBytes)));
            return;
        }
    }
    m_recordingFilePath = config.outputPath;
    // Edit mode: the chosen area becomes the preview's initial crop, kept in
    // the manifest so a recovered recording opens with it too.
    m_pendingInitialCrop = showPreview && !m_preselectedArea.isEmpty()
        ? SnapTray::RecordingArea::videoCrop(m_preselectedArea, m_recordingRegion, physicalSize)
        : QRect();
    beginSession(config.outputPath, physicalSize, shouldConfigureAudio);

    // Collect UI window IDs to exclude from capture.
    if (m_controlBar) {
        config.excludedCaptureWindowIds = m_controlBar->prepareCaptureExclusions();
        if (config.excludedCaptureWindowIds.isEmpty()) {
            cancelRecording();
            emit recordingError(tr("Failed to prepare recording controls for capture."));
            return;
        }
        m_controlBar->setExcludedFromCapture(true);
    }
    // The area's frame is never part of the recording where the platform can
    // leave it out; elsewhere (X11) it is drawn just outside the area.
    if (m_areaBoundary) {
        if (const quintptr id = m_areaBoundary->captureExclusionId()) config.excludedCaptureWindowIds.append(id);
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
    const quint64 startGeneration = m_startGeneration;
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

    // The worker initializes from immutable screen metadata. Confirm that the
    // corresponding live screen still exists before adopting its resources.
    if (!isScreenAvailable(m_targetScreen.data())) {
        qWarning() << "RecordingManager: Target screen was disconnected during initialization";
        task->discardResult();
        m_initTask.clear();
        m_targetScreen = nullptr;
        stopFrameCapture();
        setState(State::Idle);
        emit recordingError(tr("Recording screen was disconnected during initialization."));
        return;
    }

    // Get the result from the init task
    RecordingInitTask::Result &result = task->result();

    if (!result.success) {
        qWarning() << "RecordingManager: Initialization failed:" << result.error;
        emit recordingError(result.error);

        // Clean up UI
        if (m_controlBar) {
            m_controlBar->close();
            m_controlBar->deleteLater();
            m_controlBar = nullptr;
        }

        m_initTask.clear();

        setState(State::Idle);
        return;
    }

    applyEncoderAudioResult(result.audioEnabled, result.audioWarning);

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
        connect(m_captureEngine.get(), &ICaptureEngine::stoppedByUser, this, [this] {
            interruptRecording(SnapTray::RecordingStopReason::SystemControl);
        });
        connect(m_captureEngine.get(), &ICaptureEngine::error,
                this, [this](const QString &msg) {
            interruptRecording(SnapTray::RecordingStopReason::CaptureFailed, msg);
        });
    }

    m_usingNativeEncoder = result.usingNativeEncoder;
    m_encoderAcceptsNativeFrames = result.nativeEncoder && result.nativeEncoder->acceptsNativeFrames();
    m_session.fragmented = result.fragmentedOutput;
    updateSession(SnapTray::RecordingSessionInfo::State::Recording);

    // Create encoding worker to offload encoding from main thread
    m_encodingWorker = std::make_unique<EncodingWorker>(nullptr);
    m_health = std::make_shared<SnapTray::RecordingHealth>();
    m_health->reset(m_captureEngine ? m_captureEngine->engineName() : QString(), m_startGeneration);
    m_encodingWorker->setHealth(m_health);
    m_healthPolicy.reset();
    m_healthOfferedSeen = 0;
    m_healthDroppedSeen = 0;
    m_healthTicks = 0;

    // Transfer encoder ownership to worker
    const bool hasNativeEncoder = (result.nativeEncoder != nullptr);
    const bool hasGifEncoder = (result.gifEncoder != nullptr);
    const bool hasWebPEncoder = (result.webpEncoder != nullptr);
    IVideoEncoder* nativeEncoder = hasNativeEncoder ? result.releaseNativeEncoder() : nullptr;
    NativeGifEncoder* gifEncoder = hasGifEncoder ? result.releaseGifEncoder() : nullptr;
    WebPAnimationEncoder* webpEncoder = hasWebPEncoder ? result.releaseWebpEncoder() : nullptr;

    if (nativeEncoder) {
        m_encodingWorker->setVideoEncoder(nativeEncoder);
        m_encodingWorker->setEncoderType(EncodingWorker::EncoderType::Video);
        m_encodingWorker->setNativeFramesEnabled(
            RecordingSettingsManager::instance().nativeFrameEncoding());
    }
    m_writtenOutputBytes = nativeEncoder ? nativeEncoder->writtenBytesCounter() : nullptr;
    m_bufferedOutputBytes = gifEncoder ? gifEncoder->bufferedBytesCounter()
        : webpEncoder ? webpEncoder->bufferedBytesCounter() : nullptr;
    if (gifEncoder) {
        m_encodingWorker->setGifEncoder(gifEncoder);
        if (!nativeEncoder) {
            m_encodingWorker->setEncoderType(EncodingWorker::EncoderType::Gif);
        }
    }
    if (webpEncoder) {
        m_encodingWorker->setWebPEncoder(webpEncoder);
        if (!nativeEncoder) {
            m_encodingWorker->setEncoderType(EncodingWorker::EncoderType::WebP);
        }
    }

    const quint64 encodingGeneration = ++m_encodingGeneration;
    const bool useDedicatedEncodingThread = shouldUseDedicatedEncodingThread(hasNativeEncoder);
    if (useDedicatedEncodingThread) {
        m_encodingThread = std::make_unique<QThread>();
        m_encodingThread->setObjectName(QStringLiteral("RecordingEncodingThread"));
        m_encodingThread->start();

        if (!m_encodingThread->isRunning()) {
            qWarning() << "RecordingManager: Failed to start dedicated encoding thread";
            emit recordingError(tr("Failed to start encoding thread"));
            teardownEncodingWorker(true);
            m_initTask.clear();
            setState(State::Idle);
            return;
        }

        m_encodingWorker->moveOwnedEncodersToThread(m_encodingThread.get());
        m_encodingWorker->moveToThread(m_encodingThread.get());
    }

    // Connect worker signals
    connect(m_encodingWorker.get(), &EncodingWorker::finished,
            this,
            [this, encodingGeneration](bool success, const QString& outputPath) {
                if (encodingGeneration != m_encodingGeneration) {
                    qWarning() << "RecordingManager: Ignoring stale encoding finished callback";
                    return;
                }
                onEncodingFinished(success, outputPath);
            },
            Qt::QueuedConnection);
    connect(m_encodingWorker.get(), &EncodingWorker::error,
            this,
            [this, encodingGeneration](const QString& error) {
                if (encodingGeneration != m_encodingGeneration) {
                    qWarning() << "RecordingManager: Ignoring stale encoding error callback";
                    return;
                }
                onEncodingError(error);
            },
            Qt::QueuedConnection);

    // Start the encoding worker
    if (!m_encodingWorker->start()) {
        qWarning() << "RecordingManager: Failed to start encoding worker";
        emit recordingError(tr("Failed to start encoding worker"));
        teardownEncodingWorker(true);
        m_initTask.clear();
        setState(State::Idle);
        return;
    }

    if (m_captureEngine && m_captureEngine->deliversFrames()) {
        auto* worker = m_encodingWorker.get();
        connect(m_captureEngine.get(), &ICaptureEngine::frameCaptured, worker,
                [this, worker](const SnapTray::CapturedFrame& frame) {
            QMutexLocker lock(&m_durationMutex);
            if (!m_acceptAsyncFrames || !frame.isValid()) return;
            // The frame's own capture time, not when this thread got to it.
            const qint64 sourceTimeNs = frame.sourceTimeNs >= 0 ? frame.sourceTimeNs
                                                                : SnapTray::captureClockNowNs();
            qint64 timeMs = 0;
            if (!admitFrameLocked(sourceTimeNs, true, &timeMs)) return;
            if (worker->enqueueFrame({frame.image, timeMs, 0, frame.nativeBuffer, frame.pixelSize})) ++m_frameCount;
        }, Qt::DirectConnection);
    }

    configureAudioCapture();

    // Clean up init task
    m_initTask.clear();

    flushStartupAudioWarnings();
    if (m_state != State::Preparing || startGeneration != m_startGeneration) return;

    // Start countdown if enabled, otherwise go directly to recording
    if (m_countdownEnabled && m_countdownSeconds > 0) {
        startCountdown();
    } else {
        startRecordingAfterCountdown();
    }
}

void RecordingManager::configureAudioCapture()
{
    // Create audio engine on main thread (macOS audio APIs require main thread)
    // Audio engine was NOT created in worker thread due to thread affinity requirements
    if (m_audioEnabled) {
        // Pass nullptr as parent since we use custom deleter with unique_ptr
        m_audioEngine.reset(m_createAudioEngine());
        if (m_audioEngine) {
            const QPointer<IAudioCaptureEngine> configuredEngine(m_audioEngine.get());
            // Connect status signals before configuration because some engines
            // can report a source fallback synchronously from setAudioSource().
            connect(m_audioEngine.get(), &IAudioCaptureEngine::error,
                    this, [this, configuredEngine](const QString &msg) {
                if (!configuredEngine || m_audioEngine.get() != configuredEngine.data()) {
                    return;
                }
                qWarning() << "RecordingManager: Audio error:" << msg;
            });
            connect(m_audioEngine.get(), &IAudioCaptureEngine::warning,
                    this, [this, configuredEngine](const QString &msg) {
                if (!configuredEngine || m_audioEngine.get() != configuredEngine.data()) {
                    return;
                }
                qWarning() << "RecordingManager: Audio warning:" << msg;
                reportAudioWarning(tr(
                    "Audio capture encountered a problem. This recording may contain missing audio."));
            });
            connect(m_audioEngine.get(), &IAudioCaptureEngine::activeSourceChanged,
                    this, [this, configuredEngine](IAudioCaptureEngine::AudioSource activeSource) {
                if (!configuredEngine || m_audioEngine.get() != configuredEngine.data()) {
                    return;
                }
                auto status = m_audioStatus;
                const auto lost = [](SnapTray::AudioSourceState& state) {
                    if (state != SnapTray::AudioSourceState::Off
                        && state != SnapTray::AudioSourceState::PermissionDenied
                        && state != SnapTray::AudioSourceState::DeviceMissing) {
                        state = SnapTray::AudioSourceState::Unavailable;
                    }
                };
                if (activeSource == IAudioCaptureEngine::AudioSource::Microphone
                    || activeSource == IAudioCaptureEngine::AudioSource::None) {
                    lost(status.systemAudio);
                }
                if (activeSource == IAudioCaptureEngine::AudioSource::SystemAudio
                    || activeSource == IAudioCaptureEngine::AudioSource::None) {
                    lost(status.microphone);
                }
                setAudioStatus(status);
                switch (activeSource) {
                case IAudioCaptureEngine::AudioSource::Microphone:
#ifdef Q_OS_MACOS
                    reportAudioWarning(tr(
                        "System audio is unavailable. Recording microphone only. "
                        "Check Screen Recording permission in System Settings."));
#else
                    reportAudioWarning(tr(
                        "System audio is unavailable. Recording microphone only. "
                        "Check the default playback device."));
#endif
                    break;
                case IAudioCaptureEngine::AudioSource::SystemAudio:
                    reportAudioWarning(tr(
                        "Microphone is unavailable. Recording system audio only. "
                        "Check microphone permission and the selected input device."));
                    break;
                case IAudioCaptureEngine::AudioSource::None:
                    m_audioEnabled = false;
                    if (m_controlBar) {
                        m_controlBar->setAudioEnabled(false);
                    }
                    reportAudioWarning(tr(
                        "Audio is unavailable. This recording will be silent. "
                        "Check system permissions and the selected audio device."));
                    break;
                case IAudioCaptureEngine::AudioSource::Both:
                    break;
                }
            });

            connect(m_audioEngine.get(), &IAudioCaptureEngine::deviceLost,
                    this, [this, configuredEngine](IAudioCaptureEngine::AudioSource lost) {
                if (!configuredEngine || m_audioEngine.get() != configuredEngine.data()) {
                    return;
                }
                // Only the source that failed: the other may still be recording.
                auto status = m_audioStatus;
                const bool microphoneLost = lost != IAudioCaptureEngine::AudioSource::SystemAudio;
                const bool micWasRecording = microphoneLost
                    && status.microphone == SnapTray::AudioSourceState::Active;
                auto& state = microphoneLost ? status.microphone : status.systemAudio;
                if (state != SnapTray::AudioSourceState::Off) {
                    state = SnapTray::AudioSourceState::DeviceMissing;
                }
                setAudioStatus(status);
                // Keep recording the rest; never switch to another microphone
                // without the user choosing it.
                if (micWasRecording) {
                    reportAudioWarning(tr("The microphone was disconnected. Recording continues without it, "
                                          "so its audio stops from this point."));
                }
            });

            // Set audio source
            const auto source = resolveAudioSource(m_audioSource);
            if (!m_audioEngine->setAudioSource(source)) {
                qWarning() << "RecordingManager: Failed to apply configured audio source";
                transitionAudioSources(SnapTray::AudioSourceState::Starting,
                                       SnapTray::AudioSourceState::Unavailable);
                reportAudioWarning(tr(
                    "Audio is unavailable. This recording will be silent. "
                    "Check system permissions and the selected audio device."));
                m_audioEnabled = false;
                cleanupAudio();
            }

            if (m_audioEngine) {
                const QString requestedDevice = m_audioDevice;
                m_audioDevice = configuredAudioInputDeviceOrDefault(
                    *m_audioEngine, m_audioSource, m_audioDevice);
                if (audioSourceUsesInputDevice(m_audioSource)
                    && !requestedDevice.isEmpty() && m_audioDevice.isEmpty()) {
                    reportAudioWarning(tr(
                        "The selected microphone was not found. Recording from the default microphone."));
                }

                if (audioSourceUsesInputDevice(m_audioSource)
                    && !m_audioDevice.isEmpty() && !m_audioEngine->setDevice(m_audioDevice)) {
                    qWarning() << "RecordingManager: Failed to apply configured audio device";
                }
            }
        } else {
            qWarning() << "RecordingManager: Failed to create audio engine";
            m_audioEnabled = false;
            transitionAudioSources(SnapTray::AudioSourceState::Starting,
                                   SnapTray::AudioSourceState::Unavailable);
            reportAudioWarning(tr(
                "Audio is unavailable. This recording will be silent. "
                "Check system permissions and the selected audio device."));
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
            transitionAudioSources(SnapTray::AudioSourceState::Starting,
                                   SnapTray::AudioSourceState::UnsupportedFormat);
            cleanupAudio();
        }
    }

    // Update control bar to show ready state
    if (m_controlBar) {
        m_controlBar->setPreparing(false);
        m_controlBar->setAudioEnabled(m_audioEngine != nullptr && m_audioEnabled);
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

    m_countdownOverlay = new QmlCountdownOverlay(this);
    m_countdownOverlay->setRegion(m_recordingRegion);
    m_countdownOverlay->setCountdownSeconds(m_countdownSeconds);

    const quint64 generation = m_startGeneration;
    connect(m_countdownOverlay, &QmlCountdownOverlay::countdownFinished,
            this, [this, generation] {
        if (generation == m_startGeneration) startRecordingAfterCountdown();
    });
    connect(m_countdownOverlay, &QmlCountdownOverlay::countdownCancelled,
            this, &RecordingManager::onCountdownCancelled);

    m_countdownOverlay->start();
}

void RecordingManager::startRecordingAfterCountdown()
{
    if (m_state != State::Preparing && m_state != State::Countdown) return;
    const quint64 generation = m_startGeneration;
    // Clean up countdown overlay if it exists (deleteLater because we're
    // called from the QML countdown finished signal handler)
    if (m_countdownOverlay) {
        m_countdownOverlay->close();
        m_countdownOverlay->deleteLater();
        m_countdownOverlay = nullptr;
    }

    if (!isScreenAvailable(m_targetScreen.data())) {
        qWarning() << "RecordingManager: Target screen unavailable after countdown";
        m_targetScreen = nullptr;
        emit recordingError(tr("Recording screen is no longer available."));
        cancelRecording();
        return;
    }

    if (SnapTray::currentPlatformCapabilities().recordingControlsInTray && m_controlBar) {
        m_controlBar->hide();
        // Discard any frame cached while the preparation/countdown UI was visible.
        if (m_captureEngine) m_captureEngine->captureFrame();
    }

    // Initialize state and counters. The session clock's epoch is the same
    // moment as the elapsed timer used for the duration and window timeline.
    {
        QMutexLocker lock(&m_durationMutex);
        m_framePacer.reset(m_frameRate);
    }
    m_sessionClock.start(SnapTray::captureClockNowNs());
    m_elapsedTimer.start();
    startDiskMonitor();
    watchTargetScreen();
    setState(State::Recording);
    if (m_state != State::Recording || generation != m_startGeneration) return;
    m_frameCount = 0;

    // Start audio capture here to synchronize with video timer
    if (m_audioEngine) {
        startAudioTracks();
        if (m_audioEngine->start()) {
            transitionAudioSources(SnapTray::AudioSourceState::Starting, SnapTray::AudioSourceState::Active);
        } else {
            qWarning() << "RecordingManager: Failed to start audio capture";
            transitionAudioSources(SnapTray::AudioSourceState::Starting,
                                   SnapTray::AudioSourceState::Unavailable);
            reportAudioWarning(tr(
                "Audio is unavailable. This recording will be silent. "
                "Check system permissions and the selected audio device."));
            m_audioEnabled = false;
            cleanupAudio();
            if (m_controlBar) {
                m_controlBar->setAudioEnabled(false);
            }
        }
    }

    // After audio start: the first sample enumerates windows synchronously and must not delay audio.
    // Samples are stamped by the elapsed clock, so the timeline is unaffected by the later start.
    startWindowTimeline();
    startCursorTimeline();

    if (m_state != State::Recording || generation != m_startGeneration) return;
    m_collectStartupAudioWarnings = false;
    flushStartupAudioWarnings();

    // Load watermark settings for recording and configure worker
    if (m_state != State::Recording || generation != m_startGeneration) return;
    m_watermarkSettings = WatermarkSettingsManager::instance().load();
    if (m_watermarkSettings.applyToRecording && !m_watermarkSettings.imagePath.isEmpty()) {
        m_watermarkSettings.enabled = true;  // Enable for render() to work
        if (m_encodingWorker) {
            m_encodingWorker->setWatermarkSettings(m_watermarkSettings);
        }
    }

    // GPU frames skip the CPU copy when the encoder takes them and nothing
    // has to draw on them (#155); otherwise frames stay images.
    if (m_captureEngine) {
        const bool nativeFrames = m_encoderAcceptsNativeFrames && !m_watermarkSettings.enabled;
        m_captureEngine->setNativeFrameOutput(nativeFrames);
        if (nativeFrames) {
            qDebug() << "RecordingManager: capture frames go to the encoder as GPU surfaces";
        }
    }

    // Small delay so the control bar is fully rendered before capture starts
    QTimer::singleShot(kOverlayRenderDelay, this, [this, generation]() {
        if (generation == m_startGeneration) startCaptureTimers();
    });

    emit recordingStarted();
}

void RecordingManager::onCountdownCancelled()
{
    // Clean up countdown overlay (deleteLater because we're called from QML signal)
    if (m_countdownOverlay) {
        m_countdownOverlay->close();
        m_countdownOverlay->deleteLater();
        m_countdownOverlay = nullptr;
    }

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

    if (m_captureEngine && m_captureEngine->deliversFrames()) {
        QMutexLocker lock(&m_durationMutex);
        m_acceptAsyncFrames = true;
    } else {
        // Polling backends retain the existing GUI-thread path.
        m_captureTimer = std::make_unique<QTimer>(nullptr);
        m_captureTimer->setTimerType(Qt::PreciseTimer);
        connect(m_captureTimer.get(), &QTimer::timeout, this, &RecordingManager::captureFrame);
        m_captureTimer->start(1000 / m_frameRate);
    }

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
        // Keep what was recorded: stop and finish, do not discard.
        interruptRecording(SnapTray::RecordingStopReason::ScreenDisconnected);
        return;
    }

    // Store local copies (raw pointers) to avoid race conditions
    // where unique_ptrs might be reset during execution
    ICaptureEngine *captureEngine = m_captureEngine.get();
    EncodingWorker *encodingWorker = m_encodingWorker.get();

    if (!captureEngine || !encodingWorker) {
        return;
    }

    // Polling backends return their latest frame; it is stamped when read.
    // (Backends that capture on their own thread stamp frames themselves.)
    const qint64 sourceTimeNs = SnapTray::captureClockNowNs();
    const SnapTray::CapturedFrame frame = captureEngine->captureNativeFrame();

    if (frame.isValid()) {
        qint64 timeMs = 0;
        {
            QMutexLocker locker(&m_durationMutex);
            // The capture timer already paces polling.
            if (!admitFrameLocked(sourceTimeNs, false, &timeMs)) return;
        }

        // Enqueue frame for encoding (non-blocking)
        // Watermark is applied by the worker thread
        EncodingWorker::FrameData frameData{frame.image, timeMs, 0, frame.nativeBuffer, frame.pixelSize};
        if (encodingWorker->enqueueFrame(frameData)) ++m_frameCount;
    }
}

void RecordingManager::interruptRecording(SnapTray::RecordingStopReason reason, const QString& detail)
{
    if (m_state == State::Preparing || m_state == State::Countdown) {
        // Nothing has been recorded yet.
        qWarning() << "RecordingManager: interrupted before recording:"
                   << SnapTray::recordingStopReasonName(reason) << detail;
        cancelRecording();
        emit recordingError(tr("Recording could not start. %1")
                                .arg(detail.isEmpty() ? SnapTray::recordingStopCause(reason) : detail));
        return;
    }
    // Idempotent: the first cause wins; a later stop or error changes nothing.
    if ((m_state != State::Recording && m_state != State::Paused) || m_stopReason) {
        return;
    }
    m_stopReason = reason;
    qWarning() << "RecordingManager: recording interrupted:" << SnapTray::recordingStopReasonName(reason)
               << detail;
    if (reason == SnapTray::RecordingStopReason::ScreenDisconnected) {
        m_targetScreen = nullptr;
    }
    stopRecording();
    emit recordingInterrupted(reason);
    const QString message = SnapTray::recordingStopMessage(reason, detail);
    if (!message.isEmpty()) {
        emit recordingWarning(message);
    }
}

void RecordingManager::watchTargetScreen()
{
    unwatchTargetScreen();
    QScreen* screen = m_targetScreen.data();
    if (!screen) {
        return;
    }
    // A different geometry, scale or orientation would distort the fixed
    // output size: stop and keep the recording instead of warping it.
    const auto changed = [this] { interruptRecording(SnapTray::RecordingStopReason::ScreenChanged); };
    m_screenWatch << connect(screen, &QScreen::geometryChanged, this, changed);
    m_screenWatch << connect(screen, &QScreen::physicalDotsPerInchChanged, this, changed);
    m_screenWatch << connect(screen, &QScreen::orientationChanged, this, changed);
    // Backends that capture on their own thread are not polled, so watch for
    // the screen going away here too.
    m_screenWatch << connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this, screen](QScreen* removed) {
        if (removed == screen) interruptRecording(SnapTray::RecordingStopReason::ScreenDisconnected);
    });
}

void RecordingManager::unwatchTargetScreen()
{
    for (const auto& connection : std::as_const(m_screenWatch)) {
        disconnect(connection);
    }
    m_screenWatch.clear();
}

void RecordingManager::beginSession(const QString& videoPath, const QSize& frameSize, bool audioRequired)
{
    endSession(true);
    m_session = {};
    m_session.sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_session.videoPath = videoPath;
    m_session.format = QFileInfo(videoPath).suffix().toLower();
    m_session.frameSize = frameSize;
    m_session.frameRate = m_frameRate;
    m_session.audioRequired = audioRequired;
    m_session.intermediate = m_recordedAsIntermediate;
    m_session.initialCrop = m_pendingInitialCrop;
    m_session.startedAt = QDateTime::currentDateTime();
    m_sessionHeartbeat = 0;
    // The lock tells a scan (here or in another SnapTray) this session is live;
    // a crashed owner leaves a stale lock that QLockFile detects.
    m_sessionLock = std::make_unique<QLockFile>(SnapTray::RecordingSession::lockPathFor(videoPath));
    m_sessionLock->setStaleLockTime(0);
    if (!m_sessionLock->tryLock(0)) {
        qWarning() << "RecordingManager: could not lock recording session" << videoPath;
    }
    updateSession(SnapTray::RecordingSessionInfo::State::Recording);
}

void RecordingManager::updateSession(SnapTray::RecordingSessionInfo::State state)
{
    if (m_session.videoPath.isEmpty()) {
        return;
    }
    m_session.state = state;
    m_session.updatedAt = QDateTime::currentDateTime();
    QString error;
    if (!SnapTray::RecordingSession::write(m_session, &error)) {
        qWarning() << "RecordingManager: could not write recording session:" << error;
    }
}

void RecordingManager::endSession(bool removeManifest)
{
    if (!m_session.videoPath.isEmpty() && removeManifest) {
        SnapTray::RecordingSession::remove(m_session.videoPath);
    }
    if (m_sessionLock) {
        m_sessionLock->unlock();
        m_sessionLock.reset();
    }
    m_session = {};
}

bool RecordingManager::isSessionActive(const QString& videoPath)
{
    // Held by this process (another QLockFile) or by a live process: busy.
    QLockFile probe(SnapTray::RecordingSession::lockPathFor(videoPath));
    probe.setStaleLockTime(0);
    if (!probe.tryLock(0)) {
        return probe.error() == QLockFile::LockFailedError;
    }
    probe.unlock();
    return false;
}

void RecordingManager::scanForRecoveries()
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QPointer<RecordingManager> guard(this);
    (void)QtConcurrent::run([guard, directory]() {
        std::unique_ptr<IVideoTranscoder> transcoder = IVideoTranscoder::create();
        const SnapTray::RecordingSession::Probe videoProbe = [&transcoder](const QString& path) {
            SnapTray::RecordingSession::ProbeResult result;
            if (!transcoder) return result;
            const VideoFileProbe info = transcoder->probe(path);
            result.decodable = info.valid;
            result.durationMs = info.durationMs;
            result.hasAudio = info.hasAudio;
            return result;
        };
        // GIF and WebP are checked as images: the video probe cannot open
        // them on every platform (#172 review).
        const auto probe = [&videoProbe](const QString& path) {
            return SnapTray::RecordingSession::probeForRecovery(path, videoProbe);
        };
        const auto found = SnapTray::RecordingSession::scan(directory, probe, &RecordingManager::isSessionActive);
        QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, found]() {
            if (guard) emit guard->recoveriesFound(found);
        }, Qt::QueuedConnection);
    });
}

bool RecordingManager::openRecoveredRecording(const QString& videoPath)
{
    if (m_state != State::Idle || !QFileInfo::exists(videoPath)) {
        return false;
    }
    const auto info = SnapTray::RecordingSession::read(SnapTray::RecordingSession::manifestPathFor(videoPath));
    m_session = info.value_or(SnapTray::RecordingSessionInfo{});
    m_session.videoPath = videoPath;
    m_sessionLock = std::make_unique<QLockFile>(SnapTray::RecordingSession::lockPathFor(videoPath));
    m_sessionLock->setStaleLockTime(0);
    if (!m_sessionLock->tryLock(0)) {
        m_sessionLock.reset();
        m_session = {};
        return false;  // another SnapTray has it open
    }
    if (SnapTray::RecordingSession::isAnimatedFormat(videoPath)) {
        // A finished GIF/WebP is the output already, and the preview plays
        // video only (#172 review): save it as the no-preview flow does. The
        // file stays if the save is cancelled or fails, and so does its
        // manifest, to be recovered later.
        const std::shared_ptr<QLockFile> sessionLock(m_sessionLock.release());
        m_session = {};
        QMetaObject::invokeMethod(this, [this, videoPath, sessionLock]() {
            // Named by its own size: a recovered recording has no region. The
            // reader is closed before saving: Windows cannot move an open file.
            QSize size;
            {
                QImageReader reader(videoPath);
                size = reader.size();
            }
            const SaveOutcome outcome = showSaveDialog(videoPath, size, true);
            if (outcome == SaveOutcome::Saved) {
                SnapTray::RecordingSession::remove(videoPath);
            }
            if (sessionLock) sessionLock->unlock();
            if (outcome != SaveOutcome::Saved) {
                emit recoveryKept(videoPath); // still recoverable
            }
        }, Qt::QueuedConnection);
        return true;
    }
    m_tempVideoPath = videoPath;
    setState(State::Previewing);
    // Recorded as the high-quality intermediate (or unknown, from an older
    // manifest): Save still checks the output quality instead of moving it.
    emit previewRequested(videoPath, RecordingSettingsManager::instance().outputFormat(), m_session.intermediate);
    return true;
}

bool RecordingManager::isAllowedSaveDestination(const QString& path)
{
    return !path.isEmpty() && !SnapTray::RecordingProjects::contains(path);
}

bool RecordingManager::openProjectRecording(const QString& videoPath)
{
    if (m_state != State::Idle || !QFileInfo::exists(videoPath)) {
        return false;
    }
    m_session = {};
    m_tempVideoPath = videoPath;
    setState(State::Previewing);
    return true;
}

void RecordingManager::startDiskMonitor()
{
    m_diskGuard.reset();
    m_writeRate.reset();
    m_diskProbeRunning = false;
    m_diskUnknownReported = false;
    setLowDiskSpace(false);
    m_diskTimer = std::make_unique<QTimer>(nullptr);
    connect(m_diskTimer.get(), &QTimer::timeout, this, &RecordingManager::checkDiskSpace);
    m_diskTimer->start(kDiskCheckIntervalMs);
}

void RecordingManager::stopDiskMonitor()
{
    if (m_diskTimer) {
        m_diskTimer->stop();
        m_diskTimer.reset();
    }
}

void RecordingManager::checkDiskSpace()
{
    if (m_diskProbeRunning || m_recordingFilePath.isEmpty()
        || (m_state != State::Recording && m_state != State::Paused)) {
        return;
    }
    // Free-space and file-size queries can block on slow or network volumes:
    // never on the GUI thread, and never more than one at a time.
    m_diskProbeRunning = true;
    const quint64 generation = m_startGeneration;
    const QString path = m_recordingFilePath;
    const auto probe = m_freeBytesForPath;
    const auto buffered = m_bufferedOutputBytes;
    const auto written = m_writtenOutputBytes;
    const qint64 sampleMs = m_elapsedTimer.isValid() ? m_elapsedTimer.elapsed() : 0;
    const QPointer<RecordingManager> guard(this);
    (void)QtConcurrent::run([guard, generation, path, probe, buffered, written, sampleMs]() {
        const qint64 freeBytes = probe ? probe(QFileInfo(path).absolutePath()) : -1;
        // An encoder writing a temporary file until it finishes (#171 review)
        // reports its size; otherwise the output file shows it.
        const qint64 fileBytes = written ? written->load() : QFileInfo(path).size();
        // Buffered formats have no file yet: all of it still needs the room.
        const qint64 pendingBytes = buffered ? buffered->load() : 0;
        QMetaObject::invokeMethod(QCoreApplication::instance(),
                                  [guard, generation, freeBytes, fileBytes, sampleMs, pendingBytes]() {
            if (guard) guard->onDiskSample(generation, freeBytes, fileBytes, sampleMs, pendingBytes);
        }, Qt::QueuedConnection);
    });
}

void RecordingManager::setLowDiskSpace(bool low)
{
    if (m_lowDiskSpace == low) return;
    m_lowDiskSpace = low;
    emit lowDiskSpaceChanged(low);
}

void RecordingManager::onDiskSample(quint64 generation, qint64 freeBytes, qint64 fileBytes, qint64 sampleMs,
                                    qint64 pendingBytes)
{
    m_diskProbeRunning = false;
    if (generation != m_startGeneration || (m_state != State::Recording && m_state != State::Paused)) {
        return;
    }
    // The rate covers both: buffered data reaches the disk at the end.
    m_writeRate.addSample(qMax<qint64>(0, fileBytes) + qMax<qint64>(0, pendingBytes), sampleMs);
    // Keep the recovery manifest's duration roughly current (every ~10 s).
    constexpr int kSamplesPerHeartbeat = 5;
    if (++m_sessionHeartbeat >= kSamplesPerHeartbeat) {
        m_sessionHeartbeat = 0;
        m_session.recordedMs = sampleMs;
        updateSession(SnapTray::RecordingSessionInfo::State::Recording);
    }
    SnapTray::DiskSpaceGuard::Input input;
    if (freeBytes >= 0) input.freeBytes = freeBytes;
    input.recordedBytes = qMax<qint64>(0, fileBytes);
    input.pendingBytes = qMax<qint64>(0, pendingBytes);
    input.bytesPerSecond = m_writeRate.bytesPerSecond();
    input.exportLikely = m_startSettings.showPreview;
    const auto previous = m_diskGuard.level();
    const auto result = m_diskGuard.evaluate(input);
    using Level = SnapTray::DiskSpaceGuard::Level;
    switch (result.level) {
    case Level::Ok:
        setLowDiskSpace(false);
        break;
    case Level::Unknown:
        if (!m_diskUnknownReported) {
            m_diskUnknownReported = true;
            qWarning() << "RecordingManager: free space unknown for" << m_recordingFilePath;
            emit recordingWarning(tr("SnapTray can't check the free disk space for this recording. "
                                     "Keep an eye on it during long recordings."));
        }
        break;
    case Level::Warning:
        if (previous != Level::Warning) {
            setLowDiskSpace(true);
            // Minutes are an estimate from the recent write rate; without one,
            // report the free space itself.
            emit recordingWarning(result.secondsLeft >= 0
                ? tr("Disk space is running low: about %1 min of recording left (estimate). "
                     "Stop soon to keep this recording.").arg(qMax<qint64>(1, result.secondsLeft / 60))
                : tr("Disk space is running low: %1 free. Stop soon to keep this recording.")
                      .arg(QLocale().formattedDataSize(freeBytes)));
        }
        break;
    case Level::SafeStop:
        if (previous != Level::SafeStop) {
            setLowDiskSpace(true);
            qWarning() << "RecordingManager: stopping; free" << freeBytes << "bytes, threshold"
                       << result.safeStopBytes;
            // The normal stop path: drain and finalize, then preview or save.
            interruptRecording(SnapTray::RecordingStopReason::DiskFull);
        }
        break;
    }
}

SnapTray::RecordingHealth::ClockCounters RecordingManager::clockCountersLocked() const
{
    const auto clock = m_sessionClock.stats();
    const auto pacing = m_framePacer.stats();
    SnapTray::RecordingHealth::ClockCounters counters;
    counters.beforeStart = clock.beforeStart;
    counters.paused = clock.paused;
    counters.outOfOrder = clock.duplicates + clock.backwards;
    counters.pacedOut = pacing.dropped;
    counters.late = pacing.late;
    counters.stalls = pacing.stalls;
    counters.longestGapMs = pacing.longestGapNs / 1000000;
    return counters;
}

void RecordingManager::checkHealth(qint64 nowMs)
{
    if (!m_health || m_state != State::Recording) {
        return;
    }
    const auto snapshot = m_health->snapshot(SnapTray::captureClockNowNs());
    const qint64 offered = snapshot.offered - m_healthOfferedSeen;
    const qint64 dropped = snapshot.droppedFrames() - m_healthDroppedSeen;
    m_healthOfferedSeen = snapshot.offered;
    m_healthDroppedSeen = snapshot.droppedFrames();
    switch (m_healthPolicy.evaluate(offered, dropped, nowMs)) {
    case SnapTray::HealthWarningPolicy::Action::Warn:
        qWarning() << "RecordingManager: sustained frame loss;"
                   << SnapTray::RecordingHealth::formatDiagnostics(snapshot);
        emit droppingFramesChanged(true);
        emit recordingWarning(tr("Recording can't keep up and is dropping frames. "
                                 "Try a lower frame rate, or close other busy apps."));
        break;
    case SnapTray::HealthWarningPolicy::Action::Recovered:
        emit droppingFramesChanged(false);
        break;
    case SnapTray::HealthWarningPolicy::Action::None:
        break;
    }
}

bool RecordingManager::admitFrameLocked(qint64 sourceTimeNs, bool paced, qint64* encoderTimeMs)
{
    qint64 recordingTimeNs = 0;
    if (m_sessionClock.admit(sourceTimeNs, &recordingTimeNs) != SnapTray::SessionClock::Admission::Accepted) {
        return false;
    }
    if (paced && m_framePacer.onFrame(recordingTimeNs) != SnapTray::FramePacer::Decision::Emit) {
        return false;
    }
    *encoderTimeMs = SnapTray::SessionClock::toEncoderMs(recordingTimeNs);
    return true;
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

        constexpr int kTicksPerHealthCheck = 1000 / kDurationUpdate;
        if (++m_healthTicks >= kTicksPerHealthCheck) {
            m_healthTicks = 0;
            checkHealth(effectiveElapsed);
        }

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

    {
        QMutexLocker lock(&m_durationMutex);
        m_acceptAsyncFrames = false;
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
        m_mediaClockPaused = true;
        m_framePacer.notePause();
    }
    m_sessionClock.pause(SnapTray::captureClockNowNs());

    if (m_windowTimelineRecorder) {
        m_windowTimelineRecorder->pause();
    }
    if (m_cursorRecorder) {
        m_cursorRecorder->pause();
    }
    if (m_keyRecorder) {
        m_keyRecorder->pause();
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
        m_mediaClockPaused = false;
    }
    m_sessionClock.resume(SnapTray::captureClockNowNs());

    // Resume audio capture
    if (m_audioEngine) {
        m_audioEngine->resume();
    }

    if (m_captureEngine && m_captureEngine->deliversFrames()) {
        const quint64 generation = m_startGeneration;
        QTimer::singleShot(kOverlayRenderDelay, this, [this, generation] {
            if (generation != m_startGeneration || m_state != State::Recording) return;
            QMutexLocker lock(&m_durationMutex);
            m_acceptAsyncFrames = true;
        });
    }
    // Resume frame capture timer
    if (m_captureTimer) {
        m_captureTimer->start(1000 / m_frameRate);
    }

    // After the pause duration above is accounted for, so the immediate sample uses media time.
    if (m_windowTimelineRecorder) {
        m_windowTimelineRecorder->resume();
    }
    if (m_cursorRecorder) {
        m_cursorRecorder->resume();
    }
    if (m_keyRecorder) {
        m_keyRecorder->resume();
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

bool RecordingManager::showRecordingControls()
{
    if (!m_controlBar) {
        return false;
    }
    switch (m_state) {
    case State::Preparing:
    case State::Countdown:
        break;
    case State::Recording:
    case State::Paused:
        // These platforms cannot keep floating controls out of the video.
        if (SnapTray::currentPlatformCapabilities().recordingControlsInTray) {
            return false;
        }
        break;
    case State::Idle:
    case State::Encoding:
    case State::Previewing:
        return false;
    }

    m_controlBar->show();
    m_controlBar->raiseAboveMenuBar();
    // Position after show(), as at start, to undo a drag off the recorded screen.
    m_controlBar->positionNear(m_recordingRegion);
    return true;
}

void RecordingManager::stopRecording()
{
    if (m_state == State::Preparing || m_state == State::Countdown) {
        cancelRecording();
        return;
    }
    if (m_state != State::Recording && m_state != State::Paused) {
        return;
    }
    if (!m_stopReason) {
        m_stopReason = SnapTray::RecordingStopReason::User;
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

    ++m_startGeneration;
    m_permissionPending = false;
    m_collectStartupAudioWarnings = false;
    m_startupAudioWarnings.clear();
    // A cancelled recording is discarded on purpose: nothing to recover.
    endSession(true);

    // Cancel countdown overlay if active
    if (m_countdownOverlay) {
        m_countdownOverlay->close();
        m_countdownOverlay->deleteLater();
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
    m_windowTimelineRecorder.reset();
    dropRecordingSidecars(true);

    // Stop encoding worker (aborts encoding)
    if (m_encodingWorker || m_encodingThread) {
        teardownEncodingWorker(true);
    }
    m_usingNativeEncoder = false;

    setState(State::Idle);
    emit recordingCancelled();
}

void RecordingManager::stopFrameCapture()
{
    {
        QMutexLocker lock(&m_durationMutex);
        m_acceptAsyncFrames = false;
        if (m_health && m_sessionClock.isStarted()) {
            m_health->setClockCounters(clockCountersLocked());
        }
        if (m_sessionClock.isStarted()) {
            const auto clock = m_sessionClock.stats();
            const auto pacing = m_framePacer.stats();
            qDebug() << "RecordingManager: frames accepted" << clock.accepted
                     << "before start" << clock.beforeStart << "paused" << clock.paused
                     << "duplicate" << clock.duplicates << "backward" << clock.backwards
                     << "| paced: emitted" << pacing.emitted << "dropped" << pacing.dropped
                     << "late" << pacing.late << "stalls" << pacing.stalls
                     << "longest gap ms" << pacing.longestGapNs / 1000000;
        }
    }
    // Frames still in flight from a capture thread are rejected from here on.
    m_sessionClock.stop();
    stopDiskMonitor();
    unwatchTargetScreen();
    if (m_healthPolicy.isWarning()) {
        m_healthPolicy.reset();
        emit droppingFramesChanged(false);
    }
    if (m_windowTimelineRecorder) {
        if (m_windowTimelineRecorder->isRunning()) {
            m_windowTimelineRecorder->sampleNow(); // capture the final window layout
        }
        m_windowTimelineRecorder->stop();
    }
    if (m_cursorRecorder) {
        // Last position, held buttons released, file closed.
        m_cursorRecorder->stop();
    }
    if (m_keyRecorder) {
        m_keyRecorder->stop();
    }

    // Stop timers
    if (m_captureTimer) {
        m_captureTimer->stop();
        m_captureTimer.reset();
    }

    if (m_durationTimer) {
        m_durationTimer->stop();
        m_durationTimer.reset();
    }

    // Keep audio teardown before EncodingWorker teardown. AudioEnginePtr closes
    // and drains any in-flight DirectConnection callback before reset returns.
    if (m_audioEngine) {
        m_audioEngine.reset();
    }

    // Stop capture engine
    // Custom deleter handles disconnect + stop() + deleteLater()
    m_captureEngine.reset();

    if (m_controlBar) {
        m_controlBar->close();
        m_controlBar->deleteLater();
        m_controlBar = nullptr;
    }
}

void RecordingManager::cleanupRecording()
{
    stopFrameCapture();

    // Stop and clean up encoding worker
    if (m_encodingWorker || m_encodingThread) {
        teardownEncodingWorker(true);
    }
    m_usingNativeEncoder = false;

}

void RecordingManager::cleanupAudio()
{
    // Custom deleter handles disconnect + stop() + deleteLater()
    m_audioEngine.reset();
}

void RecordingManager::onEncodingFinished(bool success, const QString &outputPath)
{
    if (m_state != State::Encoding) {
        qWarning() << "RecordingManager: Ignoring encoding finished in non-encoding state"
                   << static_cast<int>(m_state);
        return;
    }

    // The encoder has drained its queue: the counters are final.
    if (m_health) {
        const auto snapshot = m_health->snapshot(SnapTray::captureClockNowNs());
        m_lastDiagnostics = SnapTray::RecordingHealth::formatDiagnostics(snapshot);
        if (!m_cursorDiagnostics.isEmpty()) {
            m_lastDiagnostics += QLatin1Char('\n') + m_cursorDiagnostics;
            if (m_cursorRecorder && m_cursorRecorder->truncated()) {
                m_lastDiagnostics += QStringLiteral(" (size limit reached)");
            }
        }
        qDebug().noquote() << m_lastDiagnostics;
        emit diagnosticsAvailable();
    }

    // Get error message if encoding failed
    QString errorMsg;
    if (!success) {
        errorMsg = tr("Failed to encode video");
    }

    // Read before endSession() resets the session.
    const bool crashSafeOutput = m_session.fragmented;

    // Clean up encoding worker
    teardownEncodingWorker(false);
    finishWindowTimeline(outputPath, success);
    // The pointer sidecar stays with a finished file, or with a crash-safe
    // part that recovery may offer; otherwise there is no video to go with.
    dropRecordingSidecars(!success && !(crashSafeOutput && QFileInfo(outputPath).size() > 0));
    m_usingNativeEncoder = false;
    if (success) {
        // A finished file: if SnapTray stops before it is saved, it is still
        // offered for recovery, complete.
        updateSession(SnapTray::RecordingSessionInfo::State::Finished);
    } else {
        // Keep the manifest: if a playable part exists, the next scan offers it.
        endSession(false);
    }

    if (success) {
        const bool showPreview = m_startSettings.showPreview;

        if (showPreview) {
            QString tempPath = outputPath;
            int defaultOutputFormat = m_defaultOutputFormat;
            const bool recordedAsIntermediate = m_recordedAsIntermediate;
            QMetaObject::invokeMethod(this, [this, tempPath, defaultOutputFormat, recordedAsIntermediate]() {
                m_tempVideoPath = tempPath;
                setState(State::Previewing);
                emit previewRequested(tempPath, defaultOutputFormat, recordedAsIntermediate);
            }, Qt::QueuedConnection);
        } else {
            // Existing flow: go directly to save dialog
            // The dialog runs a nested event loop, and a new recording may
            // start meanwhile (#172 review): this recording's session (manifest
            // and lock) leaves the manager and is completed by its own dialog.
            const QString sessionPath = m_session.videoPath;
            const std::shared_ptr<QLockFile> sessionLock(m_sessionLock.release());
            m_session = {};
            setState(State::Idle);
            QString tempPath = outputPath;
            QMetaObject::invokeMethod(this, [this, tempPath, sessionPath, sessionLock]() {
                QString savedPath;
                const SaveOutcome outcome = showSaveDialog(tempPath, QSize(), false, &savedPath);
                if (outcome == SaveOutcome::Saved && !savedPath.isEmpty()) {
                    emit quickRecordingSaved(savedPath);
                }
                // Saved or discarded: nothing to recover. A failed save keeps
                // the file in the temp folder, so its manifest stays.
                if (outcome != SaveOutcome::Failed && !sessionPath.isEmpty()) {
                    SnapTray::RecordingSession::remove(sessionPath);
                }
                if (sessionLock) sessionLock->unlock();
            }, Qt::QueuedConnection);
        }
    } else {
        setState(State::Idle);
        // Say what was kept: a crash-safe recording may leave a playable part
        // (offered for recovery); otherwise nothing usable remains.
        const QString nothingKept = errorMsg + QLatin1Char(' ') + tr("Nothing usable could be kept.");
        if (!crashSafeOutput || QFileInfo(outputPath).size() <= 0) {
            emit recordingError(nothingKept);
            return;
        }
        // A non-empty file is not yet a playable one (headers only, a torn
        // first fragment): ask the probe the recovery scan uses, off the GUI
        // thread, before promising recovery (#173 review).
        const QString partKept = tr("The recording could not be finished. The part recorded so far was kept; "
                                    "recover it from the tray menu.");
        const auto playable = m_isRecordingPlayable;
        const QPointer<RecordingManager> guard(this);
        (void)QtConcurrent::run([guard, outputPath, playable, partKept, nothingKept]() {
            const bool kept = playable && playable(outputPath);
            QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, kept, partKept, nothingKept]() {
                if (!guard) return;
                emit guard->recordingError(kept ? partKept : nothingKept);
                if (kept) guard->scanForRecoveries();
            }, Qt::QueuedConnection);
        });
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
    // Saved or discarded: the file is gone and so is its manifest. A file
    // still there (SnapTray is quitting) keeps its manifest for recovery.
    endSession(!QFileInfo::exists(m_tempVideoPath));
    m_tempVideoPath.clear();
}

RecordingManager::SaveOutcome RecordingManager::triggerSaveDialog(const QString &videoPath,
                                                                  const QSize &outputSize,
                                                                  QString *savedPath)
{
    return showSaveDialog(videoPath, outputSize, true, savedPath);
}

RecordingManager::SaveOutcome RecordingManager::showSaveDialog(const QString &tempOutputPath,
                                                               const QSize &outputSize,
                                                               bool keepOnCancel,
                                                               QString *savedPath)
{
    // Once saved or deleted, the temp recording stops being a preview input
    // and its window-timeline sidecar goes with it. A file the preview keeps
    // (cancelled or failed save) keeps its sidecar too.
    bool keepSidecar = false;
    const auto dropSidecar = qScopeGuard([&tempOutputPath, &keepSidecar]() {
        if (!keepSidecar) {
            SnapTray::WindowTimelineSidecar::remove(tempOutputPath);
            SnapTray::CursorTimelineSidecar::remove(tempOutputPath);
            SnapTray::KeyTimelineSidecar::remove(tempOutputPath);
            SnapTray::AudioTracks::remove(tempOutputPath);
        }
    });

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
    // A cropped export is named by its own size; other saves by the region.
    const QSize nameSize = outputSize.isEmpty() ? m_recordingRegion.size() : outputSize;
    context.width = nameSize.width();
    context.height = nameSize.height();
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
        // Never into a kept project's folder (PR #203 review): ask instead.
        if (!isAllowedSaveDestination(savePath)) attempt = maxAttempts;
        while (attempt < maxAttempts) {
            if (QFile::rename(tempOutputPath, savePath)) {
                syncFile(savePath);
                if (savedPath) *savedPath = savePath;
                emit recordingStopped(savePath);
                return SaveOutcome::Saved;
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
        if (isAllowedSaveDestination(savePath) && QFile::copy(tempOutputPath, savePath)) {
            QFile::remove(tempOutputPath);
            syncFile(savePath);
            if (savedPath) *savedPath = savePath;
            emit recordingStopped(savePath);
            return SaveOutcome::Saved;
        }

        if (!renderError.isEmpty()) {
            qWarning() << "RecordingManager: template warning:" << renderError;
        }
        // If auto-save fails, fall through to show dialog
        qWarning() << "RecordingManager: Auto-save failed, showing save dialog";
    }

    // Show save dialog
    QString filter;
    if (extension == "gif") filter = tr("GIF Files (*.gif)");
    else if (extension == "webp") filter = tr("WebP Files (*.webp)");
    else filter = tr("MP4 Files (*.mp4)");
    const QString defaultFileName = FilenameTemplateEngine::buildUniqueFilePath(
        outputDir, templateValue, context, 1);

    const QString dialogPath = NativeFileDialogUtils::getSaveFileName(
        nullptr,
        tr("Save Recording"),
        defaultFileName,
        QStringList{filter});
    const QString savePath = forceFileExtension(dialogPath, extension);

    if (savePath.isEmpty()) {
        if (keepOnCancel) {
            // Back to the preview with the recording intact.
            keepSidecar = true;
            return SaveOutcome::Cancelled;
        }
        // No preview to return to: cancelling discards the temp file.
        QFile::remove(tempOutputPath);
        emit recordingCancelled();
        return SaveOutcome::Cancelled;
    }
    // A kept project's folder holds its source recording: a save there could
    // replace it with an edited export, losing the original (PR #203 review).
    if (!isAllowedSaveDestination(savePath)) {
        qWarning() << "RecordingManager: refusing to save inside the projects folder:" << savePath;
        keepSidecar = keepOnCancel;
        emit recordingError(tr("Recordings cannot be saved in the Recording Library's project folder.")
                            + QLatin1Char(' ')
                            + (keepOnCancel ? tr("Your recording is kept; choose another location.")
                                            : tr("The recording is kept at %1.")
                                                  .arg(QDir::toNativeSeparators(tempOutputPath))));
        return SaveOutcome::Failed;
    }
    // Move/rename the file to chosen location
    if (savePath != tempOutputPath) {
        // A copy (another volume, or replacing an existing file, #171 review)
        // needs room for the whole file; renaming to a new name on the same
        // volume needs none.
        const QString destinationDir = QFileInfo(savePath).absolutePath();
        const qint64 fileBytes = qMax<qint64>(0, QFileInfo(tempOutputPath).size());
        const bool copies = SnapTray::RecordingFileUtils::saveCopiesData(tempOutputPath, savePath);
        const qint64 destinationFree = copies && m_freeBytesForPath ? m_freeBytesForPath(destinationDir) : -1;
        if (copies && destinationFree >= 0
            && destinationFree < fileBytes + SnapTray::DiskSpaceGuard::kFloorBytes) {
            qWarning() << "RecordingManager: destination too small:" << destinationFree << "for" << fileBytes;
            keepSidecar = keepOnCancel;
            const QString room = tr("Not enough free space at the chosen location (%1 free, %2 needed).")
                                     .arg(QLocale().formattedDataSize(destinationFree),
                                          QLocale().formattedDataSize(fileBytes));
            emit recordingError(keepOnCancel
                ? room + QLatin1Char(' ') + tr("Your recording is kept; choose another location.")
                : room + QLatin1Char(' ') + tr("The recording is kept at %1.")
                                               .arg(QDir::toNativeSeparators(tempOutputPath)));
            return SaveOutcome::Failed;
        }
        QString saveError;
        if (SnapTray::RecordingFileUtils::replaceRecordingFile(
                tempOutputPath, savePath, &saveError)) {
            syncFile(savePath);
            if (savedPath) *savedPath = savePath;
            emit recordingStopped(savePath);
            return SaveOutcome::Saved;
        }
        qWarning() << "RecordingManager: Failed to save recording to:" << savePath
                   << saveError;
        keepSidecar = keepOnCancel;
        emit recordingError(keepOnCancel
            ? tr("Failed to save recording to selected location")
            : tr("Failed to save recording to selected location") + QLatin1Char(' ')
                  + tr("The recording is kept at %1.").arg(QDir::toNativeSeparators(tempOutputPath)));
        return SaveOutcome::Failed;
    }
    syncFile(tempOutputPath);
    if (savedPath) *savedPath = tempOutputPath;
    emit recordingStopped(tempOutputPath);
    return SaveOutcome::Saved;
}

void RecordingManager::onEncodingError(const QString &error)
{
    // Stop all capture-related resources (timers, capture engine, UI overlays)
    // This is needed when error occurs during recording (not just during finish/encoding)
    stopFrameCapture();

    // Clean up encoding worker
    if (m_encodingWorker || m_encodingThread) {
        teardownEncodingWorker(true);
    }
    m_usingNativeEncoder = false;
    m_windowTimelineRecorder.reset();
    dropRecordingSidecars(true);

    setState(State::Idle);
    emit recordingError(error);
}

QString RecordingManager::generateOutputPath() const
{
    // Use system temp directory for recording (file is moved to user-selected location after save dialog)
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);

    // Get output format from settings
    const int formatInt = m_startSettings.outputFormat;
    const bool showPreview = m_startSettings.showPreview;

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

void RecordingManager::startWindowTimeline()
{
    m_windowTimelineRecorder.reset();
    if (!m_startSettings.showPreview || !m_windowFrameMapping.isValid()) {
        return;
    }
    // The same clock that stamps the encoded frames: raw elapsed minus pauses.
    auto mediaTime = [this]() { return mediaTimeMs(); };
    m_windowTimelineRecorder = std::make_unique<SnapTray::WindowTimelineRecorder>(
        m_createWindowEnumerator(m_targetScreen.data()), mediaTime, m_windowFrameMapping, nullptr);
    m_windowTimelineRecorder->start();
}

void RecordingManager::finishWindowTimeline(const QString& outputPath, bool success)
{
    if (!m_windowTimelineRecorder) {
        return;
    }
    m_windowTimelineRecorder->stop();
    const SnapTray::WindowTimeline timeline = m_windowTimelineRecorder->timeline();
    m_windowTimelineRecorder.reset();
    if (!success || outputPath.isEmpty()) {
        return;
    }
    if (!timeline.hasWindows()) {
        qDebug() << "RecordingManager: window timeline has no windows; no sidecar written";
        return;
    }
    if (!SnapTray::WindowTimelineSidecar::write(outputPath, timeline)) {
        qWarning() << "RecordingManager: window timeline sidecar not written; preview keeps free-rect cropping";
    }
}

qint64 RecordingManager::mediaTimeMs()
{
    // The clock that stamps the encoded frames: raw elapsed minus pauses.
    // From the moment a pause begins it holds at that boundary, so events
    // recorded while pausing never land after the first ones after resume.
    QMutexLocker locker(&m_durationMutex);
    const qint64 raw = m_mediaClockPaused ? m_pauseStartTime : m_elapsedTimer.elapsed();
    return qMax<qint64>(0, raw - m_pausedDuration);
}

void RecordingManager::startCursorTimeline()
{
    dropCursorTimeline(false);
    m_cursorDiagnostics.clear();
    // Like the window timeline: only a previewed recording has a use for it.
    if (!m_startSettings.showPreview || !m_windowFrameMapping.isValid() || m_session.videoPath.isEmpty()) {
        return;
    }
    auto probe = m_createCursorProbe ? m_createCursorProbe() : nullptr;
    if (!probe) {
        return;
    }
    auto mediaTime = [this]() { return mediaTimeMs(); };
    const bool cursorInVideo = m_captureEngine && m_captureEngine->capturesCursor();
    m_cursorSidecarVideo = m_session.videoPath;
    m_cursorRecorder = std::make_unique<SnapTray::CursorTimelineRecorder>(
        std::move(probe), mediaTime, m_windowFrameMapping,
        SnapTray::CursorTimelineSidecar::pathFor(m_cursorSidecarVideo), cursorInVideo, nullptr);
    const auto caps = m_cursorRecorder->capabilities();
    m_cursorDiagnostics = QStringLiteral("cursor: %1, shapes %2, clicks %3")
                              .arg(m_cursorRecorder->probeName(),
                                   caps.shapes ? QStringLiteral("yes") : QStringLiteral("no"),
                                   caps.clicks ? QStringLiteral("yes") : QStringLiteral("no"));
    if (!m_cursorRecorder->start()) {
        qWarning() << "RecordingManager: pointer sidecar not written; the recording continues without it";
        m_cursorDiagnostics += QStringLiteral(" (not recorded: sidecar could not be written)");
        dropCursorTimeline(true);
        return;
    }
    qDebug().noquote() << "RecordingManager:" << m_cursorDiagnostics;
    startKeyTimeline();
}

void RecordingManager::startKeyTimeline()
{
    // Off unless the user switched it on before this recording (#187).
    const auto mode = SnapTray::KeyRecordingMode(m_startSettings.keyRecordingMode);
    if (mode == SnapTray::KeyRecordingMode::Off || m_cursorSidecarVideo.isEmpty()) return;
    auto probe = m_createKeyProbe ? m_createKeyProbe() : nullptr;
    if (!probe) {
        qWarning() << "RecordingManager: keys cannot be read here; recording without shortcut hints";
        emit recordingWarning(tr("Keyboard recording is unavailable. Check input monitoring permissions. The recording will continue without key hints."));
        return;
    }
    auto mediaTime = [this]() { return mediaTimeMs(); };
#ifdef Q_OS_MACOS
    constexpr bool kMacSymbols = true;
#else
    constexpr bool kMacSymbols = false;
#endif
    m_keyRecorder = std::make_unique<SnapTray::KeyTimelineRecorder>(
        std::move(probe), mediaTime, mode, SnapTray::KeyTimelineSidecar::pathFor(m_cursorSidecarVideo), kMacSymbols,
        nullptr);
    if (!m_keyRecorder->start()) {
        qWarning() << "RecordingManager: key sidecar not written; the recording continues without it";
        emit recordingWarning(tr("Key hints could not be saved. The recording will continue without them."));
        m_keyRecorder.reset();
        SnapTray::KeyTimelineSidecar::remove(m_cursorSidecarVideo);
    }
}

void RecordingManager::startAudioTracks()
{
    closeAudioTracks();
    // Only a previewed recording with both sources has two tracks to keep apart.
    if (!m_audioEngine || !m_startSettings.showPreview || m_session.videoPath.isEmpty()
        || m_audioEngine->audioSource() != IAudioCaptureEngine::AudioSource::Both
        || !m_audioEngine->supportsSeparateTracks()) {
        if (m_audioEngine) m_audioEngine->setSeparateTracks(false);
        return;
    }
    using SnapTray::AudioTracks::Track;
    auto microphone = std::make_shared<SnapTray::AudioTrackWriter>(
        SnapTray::AudioTracks::pathFor(m_session.videoPath, Track::Microphone));
    auto system = std::make_shared<SnapTray::AudioTrackWriter>(
        SnapTray::AudioTracks::pathFor(m_session.videoPath, Track::SystemAudio));
    if (!microphone->open() || !system->open()) {
        qWarning() << "RecordingManager: separate audio tracks not written; the mixed track is kept";
        SnapTray::AudioTracks::remove(m_session.videoPath);
        m_audioEngine->setSeparateTracks(false);
        return;
    }
    m_microphoneTrack = microphone;
    m_systemAudioTrack = system;
    m_audioTracksVideo = m_session.videoPath;
    m_audioEngine->setSeparateTracks(true);
    // On the capture thread, like the mixed track.
    connect(m_audioEngine.get(), &IAudioCaptureEngine::sourceAudioReady, m_audioEngine.get(),
            [microphone, system](const QByteArray& mic, const QByteArray& sys, qint64 startFrame) {
                microphone->write(mic, startFrame);
                system->write(sys, startFrame);
            },
            Qt::DirectConnection);
}

void RecordingManager::closeAudioTracks()
{
    // A track that stopped writing (a full disk) is shorter than the
    // recording but still looks valid: the preview and exports would use it
    // instead of the complete mixed track. The pair goes, so the recording
    // keeps its own audio (PR #202 review). Finalize both files before
    // checking: the last buffered write may fail only on close.
    if (m_microphoneTrack) m_microphoneTrack->close();
    if (m_systemAudioTrack) m_systemAudioTrack->close();
    const bool failed = (m_microphoneTrack && m_microphoneTrack->failed())
        || (m_systemAudioTrack && m_systemAudioTrack->failed());
    m_microphoneTrack.reset();
    m_systemAudioTrack.reset();
    if (failed && !m_audioTracksVideo.isEmpty()) {
        qWarning() << "RecordingManager: a separate audio track could not be written; the mixed track is kept";
        SnapTray::AudioTracks::remove(m_audioTracksVideo);
    }
}

bool RecordingManager::isRecordingKeys() const
{
    return m_keyRecorder != nullptr;
}

void RecordingManager::dropCursorTimeline(bool removeFile)
{
    if (m_cursorRecorder) {
        m_cursorRecorder->stop();
        m_cursorRecorder.reset();
    }
    if (m_keyRecorder) {
        m_keyRecorder->stop();
        m_keyRecorder.reset();
    }
    if (removeFile && !m_cursorSidecarVideo.isEmpty()) {
        SnapTray::CursorTimelineSidecar::remove(m_cursorSidecarVideo);
        SnapTray::KeyTimelineSidecar::remove(m_cursorSidecarVideo);
    }
    m_cursorSidecarVideo.clear();
}

void RecordingManager::dropRecordingSidecars(bool removeFiles)
{
    // The audio tracks follow the recording, not the pointer timeline:
    // (re)starting the pointer must never close them (PR #202 review).
    dropCursorTimeline(removeFiles);
    closeAudioTracks();
    if (removeFiles && !m_audioTracksVideo.isEmpty()) SnapTray::AudioTracks::remove(m_audioTracksVideo);
    m_audioTracksVideo.clear();
}

void RecordingManager::cleanupStaleTempFiles()
{
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QDir dir(tempDir);

    // Clean up SnapTray recording temp files older than 24 hours
    QStringList filters;
    filters << "SnapTray_Recording_*.mp4" << "SnapTray_Recording_*.gif" << "SnapTray_Recording_*.webp"
            << QStringLiteral("SnapTray_Recording_*") + QLatin1String(SnapTray::WindowTimelineSidecar::kSuffix)
            << QStringLiteral("SnapTray_Recording_*") + QLatin1String(SnapTray::CursorTimelineSidecar::kSuffix)
            << QStringLiteral("SnapTray_Recording_*") + QLatin1String(SnapTray::KeyTimelineSidecar::kSuffix)
            << QStringLiteral("SnapTray_Recording_*") + QLatin1String(SnapTray::AudioTracks::kMicrophoneSuffix)
            << QStringLiteral("SnapTray_Recording_*") + QLatin1String(SnapTray::AudioTracks::kSystemAudioSuffix);

    QFileInfoList files = dir.entryInfoList(filters, QDir::Files);
    QDateTime threshold = QDateTime::currentDateTime().addDays(-1);

    int cleanedCount = 0;
    for (const QFileInfo &file : files) {
        // A recording with a session manifest is either active or waiting to
        // be recovered: only the user's choice (recover or discard) removes it.
        // A sidecar belongs to the video its name extends.
        QString owner = file.absoluteFilePath();
        for (const char* suffix : {SnapTray::WindowTimelineSidecar::kSuffix, SnapTray::CursorTimelineSidecar::kSuffix,
                                   SnapTray::KeyTimelineSidecar::kSuffix, SnapTray::AudioTracks::kMicrophoneSuffix,
                                   SnapTray::AudioTracks::kSystemAudioSuffix}) {
            if (owner.endsWith(QLatin1String(suffix))) {
                owner.chop(int(qstrlen(suffix)));
                break;
            }
        }
        if (SnapTray::RecordingSession::hasSession(owner)) {
            continue;
        }
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
