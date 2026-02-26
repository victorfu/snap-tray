#include "scroll/ScrollCaptureSession.h"

#include "capture/ICaptureEngine.h"
#include "platform/WindowLevel.h"
#include "qml/QmlRecordingBoundary.h"
#include "qml/QmlScrollControlBar.h"
#include "qml/QmlScrollPreviewWindow.h"
#include "settings/RegionCaptureSettingsManager.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QScreen>
#include <QTimer>
#include <QtConcurrent>
#include <utility>

namespace {

constexpr int kCaptureTickIntervalMs = 66;
constexpr int kWatchdogIntervalMs = 600;
constexpr qint64 kFirstFrameTimeoutMs = 6000;
constexpr qint64 kTickStallTimeoutMs = 2500;
constexpr qint64 kFrameStallTimeoutMs = 3000;
constexpr int kPreviewWidth = 320;
constexpr int kPreviewHeight = 192;
constexpr int kPreviewStitchedIntervalMs = 120;
constexpr double kLowConfidenceThreshold = 0.92;
constexpr int kLowConfidenceWarningFrames = 2;
constexpr int kInitialBadFrameTolerance = 2;
constexpr int kTraceCapacity = 384;

QString qualityText(StitchQuality quality)
{
    switch (quality) {
    case StitchQuality::Good:
        return QStringLiteral("GOOD");
    case StitchQuality::PartiallyGood:
        return QStringLiteral("PARTIAL");
    case StitchQuality::Bad:
        return QStringLiteral("BAD");
    case StitchQuality::NoChange:
        return QStringLiteral("NO CHANGE");
    }
    return QStringLiteral("UNKNOWN");
}

QString stitchRejectionText(StitchRejectionCode code)
{
    switch (code) {
    case StitchRejectionCode::None:
        return QStringLiteral("none");
    case StitchRejectionCode::LowConfidence:
        return QStringLiteral("low_confidence");
    case StitchRejectionCode::DuplicateTail:
        return QStringLiteral("duplicate_tail");
    case StitchRejectionCode::DuplicateLoop:
        return QStringLiteral("duplicate_loop");
    case StitchRejectionCode::ImplausibleAppend:
        return QStringLiteral("implausible_append");
    case StitchRejectionCode::InvalidFrame:
        return QStringLiteral("invalid_frame");
    case StitchRejectionCode::OverlapTooSmall:
        return QStringLiteral("overlap_too_small");
    }
    return QStringLiteral("none");
}

ScrollStitchOptions buildScrollStitchOptions()
{
    ScrollStitchOptions options;
    auto &settings = RegionCaptureSettingsManager::instance();
    options.goodThreshold = settings.loadScrollGoodThreshold();
    options.partialThreshold = settings.loadScrollPartialThreshold();
    options.minScrollAmount = settings.loadScrollMinScrollAmount();
    options.maxFrames = settings.loadScrollMaxFrames();
    options.maxOutputPixels = settings.loadScrollMaxOutputPixels();
    options.maxOutputHeightPx = 30000;
    options.autoIgnoreBottomEdge = settings.loadScrollAutoIgnoreBottomEdge();
    options.usePoissonSeam = false;
    return options;
}

} // namespace

ScrollCaptureSession::ScrollCaptureSession(const QRect &region,
                                           QScreen *screen,
                                           QObject *parent,
                                           const Dependencies &deps)
    : QObject(parent)
    , m_region(region)
    , m_screen(screen)
    , m_deps(deps)
    , m_stitchEngine(buildScrollStitchOptions())
{
}

ScrollCaptureSession::~ScrollCaptureSession()
{
    m_running = false;
    shutdownRuntime();
}

