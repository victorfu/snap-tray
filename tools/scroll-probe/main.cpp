// Developer probe for #265: shows the scroll area the platform accessibility
// API reports under the cursor, and measures programmatic scrolling against
// lossless captures. Writes one JSON line per measurement. Not part of the
// product; it only drives the scrollcapture modules the product will use.
//
//   snaptray-scroll-probe inspect [--level N] [--ax-boost MODE]
//   snaptray-scroll-probe measure [--level N] [--ax-boost MODE]
//                                 [--scroll METHOD --amount N --repeats N]
//                                 [--output FILE] [--app-label TEXT] [--note TEXT]
#include "capture/ScreenSnapshot.h"
#include "platform/WindowLevel.h"
#include "scrollcapture/FrameRowHashes.h"
#include "scrollcapture/FrameSettleDetector.h"
#include "scrollcapture/RowHashShiftEstimator.h"
#include "scrollcapture/ScrollAreaLocator.h"
#include "scrollcapture/ScrollInjector.h"
#include "ContentExport.h"
#include "InspectState.h"
#include "ProbeEventLoop.h"
#include "hotkey/HotkeyManager.h"
#include "platform/KeyboardModifiers.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QScreen>
#include <QSysInfo>
#include <QTextStream>
#include <QThread>
#include <QSet>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <optional>
#include <vector>

using namespace snaptray::scrollcapture;
using namespace snaptray::scrollcapture::probe;

namespace {

constexpr int kSchemaVersion = 8;
// `measure --content-output` ran and wrote its record, but exported no image.
constexpr int kContentExportRejectedExitCode = 3;
constexpr int kInspectIntervalMs = 150;
constexpr int kCountdownPollMs = 50;
constexpr int kCountdownLocateMs = 300;
constexpr int kDefaultDelaySeconds = 3;
constexpr int kDefaultAttempts = 3;
constexpr int kDefaultRepeats = 10;
constexpr int kDefaultAmount = 120;
constexpr int kDefaultSearchRadius = 64;
constexpr int kDefaultTimeoutMs = 1500;
constexpr int kDefaultRepeatIntervalMs = 300;
constexpr int kDefaultAxWaitMs = 1000;
const QString kAxBoostAll = QStringLiteral("all");
constexpr int kOverlayHideSettleMs = 150;
constexpr int kOverlayBorder = 3;
constexpr int kOverlayLabelPadding = 4;
// Accessibility bounds can include the window's fixed border; fractional DPI
// rounding can also include an adjacent edge pixel. Do not hash that perimeter
// as scrolling content. Keep the full frames for settle detection and evidence.
constexpr int kHashHorizontalInsetLogicalPx = 2;
constexpr double kP50 = 0.5;
constexpr double kP95 = 0.95;

struct ProbeOptions {
    QString contentOutput;
    QString framesDirectory;
    bool measure = false;
    std::optional<QPoint> point;
    int level = 0;
    int delaySeconds = kDefaultDelaySeconds;
    int attempts = kDefaultAttempts;
    // "", "manual", "enhanced", "both", or "all" (measure each in turn).
    QString axBoost;
    int axWaitMs = kDefaultAxWaitMs;
    std::optional<ScrollMethod> method;
    int amount = kDefaultAmount;
    double percent = 0.0;
    int repeats = kDefaultRepeats;
    bool postToProcess = false;
    int timeoutMs = kDefaultTimeoutMs;
    int strictPairs = 1;
    int searchRadius = kDefaultSearchRadius;
    int repeatIntervalMs = kDefaultRepeatIntervalMs;
    QString output;
    QString appLabel;
    QString note;
};

// Ctrl+C must not leave a target application with the accessibility
// attributes this probe turned on, so it only sets a flag; the event loop and
// the measurement loops check it and exit through the normal cleanup path.
std::atomic<bool> g_stopRequested{false};

extern "C" void requestStop(int)
{
    g_stopRequested.store(true);
}

bool stopRequested()
{
    return g_stopRequested.load();
}

QList<AccessibilityBoost> boostsFor(const QString& mode)
{
    if (mode == kAxBoostAll) {
        return {AccessibilityBoost::ManualAccessibility, AccessibilityBoost::EnhancedUserInterface,
                AccessibilityBoost::Both};
    }
    if (const auto boost = accessibilityBoostFromName(mode)) {
        return {*boost};
    }
    return {};
}

QTextStream& out()
{
    static QTextStream stream(stdout);
    return stream;
}

QTextStream& err()
{
    static QTextStream stream(stderr);
    return stream;
}

// Border around the detected scroll area. Input passes through it and it is
// excluded from captures; measurements also hide it before capturing.
class HighlightOverlay : public QWidget
{
public:
    HighlightOverlay()
        : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool |
                               Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
    }

