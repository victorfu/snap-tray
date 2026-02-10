#include "scrollcapture/ScrollCaptureSession.h"

#include "PlatformFeatures.h"
#include "capture/ICaptureEngine.h"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QScreen>
#include <QThread>
#include <QTimer>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kInitialFrameTimeoutMs = 1500;
constexpr int kFollowupFrameTimeoutMs = 600;
constexpr int kFramePollIntervalMs = 16;
constexpr double kFallbackMotionThreshold = 1.0;

double meanDiff(const QImage& lhs, const QImage& rhs)
{
    if (lhs.isNull() || rhs.isNull() || lhs.size() != rhs.size()) {
        return 255.0;
    }

    cv::Mat lhsMat(lhs.height(), lhs.width(), CV_8UC4,
                   const_cast<uchar*>(lhs.constBits()), lhs.bytesPerLine());
    cv::Mat rhsMat(rhs.height(), rhs.width(), CV_8UC4,
                   const_cast<uchar*>(rhs.constBits()), rhs.bytesPerLine());

    cv::Mat lhsGray;
    cv::Mat rhsGray;
    cv::cvtColor(lhsMat, lhsGray, cv::COLOR_BGRA2GRAY);
    cv::cvtColor(rhsMat, rhsGray, cv::COLOR_BGRA2GRAY);

    cv::Mat diff;
    cv::absdiff(lhsGray, rhsGray, diff);
    return cv::mean(diff)[0];
}
} // namespace

ScrollCaptureSession::ScrollCaptureSession(QObject* parent)
    : QObject(parent)
{
}

ScrollCaptureSession::~ScrollCaptureSession()
{
    m_stopRequested.store(true);
    m_cancelRequested.store(true);
    if (m_captureEngine) {
        m_captureEngine->stop();
    }
}

void ScrollCaptureSession::configure(const DetectedElement& element, ScrollCapturePreset preset)
{
    m_element = element;
    m_preset = preset;
    m_params = paramsForPreset(preset);
    m_windowRect = element.bounds;
}

void ScrollCaptureSession::setExcludedWindows(const QList<WId>& windowIds)
{
    m_excludedWindowIds = windowIds;
}

void ScrollCaptureSession::start()
{
    if (m_running) {
        return;
    }

    m_stopRequested.store(false);
    m_cancelRequested.store(false);
    m_running = true;
    m_hasResult = false;
    m_frameCount = 0;
    m_smallDeltaStreak = 0;
    m_noChangeStreak = 0;
    m_noProgressStreak = 0;
    m_weakMatchStreak = 0;
    m_forceWarpFallbackNextStep = false;
    m_warpFallbackUsedCount = 0;
    m_stepMultiplier = 1.0;
    m_lastWarning.clear();
    m_stitcher.reset();

    emit stateChanged(ScrollCaptureState::Arming);
    QTimer::singleShot(0, this, &ScrollCaptureSession::runCaptureLoop);
}

void ScrollCaptureSession::stop()
{
    m_stopRequested.store(true);
}

void ScrollCaptureSession::cancel()
{
    m_cancelRequested.store(true);
}

bool ScrollCaptureSession::initializeCaptureEngine(QString* errorMessage)
{
    if (!m_windowRect.isValid()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Selected window bounds are invalid.");
        }
        return false;
    }

#ifdef Q_OS_MAC
    if (!PlatformFeatures::hasAccessibilityPermission()) {
        if (errorMessage) {
            *errorMessage = tr("Accessibility permission is required for auto scrolling.");
        }
        return false;
    }
#endif

    m_targetScreen = QGuiApplication::screenAt(m_windowRect.center());
    if (!m_targetScreen) {
        m_targetScreen = QGuiApplication::primaryScreen();
    }
    if (!m_targetScreen) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No available screen for selected window.");
        }
        return false;
    }

    m_captureEngine.reset(ICaptureEngine::createBestEngine(nullptr));
    if (!m_captureEngine) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to create capture engine.");
        }
        return false;
    }

    if (!m_excludedWindowIds.isEmpty()) {
        m_captureEngine->setExcludedWindows(m_excludedWindowIds);
    }

    if (!m_captureEngine->setRegion(m_windowRect, m_targetScreen.data())) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to configure capture region.");
        }
        return false;
    }

    if (!m_captureEngine->start()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to start capture engine.");
        }
        return false;
    }
    return true;
}