void ScrollCaptureSession::start()
{
    if (m_running) {
        return;
    }

    if (m_region.isEmpty() || m_screen.isNull() || !QGuiApplication::screens().contains(m_screen.data())) {
        emit failed(tr("Invalid capture region or screen."));
        return;
    }

    m_captureEngine = m_deps.captureEngine;
    if (!m_captureEngine) {
        m_captureEngine = ICaptureEngine::createBestEngine(this);
        m_ownsCaptureEngine = true;
    }
    if (!m_captureEngine) {
        emit failed(tr("Failed to create capture engine."));
        return;
    }

    m_stitchEngine = ScrollStitchEngine(buildScrollStitchOptions());
    m_stitchStarted = false;
    m_running = false;
    m_finishing = false;
    m_paused = false;
    m_initialCombineAttempts = 0;
    m_initialBadCount = 0;
    m_initialFrameMisses = 0;
    m_manualReason.clear();

    m_startedElapsedMs = 0;
    m_lastTickElapsedMs = 0;
    m_lastFrameElapsedMs = -1;

    m_previewDirty = true;
    m_stitchedPreviewDirty = true;
    m_lastPreviewUpdateElapsedMs = -1;
    m_lastStitchedPreviewUpdateElapsedMs = -1;

    m_lastQuality = StitchQuality::NoChange;
    m_lastQualityConfidence = 0.0;
    m_lastStrategy = StitchMatchStrategy::RowDiff;
    m_cachedPreviewMeta = StitchPreviewMeta();
    m_cachedFrameCount = 0;
    m_cachedTotalHeight = 0;

    m_consecutiveLowConfidenceCount = 0;
    m_blockedByLowConfidence = false;
    m_slowScrollWarningActive = false;

    ++m_appendGeneration;
    m_appendInFlight = false;
    m_appendInFlightGeneration = 0;
    m_pendingAppendFrame = QImage();
    m_hasPendingAppendFrame = false;
    m_pendingExternalDyHint = 0;

    m_sessionTrace.clear();
    m_runtimeClock.invalidate();

    if (!m_captureEngine->setRegion(m_region, m_screen.data())) {
        emit failed(tr("Failed to configure capture region."));
        return;
    }

    if (m_deps.enableUi) {
        setupUi();
    }

    m_captureEngine->setExcludedWindows(collectExcludedWindowIds());

    if (!m_captureEngine->start()) {
        traceEvent(QStringLiteral("capture_start_failed"));
        shutdownRuntime();
        emit failed(tr("Failed to start capture engine."));
        return;
    }

    m_running = true;
    m_runtimeClock.start();
    m_startedElapsedMs = m_runtimeClock.elapsed();

    const QImage firstFrame = m_captureEngine->captureFrame();
    if (!firstFrame.isNull()) {
        m_lastFrameElapsedMs = m_runtimeClock.elapsed();
        {
            QMutexLocker locker(&m_stitchMutex);
            m_stitchStarted = m_stitchEngine.start(firstFrame);
            if (m_stitchStarted) {
                m_cachedFrameCount = m_stitchEngine.frameCount();
                m_cachedTotalHeight = m_stitchEngine.totalHeight();
                m_cachedPreviewMeta = m_stitchEngine.previewMeta();
            }
        }
        if (!m_stitchStarted) {
            m_running = false;
            traceEvent(QStringLiteral("stitch_init_failed"));
            exportTraceSnapshot(QStringLiteral("stitch_init_failed"));
            shutdownRuntime();
            emit failed(tr("Failed to initialize stitch engine."));
            return;
        }
    }

    updatePreview(true);
    updateQualityLabel(StitchQuality::Good, 1.0);
    updateStatusLabel();
    updateProgressLabel();

    m_captureTimer = new QTimer(this);
    m_captureTimer->setObjectName(QStringLiteral("scrollCaptureTickTimer"));
    connect(m_captureTimer, &QTimer::timeout, this, &ScrollCaptureSession::onCaptureTick);
    m_captureTimer->start(kCaptureTickIntervalMs);

    m_watchdogTimer = new QTimer(this);
    m_watchdogTimer->setObjectName(QStringLiteral("scrollCaptureWatchdogTimer"));
    connect(m_watchdogTimer, &QTimer::timeout, this, &ScrollCaptureSession::onWatchdogTick);
    m_watchdogTimer->start(kWatchdogIntervalMs);
    traceEvent(QStringLiteral("session_started"));
}

void ScrollCaptureSession::cancel()
{
    if (!m_running) {
        return;
    }

    exportTraceSnapshot(QStringLiteral("cancel"));
    m_running = false;
    shutdownRuntime();
    emit cancelled();
}