    void showFor(const QRect& bounds, const QString& label)
    {
        m_label = label;
        setGeometry(bounds.adjusted(-kOverlayBorder, -kOverlayBorder, kOverlayBorder, kOverlayBorder));
        if (!isVisible()) {
            show();
            setWindowExcludedFromCapture(this, true);
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setPen(QPen(QColor(255, 140, 0), kOverlayBorder));
        painter.drawRect(rect().adjusted(1, 1, -2, -2));
        if (m_label.isEmpty()) {
            return;
        }
        const QRect textRect = painter.fontMetrics()
                                   .boundingRect(rect().adjusted(8, 8, -8, -8), Qt::TextWordWrap, m_label)
                                   .adjusted(-kOverlayLabelPadding, -kOverlayLabelPadding,
                                             kOverlayLabelPadding, kOverlayLabelPadding);
        painter.fillRect(textRect, QColor(0, 0, 0, 190));
        painter.setPen(Qt::white);
        painter.drawText(textRect.adjusted(kOverlayLabelPadding, kOverlayLabelPadding,
                                           -kOverlayLabelPadding, -kOverlayLabelPadding),
                         Qt::TextWordWrap, m_label);
    }

private:
    QString m_label;
};

QString optionalText(const std::optional<double>& value, int precision = 3)
{
    return value ? QString::number(*value, 'f', precision) : QStringLiteral("-");
}

QString optionalText(const std::optional<bool>& value)
{
    return value ? (*value ? QStringLiteral("yes") : QStringLiteral("no")) : QStringLiteral("-");
}

QString describe(const ScrollAreaSnapshot& snapshot)
{
    const ScrollPosition& position = snapshot.position;
    return QStringLiteral("%1 %2  level %3/%4  %5x%6 at (%7,%8)\n"
                          "fraction %9  viewport %10  offset %11/%12  top %13  bottom %14")
        .arg(snapshot.appName, snapshot.role)
        .arg(snapshot.level + 1)
        .arg(snapshot.availableLevels)
        .arg(snapshot.bounds.width())
        .arg(snapshot.bounds.height())
        .arg(snapshot.bounds.x())
        .arg(snapshot.bounds.y())
        .arg(optionalText(position.fraction), optionalText(position.viewportRatio),
             optionalText(position.offset, 1), optionalText(position.contentHeight, 1),
             optionalText(position.atTop), optionalText(position.atBottom));
}

struct Frame {
    QImage image;
    // Device pixels per logical pixel.
    double scale = 1.0;
};

Frame captureArea(const QRect& bounds)
{
    QScreen* screen = QGuiApplication::screenAt(bounds.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return {};
    }
    const QRect geometry = screen->geometry();
    const QPixmap shot = snaptray::capture::captureScreenSnapshot(screen);
    if (shot.isNull() || geometry.isEmpty()) {
        return {};
    }
    const double scale = static_cast<double>(shot.width()) / geometry.width();
    const QRect local = bounds.intersected(geometry).translated(-geometry.topLeft());
    const QRect device(qRound(local.x() * scale), qRound(local.y() * scale),
                       qRound(local.width() * scale), qRound(local.height() * scale));
    return {shot.copy(device).toImage(), scale};
}

QJsonObject locateToJson(const LocateResult& result)
{
    QJsonObject object{
        {QStringLiteral("status"), QLatin1String(locateStatusName(result.status))},
        {QStringLiteral("detail"), result.detail},
        {QStringLiteral("attempts"), result.attempts},
        {QStringLiteral("processId"), result.processId},
    };
    if (result.area) {
        object.insert(QStringLiteral("area"), toJson(result.area->snapshot()));
    }
    return object;
}

QJsonObject shiftToJson(const ShiftEstimate& estimate)
{
    QJsonArray candidates;
    for (int candidate : estimate.candidates) {
        candidates.append(candidate);
    }
    return QJsonObject{
        {QStringLiteral("status"), QLatin1String(shiftEstimateStatusName(estimate.status))},
        {QStringLiteral("shiftPx"), estimate.shift},
        {QStringLiteral("distinctMatches"), estimate.distinctMatches},
        {QStringLiteral("matchedRows"), estimate.matchedRows},
        {QStringLiteral("overlapRows"), estimate.overlapRows},
        {QStringLiteral("staticRows"), estimate.staticRows},
        {QStringLiteral("candidates"), candidates},
        {QStringLiteral("searchLowPx"), estimate.searchLow},
        {QStringLiteral("searchHighPx"), estimate.searchHigh},
        {QStringLiteral("runnerUpShiftPx"),
         estimate.runnerUpShift ? QJsonValue(*estimate.runnerUpShift) : QJsonValue()},
        {QStringLiteral("runnerUpVotes"), estimate.runnerUpVotes},
    };
}

QJsonValue optionalJson(const std::optional<qint64>& value)
{
    return value ? QJsonValue(*value) : QJsonValue();
}

// Why a settle timed out, from what the detector saw. An unchanged area is
// not proof of the end of the content: the input may not have taken effect.
QJsonValue timeoutReason(const FrameSettleDetector::Result& settle)
{
    if (settle.outcome != FrameSettleDetector::Outcome::TimedOut) {
        return QJsonValue();
    }
    if (!settle.motionObserved) {
        return QStringLiteral("no_scroll_motion_minor_changes_only");
    }
    return settle.relaxedSettleMs ? QStringLiteral("minor_changes_persisted_after_motion")
                                  : QStringLiteral("content_kept_changing");
}

struct RepeatOutcome {
    ContentRepeat content;
    QJsonObject json;
    std::optional<int> shiftPx;
    std::optional<qint64> strictSettleMs;
};

RepeatOutcome runRepeat(int index, const ScrollArea& area, ScrollInjector& injector,
                        const ProbeOptions& options, const ScrollAreaSnapshot& before)
{
    RepeatOutcome outcome;
    QJsonObject& json = outcome.json;
    json.insert(QStringLiteral("index"), index);
    json.insert(QStringLiteral("before"), toJson(before.position));

    const Frame start = captureArea(before.bounds);
    if (processCancellation(stopRequested)) {
        outcome.content.error = QStringLiteral("cancelled");
        return outcome;
    }
    if (start.image.isNull()) {
        outcome.content.error = QStringLiteral("capture_failed");
        json.insert(QStringLiteral("error"), QStringLiteral("screen capture failed"));
        return outcome;
    }
    json.insert(QStringLiteral("frameSize"), QStringLiteral("%1x%2").arg(start.image.width()).arg(start.image.height()));
    json.insert(QStringLiteral("scale"), start.scale);

    FrameSettleDetector::Config config;
    config.timeoutMs = options.timeoutMs;
    config.strictPairs = options.strictPairs;
    FrameSettleDetector detector(config);
    detector.start(start.image);

    ScrollRequest request;
    request.method = *options.method;
    request.amount = options.amount;
    request.percent = options.percent;
    request.postToProcess = options.postToProcess;

    using Clock = std::chrono::steady_clock;
    const Clock::time_point called = Clock::now();
    const InjectResult inject = injector.scroll(area, request);
    // Settle timing starts when the input was dispatched: finding a target
    // point beforehand must not use up the grace period or the timeout.
    const Clock::time_point dispatched = inject.dispatchedAt.value_or(Clock::now());
    const auto sinceDispatchMs = [dispatched]() {
        return static_cast<qint64>(
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - dispatched).count());
    };
    json.insert(QStringLiteral("inject"), QJsonObject{
        {QStringLiteral("ok"), inject.ok},
        {QStringLiteral("detail"), inject.detail},
        {QStringLiteral("preparationMs"), static_cast<qint64>(
            std::chrono::duration_cast<std::chrono::milliseconds>(dispatched - called).count())},
    });
    if (!inject.ok) {
        outcome.content.error = QStringLiteral("injection_failed");
        return outcome;
    }