void ScrollCaptureSession::runCaptureLoop()
{
    if (m_cancelRequested.load()) {
        finishWithResult(false, QStringLiteral("cancelled"));
        return;
    }
    if (m_stopRequested.load()) {
        finishWithResult(false, QStringLiteral("stopped_by_user"));
        return;
    }

    QElapsedTimer sessionTimer;
    sessionTimer.start();

    QString initError;
    if (!initializeCaptureEngine(&initError)) {
        finishWithError(initError);
        return;
    }

    QImage frame0;
    if (!captureWindowFrame(&frame0, &initError, kInitialFrameTimeoutMs)) {
        if (m_stopRequested.load()) {
            finishWithResult(false, QStringLiteral("stopped_by_user"));
            return;
        }
        finishWithError(initError);
        return;
    }

    emit stateChanged(ScrollCaptureState::Capturing);
    emit progressChanged(1, frame0.height(), tr("Capturing..."));

    // First small scroll to detect the dynamic content region.
    AutoScroller::Request probeRequest;
    probeRequest.contentRectGlobal = m_windowRect;
    probeRequest.direction = m_params.direction;
    probeRequest.stepPx = m_params.minStepPx;
    probeRequest.nativeHandle = m_element.nativeHandle;
    probeRequest.preferNoWarp = true;
    probeRequest.allowCursorWarpFallback = false;

    QString scrollError;
    if (m_stopRequested.load()) {
        finishWithResult(false, QStringLiteral("stopped_by_user"));
        return;
    }
    if (!m_autoScroller.scroll(probeRequest, &scrollError)) {
        probeRequest.preferNoWarp = false;
        probeRequest.allowCursorWarpFallback = true;
        if (!m_autoScroller.scroll(probeRequest, &scrollError)) {
            finishWithError(scrollError);
            return;
        }
        m_lastWarning = tr("Using fallback scroll delivery.");
        emit warningRaised(m_lastWarning);
    }

    FrameStabilityDetector::Params settleParams;
    settleParams.maxSettleMs = m_params.maxSettleMs;
    FrameStabilityDetector::Result settleProbe = m_stabilityDetector.waitUntilStable(
        m_captureEngine.get(),
        m_windowRect,
        m_windowRect,
        settleParams,
        &m_cancelRequested,
        &m_stopRequested);
    if (m_cancelRequested.load()) {
        finishWithResult(false, QStringLiteral("cancelled"));
        return;
    }
    if (m_stopRequested.load()) {
        finishWithResult(false, QStringLiteral("stopped_by_user"));
        return;
    }

    QImage frame1 = settleProbe.frame;
    if (frame1.isNull() && !captureWindowFrame(&frame1, &initError, kFollowupFrameTimeoutMs)) {
        if (m_stopRequested.load()) {
            finishWithResult(false, QStringLiteral("stopped_by_user"));
            return;
        }
        finishWithError(initError);
        return;
    }

    ContentRegionDetector::DetectionResult detectedRegion =
        m_regionDetector.detect(frame0, frame1, m_windowRect);
    m_contentRect = detectedRegion.success ? detectedRegion.contentRect : m_windowRect;
    if (!m_contentRect.isValid()) {
        m_contentRect = m_windowRect;
    }
    if (!detectedRegion.warning.isEmpty()) {
        m_lastWarning = detectedRegion.warning;
        emit warningRaised(m_lastWarning);
    }

    ScrollStitcher::MatchOptions options;
    options.direction = m_params.direction;
    options.matchThreshold = m_params.matchThreshold;
    options.useMultiTemplate = true;
    if (m_preset == ScrollCapturePreset::ChatHistory) {
        options.xTrimLeftRatio = 0.12;
        options.xTrimRightRatio = 0.88;
        options.minDeltaDownscaled = 10;
    }
    m_stitcher.setOptions(options);

    QImage prevContent = cropContentFrame(frame0);
    QImage currContent = cropContentFrame(frame1);
    if (prevContent.isNull() || currContent.isNull()) {
        finishWithError(QStringLiteral("Failed to crop content area."));
        return;
    }

    if (!m_stitcher.initialize(prevContent)) {
        finishWithError(QStringLiteral("Failed to initialize stitcher."));
        return;
    }

    ScrollStitcher::MatchResult firstMatch = m_stitcher.appendFromPair(prevContent, currContent);
    if (!firstMatch.valid) {
        const double firstDiff = meanDiff(prevContent, currContent);
        if (firstDiff >= kFallbackMotionThreshold) {
            const int fallbackDelta = std::clamp(
                m_params.minStepPx,
                m_params.minDeltaFullPx,
                std::max(m_params.minDeltaFullPx, currContent.height() - 1));
            if (m_stitcher.appendByDelta(currContent, fallbackDelta)) {
                firstMatch.valid = true;
                firstMatch.deltaFullPx = fallbackDelta;
                firstMatch.weak = true;
                firstMatch.message = QStringLiteral("fallback_delta");
                m_lastWarning = tr("Using fallback stitch for initial frame.");
                emit warningRaised(m_lastWarning);
            }
        }
    }
    if (firstMatch.valid) {
        m_noProgressStreak = (firstMatch.deltaFullPx < m_params.minProgressDeltaPx) ? 1 : 0;
    } else {
        m_noProgressStreak = 1;
    }
    if (firstMatch.weak) {
        m_noProgressStreak = std::max(1, m_noProgressStreak);
    }

    const auto maybeArmWarpFallback = [this]() {
        if (m_params.noWarpRetryBeforeFallback <= 0) {
            return;
        }
        if (m_noProgressStreak < m_params.noWarpRetryBeforeFallback) {
            return;
        }
        if (m_warpFallbackUsedCount >= m_params.maxWarpFallbackCount) {
            return;
        }
        if (m_forceWarpFallbackNextStep) {
            return;
        }

        m_forceWarpFallbackNextStep = true;
        m_lastWarning = tr("No progress detected, trying fallback scroll delivery.");
        emit warningRaised(m_lastWarning);
    };
    maybeArmWarpFallback();

    m_frameCount = 2;
    if (firstMatch.valid && firstMatch.deltaFullPx < 10) {
        m_smallDeltaStreak = 1;
    }

    emit progressChanged(m_frameCount, m_stitcher.currentHeight(), tr("Capturing..."));
    prevContent = currContent;

    QString finishReason = QStringLiteral("completed");
    while (!m_stopRequested.load()) {
        if (m_cancelRequested.load()) {
            finishWithResult(false, QStringLiteral("cancelled"));
            return;
        }

        if (m_params.maxDurationMs > 0 && sessionTimer.elapsed() >= m_params.maxDurationMs) {
            finishReason = QStringLiteral("max_duration");
            break;
        }
        if (m_params.noProgressStreakLimit > 0
            && m_noProgressStreak >= m_params.noProgressStreakLimit) {
            finishReason = (m_warpFallbackUsedCount > 0)
                ? QStringLiteral("no_progress_after_fallback")
                : QStringLiteral("no_progress");
            break;
        }

        QString guardReason;
        if (shouldStopForGuards(&guardReason)) {
            finishReason = guardReason;
            break;
        }

        AutoScroller::Request request;
        request.contentRectGlobal = m_contentRect;
        request.direction = m_params.direction;
        request.stepPx = computeStepPx(m_stepMultiplier);
        request.nativeHandle = m_element.nativeHandle;
        const bool useWarpFallback = m_forceWarpFallbackNextStep
            && (m_warpFallbackUsedCount < m_params.maxWarpFallbackCount);
        request.preferNoWarp = !useWarpFallback;
        request.allowCursorWarpFallback = useWarpFallback;

        if (m_stopRequested.load()) {
            finishReason = QStringLiteral("stopped_by_user");
            break;
        }
        if (useWarpFallback) {
            m_lastWarning = tr("Using fallback scroll delivery.");
            emit warningRaised(m_lastWarning);
        }
        if (!m_autoScroller.scroll(request, &scrollError)) {
            finishWithError(scrollError);
            return;
        }
        if (useWarpFallback) {
            ++m_warpFallbackUsedCount;
            m_forceWarpFallbackNextStep = false;
        }

        FrameStabilityDetector::Result settleResult = m_stabilityDetector.waitUntilStable(
            m_captureEngine.get(),
            m_contentRect,
            m_windowRect,
            settleParams,
            &m_cancelRequested,
            &m_stopRequested);
        if (m_cancelRequested.load()) {
            finishWithResult(false, QStringLiteral("cancelled"));
            return;
        }
        if (m_stopRequested.load()) {
            finishReason = QStringLiteral("stopped_by_user");
            break;
        }

        QImage newWindowFrame = settleResult.frame;
        if (newWindowFrame.isNull()
            && !captureWindowFrame(&newWindowFrame, &initError, kFollowupFrameTimeoutMs)) {
            if (m_stopRequested.load()) {
                finishReason = QStringLiteral("stopped_by_user");
                break;
            }
            finishWithError(initError);
            return;
        }

        QImage newContent = cropContentFrame(newWindowFrame);
        if (newContent.isNull()) {
            finishWithError(QStringLiteral("Failed to crop a captured frame."));
            return;
        }

        const double diff = meanDiff(prevContent, newContent);
        if (diff < 1.0) {
            ++m_noChangeStreak;
        } else {
            m_noChangeStreak = 0;
        }

        ScrollStitcher::MatchResult match = m_stitcher.appendFromPair(prevContent, newContent);
        if (!match.valid) {
            ++m_weakMatchStreak;
            m_lastWarning = tr("Weak frame match (%1).").arg(match.score, 0, 'f', 2);
            emit warningRaised(m_lastWarning);

            if (diff >= kFallbackMotionThreshold) {
                const int fallbackDelta = std::clamp(
                    request.stepPx,
                    m_params.minDeltaFullPx,
                    std::max(m_params.minDeltaFullPx, newContent.height() - 1));
                if (m_stitcher.appendByDelta(newContent, fallbackDelta)) {
                    m_lastWarning = tr("Using fallback stitch for low-confidence frame.");
                    emit warningRaised(m_lastWarning);
                    m_weakMatchStreak = 0;
                    m_stepMultiplier = 1.0;
                    ++m_frameCount;
                    m_smallDeltaStreak = (fallbackDelta < 10) ? (m_smallDeltaStreak + 1) : 0;
                    m_noProgressStreak =
                        (fallbackDelta < m_params.minProgressDeltaPx)
                            ? (m_noProgressStreak + 1)
                            : 0;
                    if (m_noProgressStreak == 0) {
                        m_forceWarpFallbackNextStep = false;
                    } else {
                        maybeArmWarpFallback();
                    }
                    prevContent = newContent;
                    emit progressChanged(m_frameCount, m_stitcher.currentHeight(), tr("Capturing..."));

                    if (m_smallDeltaStreak >= 3) {
                        finishReason = QStringLiteral("edge_reached");
                        break;
                    }
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
                    continue;
                }
            }

            ++m_noProgressStreak;
            maybeArmWarpFallback();

            if (m_weakMatchStreak >= 3) {
                if (m_stepMultiplier > 0.55) {
                    m_stepMultiplier *= 0.7;
                    m_weakMatchStreak = 0;
                    m_lastWarning = tr("Reducing scroll step and retrying.");
                    emit warningRaised(m_lastWarning);
                } else {
                    finishReason = QStringLiteral("weak_match_limit");
                    break;
                }
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            continue;
        }

        m_stepMultiplier = 1.0;
        m_weakMatchStreak = 0;
        if (match.deltaFullPx < 10) {
            ++m_smallDeltaStreak;
        } else {
            m_smallDeltaStreak = 0;
        }
        if (match.deltaFullPx < m_params.minProgressDeltaPx) {
            ++m_noProgressStreak;
        } else {
            m_noProgressStreak = 0;
            m_forceWarpFallbackNextStep = false;
        }
        if (m_noProgressStreak > 0) {
            maybeArmWarpFallback();
        }

        ++m_frameCount;
        prevContent = newContent;
        emit progressChanged(m_frameCount, m_stitcher.currentHeight(), tr("Capturing..."));

        if (m_smallDeltaStreak >= 3) {
            finishReason = QStringLiteral("edge_reached");
            break;
        }
        if (m_noChangeStreak >= 3) {
            finishReason = QStringLiteral("no_change_detected");
            break;
        }

        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }

    if (m_cancelRequested.load()) {
        finishWithResult(false, QStringLiteral("cancelled"));
        return;
    }

    if (m_stopRequested.load() && finishReason == QStringLiteral("completed")) {
        finishReason = QStringLiteral("stopped_by_user");
    }

    emit stateChanged(ScrollCaptureState::Stitching);
    emit progressChanged(m_frameCount, m_stitcher.currentHeight(), tr("Stitching..."));
    finishWithResult(true, finishReason, m_lastWarning);
}

bool ScrollCaptureSession::captureWindowFrame(QImage* outFrame,
                                              QString* errorMessage,
                                              int waitTimeoutMs) const
{
    if (!outFrame) {
        return false;
    }
    if (!m_captureEngine) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Capture engine is not initialized.");
        }
        return false;
    }
    if (m_stopRequested.load()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Capture stopped.");
        }
        return false;
    }

    auto tryCaptureOnce = [this, outFrame]() {
        QImage frame = m_captureEngine->captureFrame();
        if (frame.isNull()) {
            return false;
        }

        *outFrame = frame;
        return true;
    };

    if (tryCaptureOnce()) {
        return true;
    }

    if (waitTimeoutMs > 0) {
        QElapsedTimer timer;
        timer.start();

        while (timer.elapsed() < waitTimeoutMs) {
            if (m_cancelRequested.load()) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("Capture cancelled.");
                }
                return false;
            }
            if (m_stopRequested.load()) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("Capture stopped.");
                }
                return false;
            }

            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            QThread::msleep(kFramePollIntervalMs);

            if (tryCaptureOnce()) {
                return true;
            }
        }
    }

    if (errorMessage) {
        if (waitTimeoutMs > 0) {
            *errorMessage = QStringLiteral(
                "Timed out waiting for the first capture frame from engine.");
            qWarning() << "ScrollCaptureSession: timed out waiting for frame"
                       << "timeoutMs=" << waitTimeoutMs
                       << "engine=" << m_captureEngine->engineName();
        } else {
            *errorMessage = QStringLiteral("Failed to capture frame from engine.");
        }
    }
    return false;
}