void ScrollCaptureSession::finish()
{
    if (!m_running || m_finishing) {
        return;
    }

    m_finishing = true;

    if (m_captureTimer) {
        m_captureTimer->stop();
    }
    if (m_watchdogTimer) {
        m_watchdogTimer->stop();
    }

    m_running = false;
    drainAppendBeforeFinish();

    QImage finalImage;
    {
        QMutexLocker locker(&m_stitchMutex);
        finalImage = m_stitchEngine.composeFinal();
    }
    if (finalImage.isNull()) {
        exportTraceSnapshot(QStringLiteral("finish_compose_failed"));
        shutdownRuntime();
        m_finishing = false;
        emit failed(tr("Failed to compose final scrolling capture."));
        return;
    }

    QPixmap output = QPixmap::fromImage(finalImage, Qt::NoOpaqueDetection);
    if (output.isNull()) {
        exportTraceSnapshot(QStringLiteral("finish_pixmap_failed"));
        shutdownRuntime();
        m_finishing = false;
        emit failed(tr("Failed to convert final scrolling capture image."));
        return;
    }
    if (m_screen) {
        output.setDevicePixelRatio(m_screen->devicePixelRatio());
    }

    exportTraceSnapshot(QStringLiteral("finish"));
    shutdownRuntime();
    m_finishing = false;
    emit captureReady(output, m_region.topLeft(), m_region);
}

void ScrollCaptureSession::pause()
{
    if (!m_running || m_paused) {
        return;
    }

    m_paused = true;
    updateStatusLabel();
    updateProgressLabel();
}

void ScrollCaptureSession::resume()
{
    if (!m_running || !m_paused) {
        return;
    }

    m_paused = false;
    updateStatusLabel();
    updateProgressLabel();
}

void ScrollCaptureSession::setupUi()
{
    auto *boundary = new SnapTray::QmlRecordingBoundary(this);
    boundary->setRegion(m_region);
    boundary->setBorderMode(SnapTray::QmlRecordingBoundary::BorderMode::ScrollCapture);
    boundary->show();
    boundary->setExcludedFromCapture(true);
    boundary->raiseAboveMenuBar();
    m_boundaryOverlay = boundary;

    m_controlBar = new SnapTray::QmlScrollControlBar(this);
    connect(m_controlBar, &SnapTray::QmlScrollControlBar::finishRequested,
            this, &ScrollCaptureSession::finish);
    connect(m_controlBar, &SnapTray::QmlScrollControlBar::cancelRequested,
            this, &ScrollCaptureSession::cancel);
    connect(m_controlBar, &SnapTray::QmlScrollControlBar::escapePressed,
            this, &ScrollCaptureSession::cancel);

    m_controlBar->positionNear(m_region, m_screen.data());
    m_controlBar->show();

    m_previewWindow = new SnapTray::QmlScrollPreviewWindow(this);
    m_previewWindow->positionNear(m_region, m_screen.data());
    m_previewWindow->show();

}

void ScrollCaptureSession::shutdownRuntime()
{
    m_finishing = false;

    if (m_appendWatcher && m_appendInFlight) {
        m_appendWatcher->waitForFinished();
    }

    ++m_appendGeneration;
    m_appendInFlight = false;
    m_hasPendingAppendFrame = false;
    m_pendingAppendFrame = QImage();
    m_pendingExternalDyHint = 0;

    if (m_captureTimer) {
        m_captureTimer->stop();
        m_captureTimer->deleteLater();
        m_captureTimer = nullptr;
    }

    if (m_watchdogTimer) {
        m_watchdogTimer->stop();
        m_watchdogTimer->deleteLater();
        m_watchdogTimer = nullptr;
    }

    cleanupUi();

    if (m_captureEngine) {
        m_captureEngine->stop();
        if (m_ownsCaptureEngine) {
            m_captureEngine->deleteLater();
        }
        m_captureEngine = nullptr;
        m_ownsCaptureEngine = false;
    }
}

void ScrollCaptureSession::drainAppendBeforeFinish()
{
    ++m_appendGeneration;
    const int finishingGeneration = m_appendGeneration;

    if (m_appendWatcher && m_appendInFlight) {
        m_appendWatcher->waitForFinished();
        m_appendInFlight = false;
        m_appendInFlightGeneration = finishingGeneration;
    }

    if (m_stitchStarted && m_hasPendingAppendFrame && !m_pendingAppendFrame.isNull()) {
        const QImage pendingFrame = m_pendingAppendFrame;
        m_pendingAppendFrame = QImage();
        m_hasPendingAppendFrame = false;
        const int consumedDyHint = m_pendingExternalDyHint;
        m_pendingExternalDyHint = 0;

        QMutexLocker locker(&m_stitchMutex);
        if (consumedDyHint > 0) {
            m_stitchEngine.setExternalEstimatedDyHint(consumedDyHint);
        }
        m_stitchEngine.append(pendingFrame);
    } else {
        m_pendingAppendFrame = QImage();
        m_hasPendingAppendFrame = false;
        m_pendingExternalDyHint = 0;
    }

    if (m_stitchStarted) {
        QMutexLocker locker(&m_stitchMutex);
        m_cachedFrameCount = m_stitchEngine.frameCount();
        m_cachedTotalHeight = m_stitchEngine.totalHeight();
        m_cachedPreviewMeta = m_stitchEngine.previewMeta();
    }
}