    qint64 captureMs = 0;
    const auto loopResult = settleFrames(detector, [&] {
        QElapsedTimer captureClock;
        captureClock.start();
        const Frame frame = captureArea(before.bounds);
        captureMs += captureClock.elapsed();
        return frame.image;
    }, sinceDispatchMs, stopRequested);
    if (loopResult == SettleLoopResult::Cancelled) {
        outcome.content.error = QStringLiteral("cancelled");
        return outcome;
    }
    if (loopResult == SettleLoopResult::CaptureFailed) {
        outcome.content.error = QStringLiteral("capture_failed_while_settling");
        json.insert(QStringLiteral("error"), QStringLiteral("screen capture failed while settling"));
        return outcome;
    }
    const FrameSettleDetector::Result& settle = detector.result();
    outcome.strictSettleMs = settle.strictSettleMs;
    json.insert(QStringLiteral("settle"), QJsonObject{
        {QStringLiteral("outcome"), QLatin1String(settleOutcomeName(settle.outcome))},
        {QStringLiteral("strictSettleMs"), optionalJson(settle.strictSettleMs)},
        {QStringLiteral("strictConfirmedMs"), optionalJson(settle.strictConfirmedMs)},
        {QStringLiteral("relaxedSettleMs"), optionalJson(settle.relaxedSettleMs)},
        {QStringLiteral("motionObserved"), settle.motionObserved},
        {QStringLiteral("frames"), settle.frames},
        {QStringLiteral("averageCaptureMs"), settle.frames > 0 ? static_cast<double>(captureMs) / settle.frames : 0.0},
        {QStringLiteral("lastChangedTiles"), settle.lastChangedTiles},
        {QStringLiteral("timeoutReason"), timeoutReason(settle)},
    });

    const int hashInset = static_cast<int>(std::ceil(kHashHorizontalInsetLogicalPx * start.scale));
    const auto visual = estimateVerticalShiftByBands(start.image, detector.lastFrame(), {}, hashInset);
    ScrollAreaSnapshot after = area.snapshot();
    json.insert(QStringLiteral("afterAtVisualSettle"), toJson(after.position));
    // Accessibility can lag the compositor. Wait for an independently measured
    // displacement to enter the API prior's search window. The platform offset
    // is approximate; do not require pixel equality or replace it with pixels.
    // This is bounded and fails closed if the captured frame changes meanwhile.
    constexpr int kPositionPollMs = 50;
    const qint64 syncStartedMs = sinceDispatchMs();
    QJsonArray positionSamples;
    QString syncOutcome = QStringLiteral("not_available");
    const QString syncUnavailableReason = positionSyncUnavailableReason(
        settle.outcome == FrameSettleDetector::Outcome::Settled,
        before.position.offset.has_value(), after.position.offset.has_value(), visual.estimate);
    if (syncUnavailableReason.isEmpty()) {
        while (true) {
            const double apiShift = (*after.position.offset - *before.position.offset) * start.scale;
            if (std::abs(qRound(apiShift) - visual.estimate.shift) <= options.searchRadius) {
                syncOutcome = QStringLiteral("within_search_window");
                break;
            }
            if (sinceDispatchMs() - syncStartedMs >= options.timeoutMs || stopRequested()) {
                syncOutcome = QStringLiteral("timed_out");
                break;
            }
            QThread::msleep(kPositionPollMs);
            // UIA is created in the application's STA. Keep its message queue
            // serviced while waiting for asynchronous accessibility updates.
            if (processCancellation(stopRequested)) {
                outcome.content.error = QStringLiteral("cancelled");
                return outcome;
            }
            after = area.snapshot();
            positionSamples.append(QJsonObject{
                {QStringLiteral("elapsedMs"), sinceDispatchMs() - syncStartedMs},
                {QStringLiteral("position"), toJson(after.position)}});
            const Frame check = captureArea(before.bounds);
            if (check.image.isNull() || check.image != detector.lastFrame() || after.bounds != before.bounds) {
                syncOutcome = QStringLiteral("frame_changed");
                break;
            }
            if (!after.position.offset) {
                syncOutcome = QStringLiteral("position_missing");
                break;
            }
        }
    }
    json.insert(QStringLiteral("positionSync"), QJsonObject{
        {QStringLiteral("outcome"), syncOutcome},
        {QStringLiteral("unavailableReason"), syncUnavailableReason.isEmpty()
            ? QJsonValue() : QJsonValue(syncUnavailableReason)},
        {QStringLiteral("confirmedMs"), syncOutcome == QStringLiteral("within_search_window")
            ? QJsonValue(sinceDispatchMs()) : QJsonValue()},
        {QStringLiteral("samples"), positionSamples},
        {QStringLiteral("waitMs"), sinceDispatchMs() - syncStartedMs},
        {QStringLiteral("tolerancePx"), options.searchRadius}});
    json.insert(QStringLiteral("after"), toJson(after.position));

    if (!options.framesDirectory.isEmpty()) {
        QDir directory(options.framesDirectory);
        const QString prefix = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"))
            + QStringLiteral("-%1").arg(index);
        const QString beforePath = directory.absoluteFilePath(prefix + QStringLiteral("-before.png"));
        const QString afterPath = directory.absoluteFilePath(prefix + QStringLiteral("-after.png"));
        if (directory.mkpath(QStringLiteral(".")) && start.image.save(beforePath)
            && detector.lastFrame().save(afterPath)) {
            json.insert(QStringLiteral("frames"), QJsonObject{
                {QStringLiteral("before"), beforePath}, {QStringLiteral("after"), afterPath}});
        } else {
            json.insert(QStringLiteral("frameSaveError"), QStringLiteral("could not save frame pair"));
        }
    }