QRect ScrollCaptureSession::logicalToImageRect(const QRect& logicalRect, const QImage& image) const
{
    if (!m_windowRect.isValid() || image.isNull() || !logicalRect.isValid()) {
        return {};
    }

    const QRect localRect = logicalRect.translated(-m_windowRect.topLeft());
    const double scaleX = static_cast<double>(image.width()) / static_cast<double>(m_windowRect.width());
    const double scaleY = static_cast<double>(image.height()) / static_cast<double>(m_windowRect.height());

    QRect imageRect(
        static_cast<int>(std::round(localRect.x() * scaleX)),
        static_cast<int>(std::round(localRect.y() * scaleY)),
        static_cast<int>(std::round(localRect.width() * scaleX)),
        static_cast<int>(std::round(localRect.height() * scaleY))
    );
    return imageRect.intersected(QRect(0, 0, image.width(), image.height()));
}

QImage ScrollCaptureSession::cropContentFrame(const QImage& windowFrame) const
{
    if (windowFrame.isNull()) {
        return {};
    }
    QRect sourceRect = logicalToImageRect(m_contentRect, windowFrame);
    if (!sourceRect.isValid()) {
        sourceRect = QRect(0, 0, windowFrame.width(), windowFrame.height());
    }
    return windowFrame.copy(sourceRect);
}