void ScrollCaptureSession::cleanupUi()
{
    if (m_boundaryOverlay) {
        m_boundaryOverlay->setExcludedFromCapture(false);
        m_boundaryOverlay->close();
        m_boundaryOverlay = nullptr;
    }

    if (m_previewWindow) {
        m_previewWindow->close();
        m_previewWindow->deleteLater();
        m_previewWindow = nullptr;
    }

    if (m_controlBar) {
        m_controlBar->close();
        m_controlBar->deleteLater();
        m_controlBar = nullptr;
    }
}

void ScrollCaptureSession::pushTraceEntry(const ScrollTraceEntry &entry)
{
    m_sessionTrace.push_back(entry);
    if (m_sessionTrace.size() > kTraceCapacity) {
        m_sessionTrace.remove(0, m_sessionTrace.size() - kTraceCapacity);
    }
}

void ScrollCaptureSession::traceEvent(const QString &event)
{
    ScrollTraceEntry entry;
    entry.elapsedMs = m_runtimeClock.isValid() ? m_runtimeClock.elapsed() : 0;
    entry.event = event;
    entry.blockedByLowConfidence = m_blockedByLowConfidence;
    pushTraceEntry(entry);
}

void ScrollCaptureSession::traceAppendResult(const StitchFrameResult &result, qint64 previewUpdateMs)
{
    ScrollTraceEntry entry;
    entry.elapsedMs = m_runtimeClock.isValid() ? m_runtimeClock.elapsed() : 0;
    entry.event = QStringLiteral("append_result");
    entry.quality = result.quality;
    entry.rejectionCode = result.rejectionCode;
    entry.appendHeight = result.appendHeight;
    entry.confidence = result.confidence;
    entry.rawNccScore = result.rawNccScore;
    entry.confidencePenalty = result.confidencePenalty;
    entry.appendPlausibilityScore = result.appendPlausibilityScore;
    entry.duplicateLoopDetected = result.duplicateLoopDetected;
    entry.blockedByLowConfidence = m_blockedByLowConfidence;
    entry.previewUpdateMs = previewUpdateMs;
    pushTraceEntry(entry);
}