    json.insert(QStringLiteral("hashHorizontalInsetPx"), hashInset);
    const QVector<quint64> beforeRows = computeRowHashes(start.image, hashInset);
    const QVector<quint64> afterRows = computeRowHashes(detector.lastFrame(), hashInset);
    ShiftEstimatorOptions shiftOptions;
    shiftOptions.searchRadius = options.searchRadius;
    // The prior comes only from the platform-reported position; when that is
    // missing the search covers every shift rather than guessing from earlier
    // repeats.
    if (before.position.offset && after.position.offset) {
        const double apiShift = (*after.position.offset - *before.position.offset) * start.scale;
        json.insert(QStringLiteral("apiShiftPx"), apiShift);
        json.insert(QStringLiteral("expectedShiftSource"), QStringLiteral("platform_offset"));
        shiftOptions.expectedShift = qRound(apiShift);
    } else {
        json.insert(QStringLiteral("expectedShiftSource"), QStringLiteral("none"));
        json.insert(QStringLiteral("expectedShiftMissing"),
                    !before.position.offset ? QStringLiteral("offset_before") : QStringLiteral("offset_after"));
    }
    const auto bandEstimate = estimateVerticalShiftByBands(start.image, detector.lastFrame(), shiftOptions, hashInset);
    ShiftEstimate estimate = bandEstimate.alignmentEstimate;
    QJsonArray bands;
    QJsonArray observedBands;
    for (const auto& band : bandEstimate.bands) {
        bands.append(shiftToJson(band));
    }
    for (const auto& band : bandEstimate.observedBands) {
        observedBands.append(shiftToJson(band));
    }
    QJsonArray conflictingBands;
    QJsonArray unresolvedBands;
    for (int band : bandEstimate.conflictingBands) conflictingBands.append(band);
    for (int band : bandEstimate.unresolvedBands) unresolvedBands.append(band);
    json.insert(QStringLiteral("dominantShift"), shiftToJson(bandEstimate.estimate));
    json.insert(QStringLiteral("bandMatching"), QJsonObject{
        {QStringLiteral("bands"), bands},
        {QStringLiteral("observedBands"), observedBands},
        {QStringLiteral("supportingBands"), bandEstimate.supportingBands},
        {QStringLiteral("conflictingBands"), conflictingBands},
        {QStringLiteral("unresolvedBands"), unresolvedBands},
        {QStringLiteral("nonUniformMotion"), bandEstimate.nonUniformMotion}});
    json.insert(QStringLiteral("shiftMethod"), QStringLiteral("consistent_vertical_bands"));
    json.insert(QStringLiteral("fullRowShift"), shiftToJson(estimateVerticalShift(beforeRows, afterRows, shiftOptions)));
    // Failed settling or an attempted synchronization rejects alignment.
    // An unavailable synchronization remains allowed for viewport diagnostics;
    // content export requires a successful check. Keep the measured status too.
    QString rejectedBy;
    if (settle.outcome != FrameSettleDetector::Outcome::Settled) {
        rejectedBy = QStringLiteral("settle_") + QLatin1String(settleOutcomeName(settle.outcome));
    } else if (syncOutcome == QStringLiteral("timed_out") || syncOutcome == QStringLiteral("frame_changed")
               || syncOutcome == QStringLiteral("position_missing")) {
        rejectedBy = QStringLiteral("position_sync_") + syncOutcome;
    }
    const ShiftEstimate::Status measuredStatus = estimate.status;
    if (!rejectedBy.isEmpty()) {
        estimate.status = ShiftEstimate::Status::NotFound;
    }
    QJsonObject shiftJson = shiftToJson(estimate);
    if (!rejectedBy.isEmpty()) {
        shiftJson.insert(QStringLiteral("rejectedBy"), rejectedBy);
        shiftJson.insert(QStringLiteral("measuredStatus"), QLatin1String(shiftEstimateStatusName(measuredStatus)));
    }
    json.insert(QStringLiteral("shift"), shiftJson);
    if (shiftOptions.expectedShift) {
        // The same frames without the platform prior, to show how far the
        // reported position is from the measured one.
        json.insert(QStringLiteral("shiftWithoutPrior"),
                    shiftToJson(visual.estimate));
    }
    if (estimate.status == ShiftEstimate::Status::Found) {
        outcome.shiftPx = estimate.shift;
    }
    if (!options.contentOutput.isEmpty()) {
        outcome.content = validateContentRepeat(start.image, detector.lastFrame(), settle.outcome,
            syncOutcome, before.bounds == after.bounds, bandEstimate.estimate, syncUnavailableReason);
    }
    return outcome;
}

double percentile(std::vector<qint64> values, double fraction)
{
    std::sort(values.begin(), values.end());
    const auto rank = static_cast<size_t>(std::ceil(fraction * static_cast<double>(values.size())));
    return static_cast<double>(values[std::clamp<size_t>(rank, 1, values.size()) - 1]);
}

QJsonObject statistics(const std::vector<int>& shifts, const std::vector<qint64>& settleTimes, int repeats)
{
    QJsonObject stats{
        {QStringLiteral("repeats"), repeats},
        {QStringLiteral("shiftsFound"), static_cast<int>(shifts.size())},
        {QStringLiteral("strictSettles"), static_cast<int>(settleTimes.size())},
    };
    if (!shifts.empty()) {
        double sum = 0.0;
        for (int shift : shifts) {
            sum += shift;
        }
        const double mean = sum / static_cast<double>(shifts.size());
        double squares = 0.0;
        for (int shift : shifts) {
            squares += (shift - mean) * (shift - mean);
        }
        const double deviation = shifts.size() > 1 ? std::sqrt(squares / static_cast<double>(shifts.size() - 1)) : 0.0;
        stats.insert(QStringLiteral("shiftMeanPx"), mean);
        stats.insert(QStringLiteral("shiftStddevPx"), deviation);
        stats.insert(QStringLiteral("shiftMinPx"), *std::min_element(shifts.cbegin(), shifts.cend()));
        stats.insert(QStringLiteral("shiftMaxPx"), *std::max_element(shifts.cbegin(), shifts.cend()));
    }
    if (!settleTimes.empty()) {
        stats.insert(QStringLiteral("strictSettleP50Ms"), percentile(settleTimes, kP50));
        stats.insert(QStringLiteral("strictSettleP95Ms"), percentile(settleTimes, kP95));
    }
    return stats;
}