bool ScrollCaptureSession::shouldStopForGuards(QString* reason) const
{
    if (m_frameCount >= m_params.maxFrames) {
        if (reason) {
            *reason = QStringLiteral("max_frames");
        }
        return true;
    }

    if (m_stitcher.currentHeight() >= m_params.maxHeightPx) {
        if (reason) {
            *reason = QStringLiteral("max_height");
        }
        return true;
    }
    return false;
}

int ScrollCaptureSession::computeStepPx(double multiplier) const
{
    const int baseHeight = m_contentRect.isValid() ? m_contentRect.height() : m_windowRect.height();
    const int scaled = static_cast<int>(std::round(baseHeight * m_params.stepRatio * multiplier));
    const int maxStep = static_cast<int>(std::round(baseHeight * m_params.maxStepRatio));
    return std::clamp(scaled, m_params.minStepPx, std::max(m_params.minStepPx, maxStep));
}

void ScrollCaptureSession::finishWithError(const QString& reason)
{
    emit stateChanged(ScrollCaptureState::Error);
    finishWithResult(false, reason);
}

void ScrollCaptureSession::finishWithResult(bool success, const QString& reason, const QString& warning)
{
    if (m_hasResult) {
        return;
    }
    m_hasResult = true;

    if (m_captureEngine) {
        m_captureEngine->stop();
    }
    m_autoScroller.restoreCursor();

    ScrollCaptureResult result;
    result.success = success;
    result.frameCount = m_stitcher.frameCount();
    result.stitchedHeight = m_stitcher.currentHeight();
    result.reason = reason;
    result.warning = warning;
    if (success) {
        result.pixmap = m_stitcher.compose();
        result.success = !result.pixmap.isNull();
    }

    if (result.success) {
        emit stateChanged(ScrollCaptureState::Completed);
    } else if (reason == QStringLiteral("cancelled")
               || reason == QStringLiteral("stopped_by_user")) {
        emit stateChanged(ScrollCaptureState::Cancelled);
    } else {
        emit stateChanged(ScrollCaptureState::Error);
    }

    m_running = false;
    emit finished(result);
}
