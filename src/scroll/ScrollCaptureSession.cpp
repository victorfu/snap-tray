#include "scroll/ScrollCaptureSession.h"

#include "PlatformFeatures.h"
#include "RecordingBoundaryOverlay.h"
#include "capture/ICaptureEngine.h"
#include "platform/WindowLevel.h"
#include "scroll/ScrollCaptureControlBar.h"
#include "settings/RegionCaptureSettingsManager.h"

#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <QCursor>
#include <QWidget>
#include <QDebug>
#include <QVector>

namespace {

constexpr int kAutoNoChangeFinishThreshold = 3;
constexpr int kAutoRecoveryFocusThreshold = 2;
constexpr int kAutoRecoverySyntheticThreshold = 3;
constexpr int kAutoStartupNoMotionManualThreshold = 4;
constexpr double kMotionDiffThreshold = 0.015;
constexpr double kSettleDiffThreshold = 0.004;
constexpr int kCaptureTickIntervalMs = 66;
constexpr int kWatchdogIntervalMs = 600;
constexpr qint64 kFirstFrameTimeoutMs = 6000;
constexpr qint64 kTickStallTimeoutMs = 2500;
constexpr qint64 kFrameStallTimeoutMs = 3000;
constexpr int kPreviewWidth = 320;
constexpr int kPreviewHeight = 180;
constexpr qint64 kPreviewUpdateIntervalMs = 350;

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

QString strategyText(StitchMatchStrategy strategy)
{
    switch (strategy) {
    case StitchMatchStrategy::RowDiff:
        return QStringLiteral("RowDiff");
    case StitchMatchStrategy::ColumnSample:
        return QStringLiteral("ColumnSample");
    case StitchMatchStrategy::BestGuess:
        return QStringLiteral("BestGuess");
    }
    return QStringLiteral("Unknown");
}

double normalizedFrameDifference(const QImage &a, const QImage &b,
                                 int topIgnore, int bottomIgnore,
                                 int leftIgnore, int rightIgnore)
{
    if (a.isNull() || b.isNull() || a.size() != b.size()) {
        return 1.0;
    }

    const int width = a.width();
    const int height = a.height();
    if (width <= 0 || height <= 0) {
        return 1.0;
    }

    const int yStart = qBound(0, topIgnore, height - 1);
    const int yEnd = qBound(yStart + 1, height - qMax(0, bottomIgnore), height);
    const int xStart = qBound(0, leftIgnore, width - 1);
    const int xEnd = qBound(xStart + 1, width - qMax(0, rightIgnore), width);
    if (xEnd <= xStart || yEnd <= yStart) {
        return 1.0;
    }

    const int sampleWidth = xEnd - xStart;
    const int sampleHeight = yEnd - yStart;
    const int rowStep = sampleHeight > 220 ? 2 : 1;
    const int colStep = sampleWidth > 320 ? 2 : 1;

    qint64 accum = 0;
    int samples = 0;
    for (int y = yStart; y < yEnd; y += rowStep) {
        const QRgb *lineA = reinterpret_cast<const QRgb *>(a.constScanLine(y));
        const QRgb *lineB = reinterpret_cast<const QRgb *>(b.constScanLine(y));
        if (!lineA || !lineB) {
            continue;
        }

        for (int x = xStart; x < xEnd; x += colStep) {
            const QRgb pixelA = lineA[x];
            const QRgb pixelB = lineB[x];
            const int grayA = (qRed(pixelA) * 77 + qGreen(pixelA) * 150 + qBlue(pixelA) * 29) >> 8;
            const int grayB = (qRed(pixelB) * 77 + qGreen(pixelB) * 150 + qBlue(pixelB) * 29) >> 8;
            accum += qAbs(grayA - grayB);
            ++samples;
        }
    }

    if (samples <= 0) {
        return 1.0;
    }
    return static_cast<double>(accum) / (static_cast<double>(samples) * 255.0);
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
    , m_stitchEngine(ScrollStitchOptions{
          RegionCaptureSettingsManager::instance().loadScrollGoodThreshold(),
          RegionCaptureSettingsManager::instance().loadScrollPartialThreshold(),
          RegionCaptureSettingsManager::instance().loadScrollMinScrollAmount(),
          RegionCaptureSettingsManager::instance().loadScrollMaxFrames(),
          RegionCaptureSettingsManager::instance().loadScrollMaxOutputPixels(),
          RegionCaptureSettingsManager::instance().loadScrollAutoIgnoreBottomEdge()})
{
}

ScrollCaptureSession::~ScrollCaptureSession()
{
    m_running = false;
    shutdownRuntime();
}

void ScrollCaptureSession::switchToHybridMode(const QString &reason)
{
    m_mode = Mode::Hybrid;
    m_noChangeCount = 0;
    m_autoNoMotionStepCount = 0;
    m_consecutiveUnlockedAutoSteps = 0;
    m_autoDriverStatus = QStringLiteral("Hybrid");
    m_recoveryFocusTried = false;
    m_recoverySyntheticTried = false;
    m_focusRecommended = (m_scrollDriver != nullptr);
    m_manualReason = reason;
    m_preStepFrame = QImage();
    m_lastObservedFrame = QImage();
    m_autoPhase = AutoScrollPhase::Idle;
    m_settleStableFrameCount = 0;
    m_autoPhaseEnteredAtMs = 0;
    m_lastMovementObservedAtMs = -1;
    m_interruptAutoPending = false;
    updateStatusLabel();
    updateProgressLabel();
    if (m_previewDirty) {
        updatePreview(false);
    }
}

void ScrollCaptureSession::switchToManualMode(const QString &reason)
{
    m_mode = Mode::Manual;
    m_noChangeCount = 0;
    m_autoNoMotionStepCount = 0;
    m_consecutiveUnlockedAutoSteps = 0;
    m_autoDriverStatus = QStringLiteral("Manual");
    m_recoveryFocusTried = false;
    m_recoverySyntheticTried = false;
    m_focusRecommended = (m_scrollDriver != nullptr);
    m_manualReason = reason;
    m_preStepFrame = QImage();
    m_lastObservedFrame = QImage();
    m_autoPhase = AutoScrollPhase::Idle;
    m_settleStableFrameCount = 0;
    m_autoPhaseEnteredAtMs = 0;
    m_lastMovementObservedAtMs = -1;
    updateStatusLabel();
    updateProgressLabel();
}

QList<QPoint> ScrollCaptureSession::buildProbePoints() const
{
    QList<QPoint> points;
    if (m_region.isEmpty()) {
        return points;
    }

    auto appendUnique = [&points](const QPoint &point) {
        if (!points.contains(point)) {
            points.append(point);
        }
    };

    const int insetX = qMax(6, m_region.width() / 18);
    const int insetY = qMax(6, m_region.height() / 18);
    QRect probeRegion = m_region.adjusted(insetX, insetY, -insetX, -insetY);
    if (!probeRegion.isValid() || probeRegion.width() < 3 || probeRegion.height() < 3) {
        probeRegion = m_region.adjusted(1, 1, -1, -1);
    }
    if (!probeRegion.isValid() || probeRegion.isEmpty()) {
        probeRegion = m_region;
    }

    const QPoint center = probeRegion.center();
    appendUnique(center);
    appendUnique(QPoint(center.x(), probeRegion.top()));
    appendUnique(QPoint(center.x(), probeRegion.bottom()));
    appendUnique(QPoint(probeRegion.left(), center.y()));
    appendUnique(QPoint(probeRegion.right(), center.y()));

    const int density = qBound(2, m_probeGridDensity, 5);
    QVector<int> xSamples;
    QVector<int> ySamples;
    xSamples.reserve(density);
    ySamples.reserve(density);
    for (int i = 0; i < density; ++i) {
        const double ratio = (density > 1)
            ? (static_cast<double>(i) / static_cast<double>(density - 1))
            : 0.5;
        const int x = probeRegion.left() +
            qRound(ratio * static_cast<double>(qMax(0, probeRegion.width() - 1)));
        const int y = probeRegion.top() +
            qRound(ratio * static_cast<double>(qMax(0, probeRegion.height() - 1)));
        if (!xSamples.contains(x)) {
            xSamples.push_back(x);
        }
        if (!ySamples.contains(y)) {
            ySamples.push_back(y);
        }
    }

    for (const int y : ySamples) {
        for (const int x : xSamples) {
            appendUnique(QPoint(x, y));
        }
    }

    const QPoint cursorPoint = QCursor::pos();
    if (m_region.contains(cursorPoint)) {
        const QPoint clamped(
            qBound(probeRegion.left(), cursorPoint.x(), probeRegion.right()),
            qBound(probeRegion.top(), cursorPoint.y(), probeRegion.bottom()));
        appendUnique(clamped);
    }

    return points;
}

bool ScrollCaptureSession::tryProbeAutoMode(int maxProbeCount)
{
    if (!m_running || !m_scrollDriver || m_screen.isNull()) {
        return false;
    }

    QString lastReason;
    const QList<QPoint> probePoints = (!m_pendingProbePoints.isEmpty())
        ? m_pendingProbePoints
        : buildProbePoints();
    const int startIndex = (!m_pendingProbePoints.isEmpty()) ? m_probeIndex : 0;
    int processed = 0;
    for (int i = startIndex; i < probePoints.size(); ++i) {
        if (maxProbeCount > 0 && processed >= maxProbeCount) {
            break;
        }
        const QPoint &point = probePoints.at(i);
        const ScrollProbeResult probe = m_scrollDriver->probeAt(point, m_screen.data());
        ++processed;
        lastReason = probe.reason;
        if (probe.mode == ScrollAutomationMode::Auto ||
            probe.mode == ScrollAutomationMode::AutoSynthetic) {
            m_mode = Mode::Auto;
            m_interruptAutoPending = false;
            m_syntheticAutoFallback = (probe.mode == ScrollAutomationMode::AutoSynthetic);
            m_autoDriverStatus = m_syntheticAutoFallback
                ? QStringLiteral("AutoSynthetic(Global)")
                : QStringLiteral("AX");
            m_focusRecommended = probe.focusRecommended || m_syntheticAutoFallback;
            m_probeAnchor = !probe.anchorPoint.isNull() ? probe.anchorPoint : point;
            m_hasProbeAnchor = true;
            m_recoveryFocusTried = false;
            m_recoverySyntheticTried = false;
            m_manualReason.clear();
            m_lastAutoStepElapsedMs = -1;
            m_preStepFrame = QImage();
            m_lastObservedFrame = QImage();
            m_autoPhase = AutoScrollPhase::ReadyToScroll;
            m_settleStableFrameCount = 0;
            m_autoPhaseEnteredAtMs = m_runtimeClock.isValid() ? m_runtimeClock.elapsed() : 0;
            m_lastMovementObservedAtMs = -1;
            qDebug() << "ScrollCaptureSession: auto scroll enabled at" << point
                     << "reason:" << probe.reason;
            return true;
        }
    }

    if (!lastReason.isEmpty()) {
        qWarning() << "ScrollCaptureSession: fallback to manual mode:" << lastReason;
        if (lastReason.contains(QStringLiteral("timeout"), Qt::CaseInsensitive)) {
            m_manualReason = tr("Auto assist timeout");
        } else if (lastReason.contains(QStringLiteral("permission"), Qt::CaseInsensitive) ||
                   lastReason.contains(QStringLiteral("denied"), Qt::CaseInsensitive)) {
            m_manualReason = tr("Auto assist unavailable");
        } else if (lastReason.contains(QStringLiteral("focus"), Qt::CaseInsensitive) ||
                   lastReason.contains(QStringLiteral("target"), Qt::CaseInsensitive)) {
            m_manualReason = tr("Focus target then retry auto");
        } else {
            m_manualReason = tr("Auto assist unavailable");
        }
    } else {
        m_manualReason = tr("Auto assist unavailable");
    }
    return false;
}

void ScrollCaptureSession::scheduleAutoProbe()
{
    if (!m_running || m_probeInFlight || !m_scrollDriver) {
        return;
    }

    m_probeInFlight = true;
    m_pendingProbePoints = buildProbePoints();
    m_probeIndex = 0;
    m_probeExpandedPass = false;
    m_probeMaxPoints = qMin(5, m_pendingProbePoints.size());
    QTimer::singleShot(0, this, &ScrollCaptureSession::processAutoProbeStep);
}

void ScrollCaptureSession::processAutoProbeStep()
{
    if (!m_probeInFlight || !m_running || !m_scrollDriver) {
        m_probeInFlight = false;
        return;
    }

    if (tryProbeAutoMode(1)) {
        m_probeInFlight = false;
        m_pendingProbePoints.clear();
        m_probeFailureCount = 0;
        updateStatusLabel();
        updateProgressLabel();
        return;
    }

    ++m_probeIndex;
    if (m_probeIndex < m_probeMaxPoints) {
        QTimer::singleShot(0, this, &ScrollCaptureSession::processAutoProbeStep);
        return;
    }

    if (!m_probeExpandedPass && m_probeMaxPoints < m_pendingProbePoints.size()) {
        m_probeExpandedPass = true;
        m_probeIndex = m_probeMaxPoints;
        m_probeMaxPoints = m_pendingProbePoints.size();
        QTimer::singleShot(0, this, &ScrollCaptureSession::processAutoProbeStep);
        return;
    }

    m_probeInFlight = false;
    m_pendingProbePoints.clear();
    ++m_probeFailureCount;
    m_mode = Mode::Manual;
    m_autoDriverStatus = QStringLiteral("Manual");
    m_focusRecommended = (m_scrollDriver != nullptr);
    m_preStepFrame = QImage();
    m_lastObservedFrame = QImage();
    m_autoPhase = AutoScrollPhase::Idle;
    m_settleStableFrameCount = 0;
    m_autoPhaseEnteredAtMs = 0;
    m_lastMovementObservedAtMs = -1;
    updateStatusLabel();
    updateProgressLabel();
}

QList<WId> ScrollCaptureSession::collectExcludedWindowIds() const
{
    QList<WId> excludedIds;
    auto appendWindowId = [&excludedIds](QWidget *widget) {
        if (!widget) {
            return;
        }
        const WId wid = widget->winId();
        if (wid != 0 && !excludedIds.contains(wid)) {
            excludedIds.append(wid);
        }
    };

    appendWindowId(m_boundaryOverlay);
    appendWindowId(m_controlBar);
    return excludedIds;
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

    m_stitchStarted = false;
    m_running = false;
    m_paused = false;
    m_mode = Mode::Manual;
    m_noChangeCount = 0;
    m_initialCombineAttempts = 0;
    m_initialBadCount = 0;
    m_initialFrameMisses = 0;
    m_syntheticAutoFallback = false;
    m_hasConfirmedMotion = false;
    m_probeInFlight = false;
    m_pendingProbePoints.clear();
    m_probeIndex = 0;
    m_probeMaxPoints = 0;
    m_probeExpandedPass = false;
    m_probeFailureCount = 0;
    m_autoNoMotionStepCount = 0;
    m_interruptAutoPending = false;
    m_captureTimerRestarted = false;
    m_consecutiveUnlockedAutoSteps = 0;
    m_autoDriverStatus = QStringLiteral("Manual");
    m_autoStepDelayMs = qMax(40, RegionCaptureSettingsManager::instance().loadScrollAutoStepDelayMs());
    m_settleStableFramesRequired = qMax(1, RegionCaptureSettingsManager::instance().loadScrollSettleStableFrames());
    m_settleTimeoutMs = qMax<qint64>(60, RegionCaptureSettingsManager::instance().loadScrollSettleTimeoutMs());
    m_probeGridDensity = qBound(2, RegionCaptureSettingsManager::instance().loadScrollProbeGridDensity(), 5);
    m_probeAnchor = QPoint();
    m_hasProbeAnchor = false;
    m_recoveryFocusTried = false;
    m_recoverySyntheticTried = false;
    m_focusRecommended = false;
    m_manualReason.clear();
    m_startedElapsedMs = 0;
    m_lastTickElapsedMs = 0;
    m_lastFrameElapsedMs = -1;
    m_lastAutoStepElapsedMs = -1;
    m_preStepFrame = QImage();
    m_lastObservedFrame = QImage();
    m_autoPhase = AutoScrollPhase::Idle;
    m_settleStableFrameCount = 0;
    m_autoPhaseEnteredAtMs = 0;
    m_lastMovementObservedAtMs = -1;
    m_previewDirty = true;
    m_lastPreviewUpdateElapsedMs = -1;
    m_lastStatusLine.clear();
    m_progressLine.clear();
    m_lastQuality = StitchQuality::NoChange;
    m_lastQualityConfidence = 0.0;
    m_lastStrategy = StitchMatchStrategy::RowDiff;
    m_runtimeClock.invalidate();

    if (!m_captureEngine->setRegion(m_region, m_screen.data())) {
        emit failed(tr("Failed to configure capture region."));
        return;
    }

    const bool autoEnabled = RegionCaptureSettingsManager::instance().isScrollAutomationEnabled();
    if (autoEnabled && PlatformFeatures::instance().isScrollAutomationAvailable()) {
        m_scrollDriver = m_deps.automationDriver;
        if (!m_scrollDriver) {
            m_scrollDriver = PlatformFeatures::instance().createScrollAutomationDriver(this);
            m_ownsScrollDriver = true;
        }

        m_focusRecommended = (m_scrollDriver != nullptr);
    }

    if (m_deps.enableUi) {
        setupUi();
    }

    m_captureEngine->setExcludedWindows(collectExcludedWindowIds());

    if (!m_captureEngine->start()) {
        shutdownRuntime();
        emit failed(tr("Failed to start capture engine."));
        return;
    }
    m_running = true;
    m_runtimeClock.start();
    m_startedElapsedMs = m_runtimeClock.elapsed();
    m_lastTickElapsedMs = 0;
    m_lastFrameElapsedMs = -1;

    // ScreenCaptureKit's first frame is asynchronous; do not fail start() if first read is null.
    const QImage firstFrame = m_captureEngine->captureFrame();
    if (!firstFrame.isNull()) {
        m_lastFrameElapsedMs = m_runtimeClock.elapsed();
        m_stitchStarted = m_stitchEngine.start(firstFrame);
        if (!m_stitchStarted) {
            m_running = false;
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

    if (m_scrollDriver &&
        RegionCaptureSettingsManager::instance().loadScrollAutoStartMode() ==
            RegionCaptureSettingsManager::ScrollAutoStartMode::AutoProbe) {
        startAutoAssist();
    }
}

void ScrollCaptureSession::cancel()
{
    if (!m_running) {
        return;
    }

    m_running = false;
    shutdownRuntime();
    emit cancelled();
}

void ScrollCaptureSession::startAutoAssist()
{
    if (!m_running || !m_scrollDriver) {
        return;
    }
    if (m_mode == Mode::Auto || m_probeInFlight) {
        return;
    }

    m_probeFailureCount = 0;
    m_manualReason = tr("Auto assist probing...");
    m_focusRecommended = true;
    m_interruptAutoPending = false;
    scheduleAutoProbe();
    updateStatusLabel();
    updateProgressLabel();
}

void ScrollCaptureSession::finish()
{
    if (!m_running) {
        return;
    }

    m_running = false;

    QImage finalImage = m_stitchEngine.composeFinal();
    if (finalImage.isNull()) {
        shutdownRuntime();
        emit failed(tr("Failed to compose final scrolling capture."));
        return;
    }

    QPixmap output = QPixmap::fromImage(finalImage, Qt::NoOpaqueDetection);
    if (output.isNull()) {
        shutdownRuntime();
        emit failed(tr("Failed to convert final scrolling capture image."));
        return;
    }
    if (m_screen) {
        output.setDevicePixelRatio(m_screen->devicePixelRatio());
    }

    shutdownRuntime();
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

void ScrollCaptureSession::interruptAuto()
{
    if (!m_running) {
        return;
    }

    if (m_mode == Mode::Auto) {
        m_interruptAutoPending = true;
        switchToHybridMode(tr("Auto scroll stopped by user"));
        return;
    }

    if (m_mode == Mode::Hybrid) {
        switchToManualMode(tr("Hybrid switched to manual by user"));
    }
}

void ScrollCaptureSession::setupUi()
{
    RecordingBoundaryOverlay *boundary = new RecordingBoundaryOverlay();
    boundary->setRegion(m_region);
    boundary->setBorderMode(RecordingBoundaryOverlay::BorderMode::Recording);
    setWindowExcludedFromCapture(boundary, true);
    boundary->show();
    m_boundaryOverlay = boundary;

    m_controlBar = new ScrollCaptureControlBar();
    m_controlBar->setDetailsExpanded(
        !RegionCaptureSettingsManager::instance().loadScrollInlinePreviewCollapsed());
    connect(m_controlBar, &ScrollCaptureControlBar::autoAssistRequested, this, [this]() {
        if (m_mode == Mode::Auto) {
            interruptAuto();
        } else {
            startAutoAssist();
        }
    });
    connect(m_controlBar, &ScrollCaptureControlBar::focusRequested, this, [this]() {
        if (!m_scrollDriver) {
            return;
        }
        if (m_scrollDriver->focusTarget()) {
            m_manualReason = tr("Target focused");
        } else {
            m_manualReason = tr("Unable to focus target automatically");
        }
        updateStatusLabel();
    });
    connect(m_controlBar, &ScrollCaptureControlBar::finishRequested,
            this, &ScrollCaptureSession::finish);
    connect(m_controlBar, &ScrollCaptureControlBar::cancelRequested,
            this, &ScrollCaptureSession::cancel);
    connect(m_controlBar, &ScrollCaptureControlBar::detailsExpandedChanged, this, [](bool expanded) {
        RegionCaptureSettingsManager::instance().saveScrollInlinePreviewCollapsed(!expanded);
    });

    m_controlBar->positionNear(m_region, m_screen.data());
    setWindowExcludedFromCapture(m_controlBar, true);
    m_controlBar->show();
    setWindowFloatingWithoutFocus(m_controlBar);
    setWindowVisibleOnAllWorkspaces(m_controlBar, true);

    setWindowVisibleOnAllWorkspaces(m_boundaryOverlay, true);
}

void ScrollCaptureSession::shutdownRuntime()
{
    m_probeInFlight = false;
    m_pendingProbePoints.clear();
    m_probeIndex = 0;
    m_probeMaxPoints = 0;
    m_probeExpandedPass = false;

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

    if (m_scrollDriver) {
        m_scrollDriver->reset();
        if (m_ownsScrollDriver) {
            m_scrollDriver->deleteLater();
        }
        m_scrollDriver = nullptr;
        m_ownsScrollDriver = false;
    }
}

void ScrollCaptureSession::cleanupUi()
{
    if (m_boundaryOverlay) {
        setWindowExcludedFromCapture(m_boundaryOverlay, false);
        setWindowVisibleOnAllWorkspaces(m_boundaryOverlay, false);
        m_boundaryOverlay->close();
        m_boundaryOverlay->deleteLater();
        m_boundaryOverlay = nullptr;
    }

    if (m_controlBar) {
        setWindowExcludedFromCapture(m_controlBar, false);
        setWindowVisibleOnAllWorkspaces(m_controlBar, false);
        m_controlBar->close();
        m_controlBar->deleteLater();
        m_controlBar = nullptr;
    }
}

void ScrollCaptureSession::updatePreview(bool force)
{
    if (!m_controlBar) {
        return;
    }

    const qint64 elapsedMs = m_runtimeClock.isValid() ? m_runtimeClock.elapsed() : 0;
    if (!force && m_lastPreviewUpdateElapsedMs >= 0 &&
        (elapsedMs - m_lastPreviewUpdateElapsedMs) < kPreviewUpdateIntervalMs) {
        m_previewDirty = true;
        return;
    }

    QImage preview = m_stitchEngine.preview(kPreviewWidth, kPreviewHeight);
    if (preview.isNull()) {
        return;
    }
    m_controlBar->setPreviewImage(preview);
    m_lastPreviewUpdateElapsedMs = elapsedMs;
    m_previewDirty = false;
    updatePreviewMetaLabel();
}

void ScrollCaptureSession::updatePreviewMetaLabel()
{
    if (!m_controlBar) {
        return;
    }

    const StitchPreviewMeta meta = m_stitchEngine.previewMeta();
    const QString lastAppendText = meta.hasLastAppend ? QString::number(meta.lastAppend) : QStringLiteral("--");
    const QString strategy = meta.hasLastAppend
        ? strategyText(meta.lastStrategy)
        : QStringLiteral("--");

    const QString quality = tr("%1 (%2%)")
        .arg(qualityText(m_lastQuality))
        .arg(static_cast<int>(qRound(m_lastQualityConfidence * 100.0)));

    m_controlBar->setMetaText(
        tr("%1\nFrames %2 | Height %3 px | Last append %4 px | Quality %5 | Strategy %6")
            .arg(m_progressLine)
            .arg(meta.frames)
            .arg(meta.height)
            .arg(lastAppendText)
            .arg(quality)
            .arg(strategy));
}

bool ScrollCaptureSession::isAutoDegraded() const
{
    if (m_mode != Mode::Auto) {
        return false;
    }

    return m_syntheticAutoFallback || m_consecutiveUnlockedAutoSteps >= 2 || m_focusRecommended;
}

void ScrollCaptureSession::updateStatusLabel()
{
    if (!m_controlBar) {
        return;
    }

    QString status = tr("Manual");
    if (!m_stitchStarted) {
        status = tr("Manual: waiting first frame");
    } else if (m_probeInFlight) {
        status = tr("Auto Running");
    } else if (m_paused) {
        status = tr("Paused");
    } else if (m_mode == Mode::Auto) {
        if (isAutoDegraded()) {
            status = tr("Auto Degraded");
        } else if (m_hasConfirmedMotion) {
            status = tr("Auto Running");
        } else {
            status = tr("Auto Running");
        }
    } else if (m_mode == Mode::Hybrid) {
        status = tr("Hybrid");
    }

    if (!m_manualReason.isEmpty()) {
        status += tr(" | %1").arg(m_manualReason);
    }
    m_lastStatusLine = status;
    m_controlBar->setStatusText(status);

    if (m_probeInFlight) {
        m_controlBar->setAutoAssistText(tr("Probing..."));
    } else if (m_mode == Mode::Auto) {
        m_controlBar->setAutoAssistText(tr("Stop Auto"));
    } else if (m_mode == Mode::Hybrid || m_probeFailureCount > 0) {
        m_controlBar->setAutoAssistText(tr("Retry Auto"));
    } else {
        m_controlBar->setAutoAssistText(tr("Start Auto Assist"));
    }
    m_controlBar->setAutoAssistEnabled(
        m_running && m_scrollDriver != nullptr && !m_probeInFlight);

    const bool showRefocus =
        (((m_mode == Mode::Auto && isAutoDegraded()) || m_mode == Mode::Hybrid) &&
         m_scrollDriver != nullptr);
    const bool showFocus =
        (m_mode == Mode::Manual && m_scrollDriver != nullptr);
    if (showRefocus) {
        m_controlBar->setFocusText(tr("Refocus Target"));
    } else {
        m_controlBar->setFocusText(tr("Focus Target"));
    }
    m_controlBar->setFocusVisible(showRefocus || showFocus);
    m_controlBar->setFocusEnabled((showRefocus || showFocus) && m_running);

    if (!m_controlBar->hasManualPosition()) {
        m_controlBar->positionNear(m_region, m_screen.data());
    }

    updatePreviewMetaLabel();
}

void ScrollCaptureSession::updateProgressLabel()
{
    const int frames = m_stitchEngine.frameCount();
    const int height = m_stitchEngine.totalHeight();
    QString progress = tr("Manual | %1f | %2px").arg(frames).arg(height);
    if (m_mode == Mode::Auto) {
        const int recoveryCount = m_hasConfirmedMotion ? 0 : m_autoNoMotionStepCount;
        progress = tr("Auto | %1f | %2px | recovery %3/%4 | driver %5")
                       .arg(frames)
                       .arg(height)
                       .arg(recoveryCount)
                       .arg(kAutoStartupNoMotionManualThreshold)
                       .arg(m_autoDriverStatus);
    } else if (m_mode == Mode::Hybrid) {
        progress = tr("Hybrid | %1f | %2px")
                       .arg(frames)
                       .arg(height);
    }
    m_progressLine = progress;
    updatePreviewMetaLabel();
}

void ScrollCaptureSession::updateQualityLabel(StitchQuality quality, double confidence)
{
    m_lastQuality = quality;
    m_lastQualityConfidence = confidence;
    updatePreviewMetaLabel();
}

void ScrollCaptureSession::onWatchdogTick()
{
    if (!m_running) {
        return;
    }
    if (m_paused) {
        return;
    }

    const qint64 elapsedMs = m_runtimeClock.isValid() ? m_runtimeClock.elapsed() : 0;
    if (!m_stitchStarted && (elapsedMs - m_startedElapsedMs) >= kFirstFrameTimeoutMs) {
        qWarning() << "ScrollCaptureSession: watchdog first-frame timeout after" << elapsedMs << "ms";
        m_running = false;
        shutdownRuntime();
        emit failed(tr("Timed out waiting for first capture frame."));
        return;
    }

    if (!m_captureTimer || !m_captureTimer->isActive()) {
        if (!m_captureTimerRestarted && m_captureTimer) {
            qWarning() << "ScrollCaptureSession: watchdog recovered stalled loop by restarting capture timer";
            m_captureTimer->start(kCaptureTickIntervalMs);
            m_captureTimerRestarted = true;
            if (m_mode == Mode::Auto) {
                switchToHybridMode(tr("Watchdog recovered stalled loop"));
            } else {
                m_manualReason = tr("Watchdog recovered stalled loop");
                updateStatusLabel();
            }
            return;
        }

        qWarning() << "ScrollCaptureSession: capture timer inactive and recovery already attempted";
        m_running = false;
        shutdownRuntime();
        emit failed(tr("Capture loop stalled and could not be recovered."));
        return;
    }

    if (m_lastTickElapsedMs > 0 && elapsedMs - m_lastTickElapsedMs > kTickStallTimeoutMs) {
        qWarning() << "ScrollCaptureSession: watchdog detected tick stall:" << (elapsedMs - m_lastTickElapsedMs) << "ms";
        if (m_mode == Mode::Auto) {
            switchToHybridMode(tr("Watchdog recovered stalled loop"));
        } else {
            m_manualReason = tr("Watchdog recovered stalled loop");
            updateStatusLabel();
        }
    }

    if (m_lastFrameElapsedMs >= 0 && elapsedMs - m_lastFrameElapsedMs > kFrameStallTimeoutMs) {
        if (m_mode == Mode::Auto) {
            qWarning() << "ScrollCaptureSession: watchdog observed prolonged no-frame interval:"
                       << (elapsedMs - m_lastFrameElapsedMs) << "ms";
            switchToHybridMode(tr("Watchdog: no captured frames, switched to hybrid"));
        }
    }
}

void ScrollCaptureSession::onCaptureTick()
{
    if (!m_running || m_paused || !m_captureEngine) {
        return;
    }

    if (m_runtimeClock.isValid()) {
        m_lastTickElapsedMs = m_runtimeClock.elapsed();
    }

    if (m_mode != Mode::Auto && m_interruptAutoPending) {
        m_interruptAutoPending = false;
    }

    const QImage frame = m_captureEngine->captureFrame();
    if (frame.isNull()) {
        if (!m_stitchStarted) {
            const qint64 elapsedMs = m_runtimeClock.isValid() ? m_runtimeClock.elapsed() : 0;
            ++m_initialFrameMisses;
            if ((elapsedMs - m_startedElapsedMs) >= kFirstFrameTimeoutMs) {
                qWarning() << "ScrollCaptureSession: first-frame timeout after" << elapsedMs << "ms";
                m_running = false;
                shutdownRuntime();
                emit failed(tr("Timed out waiting for first capture frame."));
            }
        }
        return;
    }

    if (!m_stitchStarted) {
        m_initialFrameMisses = 0;
        m_lastFrameElapsedMs = m_runtimeClock.isValid() ? m_runtimeClock.elapsed() : 0;
        m_stitchStarted = m_stitchEngine.start(frame);
        if (!m_stitchStarted) {
            m_running = false;
            shutdownRuntime();
            emit failed(tr("Failed to initialize stitch engine."));
            return;
        }
        updateStatusLabel();
        updateProgressLabel();

        if (m_mode == Mode::Auto && m_scrollDriver) {
            m_lastAutoStepElapsedMs = -1;
            m_preStepFrame = frame;
            m_lastObservedFrame = frame;
            m_autoPhase = AutoScrollPhase::ReadyToScroll;
            m_settleStableFrameCount = 0;
            m_autoPhaseEnteredAtMs = m_lastFrameElapsedMs;
            m_lastMovementObservedAtMs = -1;
        }
        return;
    }

    m_lastFrameElapsedMs = m_runtimeClock.isValid() ? m_runtimeClock.elapsed() : 0;
    const qint64 elapsedMs = m_runtimeClock.isValid() ? m_runtimeClock.elapsed() : 0;
    const int minScrollAmount = RegionCaptureSettingsManager::instance().loadScrollMinScrollAmount();

    auto applyAppendResult = [this, minScrollAmount](const StitchFrameResult &appendResult) -> bool {
        if (appendResult.quality != StitchQuality::NoChange) {
            ++m_initialCombineAttempts;
            if (m_initialCombineAttempts <= 2 && appendResult.quality == StitchQuality::Bad) {
                ++m_initialBadCount;
                if (m_initialCombineAttempts == 2 && m_initialBadCount == 2) {
                    if (m_mode == Mode::Auto) {
                        switchToHybridMode(tr("Auto fallback: low stitch confidence"));
                        return true;
                    }
                    m_running = false;
                    shutdownRuntime();
                    emit failed(tr("Unable to stitch the first frames reliably."));
                    return false;
                }
            }
        }

        const bool confirmedMotionThisFrame =
            (appendResult.quality == StitchQuality::Good ||
             appendResult.quality == StitchQuality::PartiallyGood) &&
            appendResult.appendHeight >= minScrollAmount;
        const bool justConfirmedMotion = confirmedMotionThisFrame && !m_hasConfirmedMotion;
        if (confirmedMotionThisFrame) {
            m_hasConfirmedMotion = true;
            m_autoNoMotionStepCount = 0;
            m_recoveryFocusTried = false;
            m_recoverySyntheticTried = false;
            m_manualReason.clear();
            if (justConfirmedMotion) {
                updateStatusLabel();
            }
        }

        if (m_hasConfirmedMotion) {
            if (appendResult.quality == StitchQuality::NoChange) {
                ++m_noChangeCount;
            } else {
                m_noChangeCount = 0;
            }
        }

        return true;
    };

    bool appendedThisTick = false;
    bool noNewContentThisFrame = false;
    StitchFrameResult result;
    result.quality = StitchQuality::NoChange;
    result.confidence = 0.0;

    if (m_mode == Mode::Auto) {
        if (m_autoPhase == AutoScrollPhase::Idle) {
            m_autoPhase = AutoScrollPhase::ReadyToScroll;
            m_autoPhaseEnteredAtMs = elapsedMs;
            m_settleStableFrameCount = 0;
            m_lastMovementObservedAtMs = -1;
            m_preStepFrame = frame;
            m_lastObservedFrame = frame;
        }

        const int topIgnore = m_stitchEngine.stickyHeaderPx();
        const int bottomIgnore = m_stitchEngine.stickyFooterPx();
        const int sideIgnore = qMin(frame.width() / 12, qMax(0, frame.width() / 2 - 1));

        const bool waitingSettlePhase =
            (m_autoPhase == AutoScrollPhase::ScrollIssued ||
             m_autoPhase == AutoScrollPhase::MovementObserved ||
             m_autoPhase == AutoScrollPhase::WaitingForSettle);

        if (waitingSettlePhase) {
            const QImage &baseline = m_preStepFrame.isNull() ? m_lastObservedFrame : m_preStepFrame;
            const double movementDiff = normalizedFrameDifference(
                baseline, frame, topIgnore, bottomIgnore, sideIgnore, sideIgnore);
            const bool movementObserved = movementDiff >= kMotionDiffThreshold;

            if (movementObserved) {
                if (m_autoPhase == AutoScrollPhase::ScrollIssued) {
                    m_autoPhase = AutoScrollPhase::MovementObserved;
                } else {
                    m_autoPhase = AutoScrollPhase::WaitingForSettle;
                }
                m_lastMovementObservedAtMs = elapsedMs;
            }

            if (!m_lastObservedFrame.isNull()) {
                const double settleDiff = normalizedFrameDifference(
                    m_lastObservedFrame, frame, topIgnore, bottomIgnore, sideIgnore, sideIgnore);
                if (settleDiff <= kSettleDiffThreshold) {
                    ++m_settleStableFrameCount;
                } else {
                    m_settleStableFrameCount = 0;
                }
            } else {
                m_settleStableFrameCount = 0;
            }

            const bool timedOut = (elapsedMs - m_autoPhaseEnteredAtMs) >= m_settleTimeoutMs;
            const bool observedMovementSinceStep =
                (m_lastMovementObservedAtMs >= m_autoPhaseEnteredAtMs);

            if (m_settleStableFrameCount >= m_settleStableFramesRequired) {
                result = m_stitchEngine.append(frame);
                appendedThisTick = true;
            } else if (timedOut && observedMovementSinceStep) {
                result = m_stitchEngine.append(frame);
                appendedThisTick = true;
                m_manualReason = tr("Auto settle timeout: committed best available frame");
            }

            if (appendedThisTick || timedOut) {
                m_autoPhase = AutoScrollPhase::ReadyToScroll;
                m_autoPhaseEnteredAtMs = elapsedMs;
                m_settleStableFrameCount = 0;
                m_lastMovementObservedAtMs = -1;
                m_preStepFrame = frame;
                m_lastObservedFrame = frame;
            } else {
                m_lastObservedFrame = frame;
            }
        } else {
            m_lastObservedFrame = frame;
        }

        if (appendedThisTick) {
            updateQualityLabel(result.quality, result.confidence);
            if (!applyAppendResult(result)) {
                return;
            }
            noNewContentThisFrame = (result.quality == StitchQuality::NoChange);
            m_previewDirty = true;
            updatePreview(false);
        }

        const bool readyToStep = (m_autoPhase == AutoScrollPhase::ReadyToScroll);
        const bool shouldAutoStepNow =
            (readyToStep && !appendedThisTick && m_scrollDriver &&
             (m_lastAutoStepElapsedMs < 0 || (elapsedMs - m_lastAutoStepElapsedMs) >= m_autoStepDelayMs));

        if (shouldAutoStepNow) {
            if (m_interruptAutoPending) {
                m_interruptAutoPending = false;
                switchToHybridMode(tr("Auto interrupted by user"));
                return;
            }

            m_lastAutoStepElapsedMs = elapsedMs;
            const ScrollStepResult stepResult = m_scrollDriver->step();
            if (stepResult.inputInjected) {
                m_autoDriverStatus = stepResult.targetLocked
                    ? QStringLiteral("AutoSynthetic(Targeted)")
                    : QStringLiteral("AutoSynthetic(Global)");
                if (stepResult.targetLocked) {
                    m_consecutiveUnlockedAutoSteps = 0;
                } else {
                    ++m_consecutiveUnlockedAutoSteps;
                }
            } else {
                m_autoDriverStatus = QStringLiteral("AX");
                m_consecutiveUnlockedAutoSteps = 0;
            }

            if (stepResult.status == ScrollStepStatus::EndReached) {
                if (m_hasConfirmedMotion && !noNewContentThisFrame) {
                    noNewContentThisFrame = true;
                }
                if (!m_hasConfirmedMotion) {
                    ++m_autoNoMotionStepCount;
                }
            } else if (stepResult.status == ScrollStepStatus::Failed) {
                switchToHybridMode(tr("Auto scroll failed"));
                return;
            } else if (!m_hasConfirmedMotion) {
                ++m_autoNoMotionStepCount;
            }

            if (stepResult.status == ScrollStepStatus::Stepped ||
                stepResult.status == ScrollStepStatus::EndReached) {
                m_autoPhase = AutoScrollPhase::ScrollIssued;
                m_autoPhaseEnteredAtMs = elapsedMs;
                m_settleStableFrameCount = 0;
                m_lastMovementObservedAtMs = -1;
                m_preStepFrame = frame;
                m_lastObservedFrame = frame;
            }

            if (m_mode == Mode::Auto) {
                if (m_consecutiveUnlockedAutoSteps >= 2 && m_manualReason.isEmpty()) {
                    m_manualReason = tr("Warning: global injection; cursor movement may affect auto scroll");
                }
                if (m_consecutiveUnlockedAutoSteps >= 3) {
                    switchToHybridMode(tr("Auto fallback: target lock unstable"));
                    return;
                }
                updateStatusLabel();
            }
        }
    } else {
        result = m_stitchEngine.append(frame);
        appendedThisTick = true;
        noNewContentThisFrame = (result.quality == StitchQuality::NoChange);
        updateQualityLabel(result.quality, result.confidence);
        if (!applyAppendResult(result)) {
            return;
        }
        m_previewDirty = true;
        updatePreview(false);
    }

    if (m_mode == Mode::Auto && m_hasConfirmedMotion && noNewContentThisFrame && !appendedThisTick) {
        ++m_noChangeCount;
    }

    if (m_mode == Mode::Auto) {
        if (!m_hasConfirmedMotion) {
            if (!m_recoveryFocusTried &&
                m_autoNoMotionStepCount >= kAutoRecoveryFocusThreshold) {
                m_recoveryFocusTried = true;
                if (m_scrollDriver && m_scrollDriver->focusTarget()) {
                    m_manualReason = tr("Auto recovery: focused target");
                } else {
                    m_manualReason = tr("Auto recovery: unable to focus target");
                }
                updateStatusLabel();
            }

            if (!m_recoverySyntheticTried &&
                m_autoNoMotionStepCount >= kAutoRecoverySyntheticThreshold) {
                m_recoverySyntheticTried = true;
                bool switchedToSynthetic = false;
                if (m_scrollDriver && m_scrollDriver->forceSyntheticFallback()) {
                    switchedToSynthetic = true;
                } else if (m_scrollDriver && m_hasProbeAnchor && m_screen) {
                    const ScrollProbeResult reprobe = m_scrollDriver->probeAt(m_probeAnchor, m_screen.data());
                    if (reprobe.mode == ScrollAutomationMode::AutoSynthetic) {
                        switchedToSynthetic = true;
                    }
                }

                if (switchedToSynthetic) {
                    m_syntheticAutoFallback = true;
                    m_focusRecommended = true;
                    m_autoDriverStatus = QStringLiteral("AutoSynthetic(Global)");
                    m_consecutiveUnlockedAutoSteps = 0;
                    m_manualReason = tr("Auto recovery: switched to synthetic fallback");
                    updateStatusLabel();
                }
            }

            if (m_autoNoMotionStepCount >= kAutoStartupNoMotionManualThreshold) {
                switchToHybridMode(tr("Auto fallback to hybrid: no observable movement"));
                return;
            }
        }

        if (m_hasConfirmedMotion && m_noChangeCount >= kAutoNoChangeFinishThreshold) {
            finish();
            return;
        }
    }

    updateProgressLabel();
}