QJsonObject runScrollPhase(const ScrollArea& area, ScrollInjector& injector, const ProbeOptions& options)
{
    QJsonObject phase{
        {QStringLiteral("method"), QLatin1String(scrollMethodName(*options.method))},
        {QStringLiteral("amount"), options.amount},
        {QStringLiteral("percent"), options.percent},
        {QStringLiteral("postToProcess"), options.postToProcess},
        {QStringLiteral("timeoutMs"), options.timeoutMs},
        {QStringLiteral("strictPairs"), options.strictPairs},
        {QStringLiteral("minStableMs"), FrameSettleDetector::Config{}.minStableMs},
        {QStringLiteral("searchRadius"), options.searchRadius},
    };
    QJsonArray repeats;
    ContentExportSession contentSession;
    std::vector<int> shifts;
    std::vector<qint64> settleTimes;
    const bool towardBottom = options.method == ScrollMethod::PatternSetPercent || options.amount >= 0;
    for (int index = 0; index < options.repeats && !stopRequested(); ++index) {
        const ScrollAreaSnapshot before = area.snapshot();
        const std::optional<bool> atEnd = towardBottom ? before.position.atBottom : before.position.atTop;
        if (atEnd.value_or(false) && options.method != ScrollMethod::PatternSetPercent) {
            phase.insert(QStringLiteral("stoppedAtEnd"), index);
            break;
        }
        const RepeatOutcome outcome = runRepeat(index, area, injector, options, before);
        if (processCancellation(stopRequested)) break;
        repeats.append(outcome.json);
        if (!options.contentOutput.isEmpty()) {
            contentSession.add(index, outcome.content);
        }
        if (outcome.shiftPx) {
            shifts.push_back(*outcome.shiftPx);
        }
        if (outcome.strictSettleMs) {
            settleTimes.push_back(*outcome.strictSettleMs);
        }
        waitWithCancellation(options.repeatIntervalMs, stopRequested);
    }
    phase.insert(QStringLiteral("repeats"), repeats);
    phase.insert(QStringLiteral("stats"), statistics(shifts, settleTimes, static_cast<int>(repeats.size())));
    if (!options.contentOutput.isEmpty()) {
        QJsonObject content = contentSession.finish(options.contentOutput, stopRequested());
        if (content.value(QStringLiteral("status")).toString() == QLatin1String("exported")) {
            std::vector<int> contentShifts;
            for (const auto& pair : contentSession.pairs()) contentShifts.push_back(pair.shift);
            content.insert(QStringLiteral("stats"), statistics(contentShifts, settleTimes, int(repeats.size())));
        }
        phase.insert(QStringLiteral("contentExport"), content);
    }
    return phase;
}

QJsonObject screenToJson(const QPoint& point)
{
    QScreen* screen = QGuiApplication::screenAt(point);
    if (!screen) {
        return {};
    }
    return QJsonObject{
        {QStringLiteral("name"), screen->name()},
        {QStringLiteral("devicePixelRatio"), screen->devicePixelRatio()},
        {QStringLiteral("logicalDpi"), screen->logicalDotsPerInch()},
        {QStringLiteral("width"), screen->geometry().width()},
        {QStringLiteral("height"), screen->geometry().height()},
    };
}

void writeRecord(const QJsonObject& record, const QString& path)
{
    const QByteArray line = QJsonDocument(record).toJson(QJsonDocument::Compact);
    out() << line << '\n';
    out().flush();
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        err() << "cannot write " << path << ": " << file.errorString() << '\n';
        return;
    }
    file.write(line);
    file.write("\n");
}

// Shows the area under the cursor while counting down, so the operator can
// check what is about to be measured.
void countdown(ScrollAreaLocator& locator, HighlightOverlay& overlay, const ProbeOptions& options)
{
    QElapsedTimer clock;
    clock.start();
    qint64 lastLocate = -kCountdownLocateMs;
    int lastAnnounced = -1;
    const qint64 totalMs = static_cast<qint64>(options.delaySeconds) * 1000;
    while (clock.elapsed() < totalMs && !stopRequested()) {
        const int remaining = static_cast<int>((totalMs - clock.elapsed() + 999) / 1000);
        if (remaining != lastAnnounced) {
            err() << "measuring in " << remaining << "s...\n";
            err().flush();
            lastAnnounced = remaining;
        }
        if (clock.elapsed() - lastLocate >= kCountdownLocateMs) {
            lastLocate = clock.elapsed();
            LocateOptions locateOptions;
            locateOptions.level = options.level;
            const LocateResult result = locator.locateAt(options.point.value_or(QCursor::pos()), locateOptions);
            if (result.area) {
                const ScrollAreaSnapshot snapshot = result.area->snapshot();
                overlay.showFor(snapshot.bounds, QStringLiteral("%1s\n%2").arg(remaining).arg(describe(snapshot)));
            } else {
                overlay.hide();
            }
        }
        QCoreApplication::processEvents();
        QThread::msleep(kCountdownPollMs);
    }
    overlay.hide();
    QCoreApplication::processEvents();
    QThread::msleep(kOverlayHideSettleMs);
    QCoreApplication::processEvents();
}