void ScrollCaptureSession::exportTraceSnapshot(const QString &reason)
{
    if (m_sessionTrace.isEmpty()) {
        return;
    }

    const QString logRoot = QDir::homePath() + QStringLiteral("/Library/Logs/SnapTray");
    QDir dir(logRoot);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        return;
    }

    const QString fileName = QStringLiteral("scroll-session-%1.json")
                                 .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz")));
    QFile outFile(dir.filePath(fileName));
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }

    QJsonArray entries;
    for (const ScrollTraceEntry &entry : std::as_const(m_sessionTrace)) {
        QJsonObject item;
        item.insert(QStringLiteral("elapsedMs"), static_cast<qint64>(entry.elapsedMs));
        item.insert(QStringLiteral("event"), entry.event);
        item.insert(QStringLiteral("quality"), qualityText(entry.quality));
        item.insert(QStringLiteral("rejectionCode"), stitchRejectionText(entry.rejectionCode));
        item.insert(QStringLiteral("appendHeight"), entry.appendHeight);
        item.insert(QStringLiteral("confidence"), entry.confidence);
        item.insert(QStringLiteral("rawNccScore"), entry.rawNccScore);
        item.insert(QStringLiteral("confidencePenalty"), entry.confidencePenalty);
        item.insert(QStringLiteral("appendPlausibilityScore"), entry.appendPlausibilityScore);
        item.insert(QStringLiteral("duplicateLoopDetected"), entry.duplicateLoopDetected);
        item.insert(QStringLiteral("blockedByLowConfidence"), entry.blockedByLowConfidence);
        item.insert(QStringLiteral("previewUpdateMs"), static_cast<qint64>(entry.previewUpdateMs));
        entries.push_back(item);
    }

    QJsonObject root;
    root.insert(QStringLiteral("timestampUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    root.insert(QStringLiteral("reason"), reason);
    root.insert(QStringLiteral("region"), QStringLiteral("%1,%2 %3x%4")
                                        .arg(m_region.x())
                                        .arg(m_region.y())
                                        .arg(m_region.width())
                                        .arg(m_region.height()));
    root.insert(QStringLiteral("entries"), entries);

    outFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    outFile.close();
}

void ScrollCaptureSession::updatePreview(bool force)
{
    if (!m_previewWindow) {
        return;
    }

    const qint64 elapsedMs = m_runtimeClock.isValid() ? m_runtimeClock.elapsed() : 0;
    const qreal previewDpr = qMax<qreal>(1.0, m_screen ? m_screen->devicePixelRatio() : 1.0);
    const int requestWidth = qMax(1, qRound(static_cast<qreal>(kPreviewWidth) * previewDpr));
    const int requestHeight = qMax(1, qRound(static_cast<qreal>(kPreviewHeight) * previewDpr));

    const bool stitchedDue = force ||
        m_stitchedPreviewDirty ||
        (m_lastStitchedPreviewUpdateElapsedMs < 0) ||
        (elapsedMs - m_lastStitchedPreviewUpdateElapsedMs) >= kPreviewStitchedIntervalMs;

    if (!stitchedDue || m_appendInFlight) {
        return;
    }

    QImage stitchedPreview;
    {
        QMutexLocker locker(&m_stitchMutex);
        stitchedPreview = m_stitchEngine.preview(requestWidth, requestHeight);
    }
    if (stitchedPreview.isNull()) {
        m_previewDirty = true;
        return;
    }

    stitchedPreview.setDevicePixelRatio(previewDpr);
    m_previewWindow->setPreviewImage(stitchedPreview);
    m_lastStitchedPreviewUpdateElapsedMs = elapsedMs;
    m_lastPreviewUpdateElapsedMs = elapsedMs;
    m_stitchedPreviewDirty = false;
    m_previewDirty = false;
}

void ScrollCaptureSession::updateStatusLabel()
{
    if (!m_controlBar) {
        return;
    }

    QString status = tr("Manual");
    if (!m_stitchStarted) {
        status = tr("Manual: waiting first frame");
    } else if (m_paused) {
        status = tr("Paused");
    }

    if (!m_manualReason.isEmpty()) {
        status += tr(" | %1").arg(m_manualReason);
    }
    m_lastStatusLine = status;

    if (!m_controlBar->hasManualPosition()) {
        m_controlBar->positionNear(m_region, m_screen.data());
    }
    if (m_previewWindow) {
        m_previewWindow->positionNear(m_region, m_screen.data());
    }

    updateSlowScrollWarningUi();
}

void ScrollCaptureSession::updateProgressLabel()
{
    m_progressLine = tr("MAN | %1f | %2px").arg(m_cachedFrameCount).arg(m_cachedTotalHeight);
}

void ScrollCaptureSession::updateQualityLabel(StitchQuality quality, double confidence)
{
    m_lastQuality = quality;
    m_lastQualityConfidence = confidence;
}

void ScrollCaptureSession::updateSlowScrollWarningUi()
{
    if (!m_controlBar) {
        return;
    }

    // QML handles the pulsing animation — just set the boolean state.
    m_controlBar->setSlowScrollWarning(m_slowScrollWarningActive);
}

bool ScrollCaptureSession::handleAppendResult(const StitchFrameResult &appendResult)
{
    const bool lowConfidenceRejected =
        appendResult.rejectedByConfidence ||
        ((appendResult.quality == StitchQuality::Bad) &&
         appendResult.confidence > 0.0 &&
         appendResult.confidence < kLowConfidenceThreshold);

    if (lowConfidenceRejected) {
        ++m_consecutiveLowConfidenceCount;
        if (m_consecutiveLowConfidenceCount >= kLowConfidenceWarningFrames) {
            m_blockedByLowConfidence = true;
            m_slowScrollWarningActive = true;
            m_manualReason = tr("Please slow down scrolling");
            updateStatusLabel();
        }
        return true;
    }

    const bool recoveredAppend =
        appendResult.quality == StitchQuality::Good ||
        appendResult.quality == StitchQuality::PartiallyGood;
    if (m_blockedByLowConfidence &&
        (recoveredAppend || appendResult.confidence >= kLowConfidenceThreshold)) {
        m_blockedByLowConfidence = false;
        m_slowScrollWarningActive = false;
        m_manualReason.clear();
        updateStatusLabel();
    }
    m_consecutiveLowConfidenceCount = 0;

    if (appendResult.quality != StitchQuality::NoChange) {
        ++m_initialCombineAttempts;
        if (m_initialCombineAttempts <= kInitialBadFrameTolerance &&
            appendResult.quality == StitchQuality::Bad &&
            !appendResult.rejectedByConfidence) {
            ++m_initialBadCount;
            if (m_initialCombineAttempts == kInitialBadFrameTolerance &&
                m_initialBadCount == kInitialBadFrameTolerance) {
                m_running = false;
                traceEvent(QStringLiteral("initial_frames_unreliable"));
                exportTraceSnapshot(QStringLiteral("initial_frames_unreliable"));
                shutdownRuntime();
                emit failed(tr("Unable to stitch the first frames reliably."));
                return false;
            }
        }
    }

    return true;
}

void ScrollCaptureSession::queueAppendFrame(const QImage &frame)
{
    if (!m_running || !m_stitchStarted || frame.isNull()) {
        return;
    }

    m_pendingAppendFrame = frame;
    m_hasPendingAppendFrame = true;
    dispatchNextAppendIfIdle();
}

void ScrollCaptureSession::dispatchNextAppendIfIdle()
{
    if (!m_running || !m_stitchStarted || m_appendInFlight || !m_hasPendingAppendFrame) {
        return;
    }

    if (!m_appendWatcher) {
        m_appendWatcher = new QFutureWatcher<StitchFrameResult>(this);
        connect(m_appendWatcher, &QFutureWatcher<StitchFrameResult>::finished,
                this, &ScrollCaptureSession::onAppendTaskFinished);
    }

    QImage frame = m_pendingAppendFrame;
    m_pendingAppendFrame = QImage();
    m_hasPendingAppendFrame = false;
    const int consumedDyHint = m_pendingExternalDyHint;
    m_pendingExternalDyHint = 0;
    m_appendInFlight = true;
    m_appendInFlightGeneration = m_appendGeneration;

    ScrollStitchEngine *stitchEngine = &m_stitchEngine;
    QMutex *stitchMutex = &m_stitchMutex;

    m_appendWatcher->setFuture(QtConcurrent::run([stitchEngine, stitchMutex, frame, consumedDyHint]() {
        QMutexLocker locker(stitchMutex);
        if (consumedDyHint > 0) {
            stitchEngine->setExternalEstimatedDyHint(consumedDyHint);
        }
        return stitchEngine->append(frame);
    }));
}

void ScrollCaptureSession::onAppendTaskFinished()
{
    if (!m_appendInFlight || !m_appendWatcher) {
        return;
    }

    const int finishedGeneration = m_appendInFlightGeneration;
    const StitchFrameResult result = m_appendWatcher->result();
    m_appendInFlight = false;

    if (finishedGeneration != m_appendGeneration || !m_running) {
        dispatchNextAppendIfIdle();
        return;
    }

    m_lastStrategy = result.strategy;
    updateQualityLabel(result.quality, result.confidence);
    if (!handleAppendResult(result)) {
        return;
    }

    {
        QMutexLocker locker(&m_stitchMutex);
        m_cachedFrameCount = m_stitchEngine.frameCount();
        m_cachedTotalHeight = m_stitchEngine.totalHeight();
        m_cachedPreviewMeta = m_stitchEngine.previewMeta();
    }

    m_previewDirty = true;
    m_stitchedPreviewDirty = true;
    const qint64 previewStartMs = m_runtimeClock.isValid() ? m_runtimeClock.elapsed() : 0;
    updatePreview(false);
    const qint64 previewEndMs = m_runtimeClock.isValid() ? m_runtimeClock.elapsed() : previewStartMs;
    traceAppendResult(result, qMax<qint64>(0, previewEndMs - previewStartMs));
    updateProgressLabel();

    dispatchNextAppendIfIdle();
}

void ScrollCaptureSession::onWatchdogTick()
{
    if (!m_running || m_paused) {
        return;
    }

    const qint64 elapsedMs = m_runtimeClock.isValid() ? m_runtimeClock.elapsed() : 0;
    if (!m_stitchStarted && (elapsedMs - m_startedElapsedMs) >= kFirstFrameTimeoutMs) {
        qWarning() << "ScrollCaptureSession: watchdog first-frame timeout after" << elapsedMs << "ms";
        m_running = false;
        traceEvent(QStringLiteral("watchdog_first_frame_timeout"));
        exportTraceSnapshot(QStringLiteral("watchdog_first_frame_timeout"));
        shutdownRuntime();
        emit failed(tr("Timed out waiting for first capture frame."));
        return;
    }

    if (!m_captureTimer || !m_captureTimer->isActive()) {
        qWarning() << "ScrollCaptureSession: capture timer inactive";
        m_running = false;
        traceEvent(QStringLiteral("watchdog_capture_timer_inactive"));
        exportTraceSnapshot(QStringLiteral("watchdog_capture_timer_inactive"));
        shutdownRuntime();
        emit failed(tr("Capture loop stalled and could not be recovered."));
        return;
    }

    if (m_lastTickElapsedMs > 0 && elapsedMs - m_lastTickElapsedMs > kTickStallTimeoutMs) {
        m_manualReason = tr("Watchdog recovered stalled loop");
        updateStatusLabel();
    }

    if (m_lastFrameElapsedMs >= 0 && elapsedMs - m_lastFrameElapsedMs > kFrameStallTimeoutMs) {
        m_manualReason = tr("Watchdog: no captured frames");
        updateStatusLabel();
    }
}

void ScrollCaptureSession::onCaptureTick()
{
    if (!m_running || m_paused || !m_captureEngine) {
        return;
    }

    updateSlowScrollWarningUi();

    if (m_runtimeClock.isValid()) {
        m_lastTickElapsedMs = m_runtimeClock.elapsed();
    }

    const QImage frame = m_captureEngine->captureFrame();
    if (frame.isNull()) {
        if (!m_stitchStarted) {
            const qint64 elapsedMs = m_runtimeClock.isValid() ? m_runtimeClock.elapsed() : 0;
            ++m_initialFrameMisses;
            if ((elapsedMs - m_startedElapsedMs) >= kFirstFrameTimeoutMs) {
                qWarning() << "ScrollCaptureSession: first-frame timeout after" << elapsedMs << "ms";
                m_running = false;
                traceEvent(QStringLiteral("first_frame_timeout"));
                exportTraceSnapshot(QStringLiteral("first_frame_timeout"));
                shutdownRuntime();
                emit failed(tr("Timed out waiting for first capture frame."));
            }
        }
        return;
    }

    if (!m_stitchStarted) {
        m_initialFrameMisses = 0;
        m_lastFrameElapsedMs = m_runtimeClock.isValid() ? m_runtimeClock.elapsed() : 0;
        {
            QMutexLocker locker(&m_stitchMutex);
            m_stitchStarted = m_stitchEngine.start(frame);
            if (m_stitchStarted) {
                m_cachedFrameCount = m_stitchEngine.frameCount();
                m_cachedTotalHeight = m_stitchEngine.totalHeight();
                m_cachedPreviewMeta = m_stitchEngine.previewMeta();
            }
        }
        if (!m_stitchStarted) {
            m_running = false;
            traceEvent(QStringLiteral("tick_stitch_init_failed"));
            exportTraceSnapshot(QStringLiteral("tick_stitch_init_failed"));
            shutdownRuntime();
            emit failed(tr("Failed to initialize stitch engine."));
            return;
        }

        updateStatusLabel();
        updateProgressLabel();
        m_stitchedPreviewDirty = true;
        updatePreview(true);
        return;
    }

    m_lastFrameElapsedMs = m_runtimeClock.isValid() ? m_runtimeClock.elapsed() : 0;
    queueAppendFrame(frame);
    updateProgressLabel();
    updatePreview(false);
    dispatchNextAppendIfIdle();
}

QList<WId> ScrollCaptureSession::collectExcludedWindowIds() const
{
    QList<WId> excludedIds;
    auto appendWId = [&excludedIds](WId wid) {
        if (wid != 0 && !excludedIds.contains(wid)) {
            excludedIds.append(wid);
        }
    };

    if (m_boundaryOverlay) {
        appendWId(m_boundaryOverlay->winId());
    }
    if (m_controlBar) {
        appendWId(m_controlBar->winId());
    }
    if (m_previewWindow) {
        appendWId(m_previewWindow->winId());
    }
    return excludedIds;
}