int runMeasure(const ProbeOptions& options)
{
    auto locator = ScrollAreaLocator::create();
    auto injector = ScrollInjector::create();
    HighlightOverlay overlay;
    countdown(*locator, overlay, options);

    const QPoint cursor = QCursor::pos();
    const QPoint target = options.point.value_or(cursor);
    QJsonObject record{
        {QStringLiteral("tool"), QStringLiteral("snaptray-scroll-probe")},
        {QStringLiteral("schema"), kSchemaVersion},
        {QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("os"), QSysInfo::prettyProductName()},
        {QStringLiteral("kernel"), QSysInfo::kernelVersion()},
        {QStringLiteral("appLabel"), options.appLabel},
        {QStringLiteral("note"), options.note},
        {QStringLiteral("cursor"), QJsonObject{{QStringLiteral("x"), cursor.x()}, {QStringLiteral("y"), cursor.y()}}},
        {QStringLiteral("level"), options.level},
        {QStringLiteral("targetPoint"), QJsonObject{{QStringLiteral("x"), target.x()}, {QStringLiteral("y"), target.y()}}},
        {QStringLiteral("targetPointSource"), options.point ? QStringLiteral("explicit") : QStringLiteral("cursor")},
        {QStringLiteral("screen"), screenToJson(target)},
        {QStringLiteral("environment"), locator->environment()},
    };

    LocateOptions locateOptions;
    locateOptions.level = options.level;
    locateOptions.maxAttempts = options.attempts;
    LocateResult located = locator->locateAt(target, locateOptions);
    record.insert(QStringLiteral("locate"), locateToJson(located));

    const QList<AccessibilityBoost> boosts = boostsFor(options.axBoost);
    const qint64 targetProcessId = located.processId;
    if (!boosts.isEmpty() && targetProcessId != 0) {
        // Each mode starts from the application's own state, so the results
        // show which request actually builds the tree.
        QJsonArray attempts;
        for (const AccessibilityBoost boost : boosts) {
            if (stopRequested()) {
                break;
            }
            QJsonObject attempt = locator->requestFullAccessibilityTree(targetProcessId, boost);
            QThread::msleep(static_cast<unsigned long>(options.axWaitMs));
            attempt.insert(QStringLiteral("locate"), locateToJson(locator->locateAt(target, locateOptions)));
            locator->restoreAccessibility();
            attempts.append(attempt);
            QThread::msleep(static_cast<unsigned long>(options.axWaitMs));
        }
        record.insert(QStringLiteral("accessibilityBoost"), QJsonObject{
            {QStringLiteral("waitMs"), options.axWaitMs},
            {QStringLiteral("attempts"), attempts},
        });
        if (options.method) {
            // Scroll with the tree built; `all` uses both attributes.
            const AccessibilityBoost scrollBoost =
                boosts.size() == 1 ? boosts.first() : AccessibilityBoost::Both;
            record.insert(QStringLiteral("scrollAccessibilityBoost"),
                          locator->requestFullAccessibilityTree(targetProcessId, scrollBoost));
            QThread::msleep(static_cast<unsigned long>(options.axWaitMs));
            located = locator->locateAt(target, locateOptions);
        }
    }

    if (options.method && !stopRequested()) {
        if (located.area) {
            record.insert(QStringLiteral("scroll"), runScrollPhase(*located.area, *injector, options));
        } else {
            QJsonObject skipped{{QStringLiteral("skipped"), QStringLiteral("no scroll area")}};
            if (!options.contentOutput.isEmpty()) {
                skipped.insert(QStringLiteral("contentExport"), QJsonObject{
                    {QStringLiteral("status"), QStringLiteral("rejected")},
                    {QStringLiteral("reason"), QStringLiteral("no_scroll_area")},
                    {QStringLiteral("path"), options.contentOutput},
                    {QStringLiteral("staleFileAtPath"), QFile::exists(options.contentOutput)}});
            }
            record.insert(QStringLiteral("scroll"), skipped);
        }
        QCursor::setPos(cursor);
    }
    locator->restoreAccessibility();

    if (processCancellation(stopRequested)) {
        err() << "stopped; accessibility attributes restored, nothing written\n";
        return 1;
    }
    writeRecord(record, options.output);
    if (!options.contentOutput.isEmpty()) {
        // The record is written either way, but a requested export that was
        // not produced must not look like success to a calling script.
        const QJsonObject content = record.value(QStringLiteral("scroll")).toObject()
            .value(QStringLiteral("contentExport")).toObject();
        if (content.value(QStringLiteral("status")).toString() != QStringLiteral("exported")) {
            err() << "content export rejected: " << content.value(QStringLiteral("reason")).toString()
                  << "; " << options.contentOutput << " was not written\n";
            return kContentExportRejectedExitCode;
        }
    }
    return 0;
}

int runInspect(const ProbeOptions& options)
{
    auto locator = ScrollAreaLocator::create();
    if (!locator->isAvailable()) {
        err() << "scroll area detection is not available on this platform\n";
        return 1;
    }

    // `all` is a measurement mode; inspect applies both attributes.
    const QList<AccessibilityBoost> boosts = boostsFor(options.axBoost);
    const std::optional<AccessibilityBoost> boost = boosts.isEmpty()
        ? std::nullopt
        : std::optional<AccessibilityBoost>(boosts.size() == 1 ? boosts.first() : AccessibilityBoost::Both);

    auto* overlay = new HighlightOverlay;
    auto* timer = new QTimer(overlay);
    InspectState state(options.level);
    auto refresh = [&locator, &state, overlay, options, boost, boostedProcesses = QSet<qint64>(),
                      lastKey = QString()]() mutable {
        if (stopRequested()) {
            QApplication::quit();
            return;
        }
        if (state.busy()) return;
        LocateOptions locateOptions;
        // Query the innermost area first so moving to a less deeply nested
        // target cannot strand inspect at an invalid outer level.
        LocateResult result = locator->locateAt(QCursor::pos(), locateOptions);
        if (boost && result.processId != 0 && !boostedProcesses.contains(result.processId)) {
            boostedProcesses.insert(result.processId);
            const QJsonObject request = locator->requestFullAccessibilityTree(result.processId, *boost);
            out() << "accessibility boost for pid " << result.processId << ": "
                  << QJsonDocument(request).toJson(QJsonDocument::Compact) << '\n';
            out().flush();
            QThread::msleep(static_cast<unsigned long>(options.axWaitMs));
            result = locator->locateAt(QCursor::pos(), locateOptions);
        }
        state.updateLevels(result.area ? result.area->snapshot().availableLevels : 0);
        if (result.area && state.level() != 0) {
            locateOptions.level = state.level();
            result = locator->locateAt(QCursor::pos(), locateOptions);
        }
        // Print only when the area itself changes. Position changes go to
        // the overlay label: printing them into the terminal under the
        // cursor would scroll it and loop forever.
        QString key;
        QString line;
        if (result.area) {
            const ScrollAreaSnapshot snapshot = result.area->snapshot();
            line = describe(snapshot);
            overlay->showFor(snapshot.bounds, line);
            const QRect& b = snapshot.bounds;
            key = QStringLiteral("%1|%2|%3|%4,%5,%6,%7")
                      .arg(snapshot.processId).arg(snapshot.role).arg(snapshot.level)
                      .arg(b.x()).arg(b.y()).arg(b.width()).arg(b.height());
        } else {
            overlay->hide();
            line = QStringLiteral("%1: %2").arg(QLatin1String(locateStatusName(result.status)), result.detail);
            key = line;
        }
        if (key != lastKey) {
            out() << line << '\n';
            out().flush();
            lastKey = key;
        }
    };
    QObject::connect(timer, &QTimer::timeout, overlay, [&] { refresh(); });

    // Do not initialize the product's hotkeys/settings in this developer tool.
    // Owner destruction unregisters every shortcut, including partial failure.
    auto* measureTimer = new QTimer(overlay);
    QElapsedTimer pending;
    QObject::connect(measureTimer, &QTimer::timeout, overlay, [&] {
        constexpr int kModifierReleaseTimeoutMs = 3000;
        if (stopRequested()) {
            measureTimer->stop();
            state.endMeasurement();
            err() << "measurement cancelled: stop requested\n";
            err().flush();
            return;
        }
        const auto modifiers = SnapTray::currentPhysicalKeyboardModifiers();
        if (pending.elapsed() >= kModifierReleaseTimeoutMs) {
            measureTimer->stop();
            state.endMeasurement();
            QStringList held;
            if (modifiers.testFlag(Qt::ControlModifier)) held << QStringLiteral("Control");
            if (modifiers.testFlag(Qt::AltModifier)) held << QStringLiteral("Alt");
            if (modifiers.testFlag(Qt::ShiftModifier)) held << QStringLiteral("Shift");
            if (modifiers.testFlag(Qt::MetaModifier)) held << QStringLiteral("Meta");
            err() << "measurement cancelled: modifier release timed out after " << kModifierReleaseTimeoutMs
                  << " ms; reported modifiers: " << (held.isEmpty() ? QStringLiteral("none") : held.join('+')) << '\n';
            err().flush();
            return;
        }
        if (modifiers != Qt::NoModifier) return;
        measureTimer->stop();
        timer->stop();
        overlay->hide();
        auto measurement = options;
        measurement.measure = true;
        measurement.level = state.level();
        measurement.delaySeconds = 0;
        measurement.repeats = 1;
        // Inspect owns any accessibility boost until it exits.
        measurement.axBoost.clear();
        const int result = runMeasure(measurement);
        err() << "inspect measurement exit code: " << result << '\n';
        err().flush();
        state.endMeasurement();
        timer->start(kInspectIntervalMs);
    });
    const auto registerShortcut = [&](const QString& sequence, std::function<void()> callback) {
        if (SnapTray::HotkeyManager::registerSessionShortcut(sequence, overlay, std::move(callback))) return true;
        err() << "inspect shortcut unavailable: " << sequence
              << "; invalid/unsupported shortcut or OS registration failure (possibly a conflict)\n";
        err().flush();
        return false;
    };
    const auto ignored = [&](const char* action) {
        if (const char* reason = state.ignoredReason()) {
            err() << action << " ignored: " << reason << '\n';
            err().flush();
            return true;
        }
        return false;
    };
    const auto cycle = [&](int direction) {
        refresh();
        if (ignored("level change")) return;
        state.cycle(direction);
        refresh();
    };
    // A conflicting optional binding must not disable passive inspection.
    // Keep a working global stop shortcut mandatory for interactive measurement.
    if (!registerShortcut(QStringLiteral("Ctrl+Alt+Shift+Q"), [] { requestStop(0); })) {
        delete overlay;
        locator->restoreAccessibility();
        return 1;
    }
    const bool upRegistered = registerShortcut(QStringLiteral("Ctrl+Alt+Shift+Up"), [&] { cycle(1); });
    const bool downRegistered = registerShortcut(QStringLiteral("Ctrl+Alt+Shift+Down"), [&] { cycle(-1); });
    const bool measureRegistered = registerShortcut(QStringLiteral("Ctrl+Alt+Shift+M"), [&] {
        refresh();
        if (ignored("measure") || !state.beginMeasurement()) return;
        pending.start();
        measureTimer->start(kCountdownPollMs);
    });
    err() << "Hover over a scroll area. ";
    if (upRegistered) err() << "Ctrl+Alt+Shift+Up: outer; ";
    if (downRegistered) err() << "Ctrl+Alt+Shift+Down: inner; ";
    if (measureRegistered) err() << "Ctrl+Alt+Shift+M: measure once after releasing modifiers; ";
    err() << "Ctrl+Alt+Shift+Q or Ctrl+C: quit.\n";
    err().flush();
    timer->start(kInspectIntervalMs);
    const int code = QApplication::exec();
    delete overlay;
    locator->restoreAccessibility();
    return code;
}

std::optional<ProbeOptions> parseOptions(const QApplication& app, QCommandLineParser& parser)
{
    QStringList methodNames;
    for (ScrollMethod method : ScrollInjector::create()->supportedMethods()) {
        methodNames << QLatin1String(scrollMethodName(method));
    }
    const QCommandLineOption point(QStringLiteral("point"),
        QStringLiteral("Measure at global logical coordinates instead of the cursor (measure only)."),
        QStringLiteral("x,y"));
    const QCommandLineOption level(QStringLiteral("level"), QStringLiteral("Nested scroll area to use; 0 is innermost."),
                                   QStringLiteral("n"), QStringLiteral("0"));
    const QCommandLineOption delay(QStringLiteral("delay"), QStringLiteral("Seconds before measuring."),
                                   QStringLiteral("seconds"), QString::number(kDefaultDelaySeconds));
    const QCommandLineOption attempts(QStringLiteral("attempts"), QStringLiteral("Accessibility queries before giving up."),
                                      QStringLiteral("n"), QString::number(kDefaultAttempts));
    const QCommandLineOption axBoost(QStringLiteral("ax-boost"),
                                     QStringLiteral("macOS: ask the target to build its full accessibility tree "
                                                    "(manual, enhanced, both; measure also takes all) and locate "
                                                    "again. Restored on exit."),
                                     QStringLiteral("mode"));
    const QCommandLineOption enableAx(QStringLiteral("enable-ax"), QStringLiteral("Same as --ax-boost all."));
    const QCommandLineOption axWait(QStringLiteral("ax-wait"),
                                    QStringLiteral("Milliseconds to wait after an accessibility boost."),
                                    QStringLiteral("ms"), QString::number(kDefaultAxWaitMs));
    const QCommandLineOption scroll(QStringLiteral("scroll"),
                                    QStringLiteral("Also measure scrolling with this method: %1.").arg(methodNames.join(QStringLiteral(", "))),
                                    QStringLiteral("method"));
    const QCommandLineOption amount(QStringLiteral("amount"),
                                    QStringLiteral("Pixels (macOS), wheel units (wheel-delta) or increments; positive scrolls down."),
                                    QStringLiteral("n"), QString::number(kDefaultAmount));
    const QCommandLineOption percent(QStringLiteral("percent"), QStringLiteral("Target for pattern-set-percent."),
                                     QStringLiteral("p"), QStringLiteral("0"));
    const QCommandLineOption repeats(QStringLiteral("repeats"), QStringLiteral("Scrolls to measure."),
                                     QStringLiteral("n"), QString::number(kDefaultRepeats));
    const QCommandLineOption postToProcess(QStringLiteral("post-to-process"),
                                           QStringLiteral("macOS: deliver with CGEventPostToPid instead of moving the cursor."));
    const QCommandLineOption timeout(QStringLiteral("timeout"), QStringLiteral("Settle timeout in ms."),
                                     QStringLiteral("ms"), QString::number(kDefaultTimeoutMs));
    const QCommandLineOption strictPairs(QStringLiteral("strict-pairs"), QStringLiteral("Identical frame pairs for a strict settle."),
                                         QStringLiteral("n"), QStringLiteral("1"));
    const QCommandLineOption searchRadius(QStringLiteral("search-radius"), QStringLiteral("Pixels around the reported shift to search."),
                                          QStringLiteral("px"), QString::number(kDefaultSearchRadius));
    const QCommandLineOption interval(QStringLiteral("interval"), QStringLiteral("Pause between scrolls in ms."),
                                      QStringLiteral("ms"), QString::number(kDefaultRepeatIntervalMs));
    const QCommandLineOption output(QStringLiteral("output"), QStringLiteral("Append JSON lines to this file."),
                                    QStringLiteral("file"));
    const QCommandLineOption framesDirectory(QStringLiteral("frames-dir"),
        QStringLiteral("Save lossless before/after frame pairs for diagnosis (after settle timing)."), QStringLiteral("directory"));
    const QCommandLineOption contentOutput(QStringLiteral("content-output"),
        QStringLiteral("Opt-in: export a common, exactly aligned scrolling content rectangle as PNG (downward only)."),
        QStringLiteral("png"));
    const QCommandLineOption appLabel(QStringLiteral("app-label"), QStringLiteral("Name of the case, e.g. \"Chrome sticky header\"."),
                                      QStringLiteral("text"));
    const QCommandLineOption note(QStringLiteral("note"), QStringLiteral("Free-form note for the record."), QStringLiteral("text"));

    parser.setApplicationDescription(QStringLiteral("Probe system-reported scroll areas and programmatic scrolling (#265)."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("inspect (default) or measure"));
    parser.addOptions({point, level, delay, attempts, axBoost, enableAx, axWait, scroll, amount, percent, repeats, postToProcess, timeout,
                       strictPairs, searchRadius, interval, output, framesDirectory, contentOutput, appLabel, note});
    parser.process(app);

    ProbeOptions options;
    const QStringList positional = parser.positionalArguments();
    const QString command = positional.value(0, QStringLiteral("inspect"));
    if (positional.size() > 1 || (command != QLatin1String("inspect") && command != QLatin1String("measure"))) {
        return std::nullopt;
    }
    options.measure = command == QLatin1String("measure");
    if (parser.isSet(point)) {
        const QStringList coordinates = parser.value(point).split(QLatin1Char(','));
        bool xOk = false;
        bool yOk = false;
        const int x = coordinates.value(0).toInt(&xOk);
        const int y = coordinates.value(1).toInt(&yOk);
        if (!options.measure || coordinates.size() != 2 || !xOk || !yOk) {
            err() << "--point requires measure and two integer coordinates: x,y\n";
            return std::nullopt;
        }
        options.point = QPoint(x, y);
    }
    options.level = std::max(0, parser.value(level).toInt());
    options.delaySeconds = std::max(0, parser.value(delay).toInt());
    options.attempts = std::max(1, parser.value(attempts).toInt());
    options.axBoost = parser.isSet(axBoost) ? parser.value(axBoost)
                    : parser.isSet(enableAx) ? kAxBoostAll : QString();
    if (!options.axBoost.isEmpty() && boostsFor(options.axBoost).isEmpty()) {
        err() << "unsupported --ax-boost mode; use manual, enhanced, both or all\n";
        return std::nullopt;
    }
    options.axWaitMs = std::max(0, parser.value(axWait).toInt());
    options.amount = parser.value(amount).toInt();
    options.percent = parser.value(percent).toDouble();
    options.repeats = std::max(1, parser.value(repeats).toInt());
    options.postToProcess = parser.isSet(postToProcess);
    options.timeoutMs = std::max(1, parser.value(timeout).toInt());
    options.strictPairs = std::max(1, parser.value(strictPairs).toInt());
    options.searchRadius = std::max(0, parser.value(searchRadius).toInt());
    options.repeatIntervalMs = std::max(0, parser.value(interval).toInt());
    options.output = parser.value(output);
    options.framesDirectory = parser.value(framesDirectory);
    options.contentOutput = parser.value(contentOutput);
    options.appLabel = parser.value(appLabel);
    options.note = parser.value(note);
    if (parser.isSet(scroll)) {
        options.method = scrollMethodFromName(parser.value(scroll));
        if (!options.method || !methodNames.contains(parser.value(scroll))) {
            err() << "unsupported --scroll method; available: " << methodNames.join(QStringLiteral(", ")) << '\n';
            return std::nullopt;
        }
    }
    if (!options.contentOutput.isEmpty() && (!options.measure || !options.method)) {
        err() << "--content-output requires measure and --scroll\n";
        return std::nullopt;
    }
    if (!options.contentOutput.isEmpty()) {
        if (options.method != ScrollMethod::PatternSetPercent && options.amount <= 0) {
            err() << "--content-output requires a positive --amount (downward scrolling)\n";
            return std::nullopt;
        }
        const QString pathError = contentOutputPathError(options.contentOutput);
        if (!pathError.isEmpty()) {
            err() << "invalid --content-output: " << pathError << '\n';
            return std::nullopt;
        }
    }
    return options;
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    std::signal(SIGINT, requestStop);
    std::signal(SIGTERM, requestStop);
    QCommandLineParser parser;
    const std::optional<ProbeOptions> options = parseOptions(app, parser);
    if (!options) {
        parser.showHelp(2);
    }
    return options->measure ? runMeasure(*options) : runInspect(*options);
}
