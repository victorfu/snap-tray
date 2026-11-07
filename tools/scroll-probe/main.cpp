// Developer probe for #265: shows the scroll area the platform accessibility
// API reports under the cursor, and measures programmatic scrolling against
// lossless captures. Writes one JSON line per measurement. Not part of the
// product; it only drives the scrollcapture modules the product will use.
//
//   snaptray-scroll-probe inspect [--level N]
//   snaptray-scroll-probe measure [--level N] [--enable-ax]
//                                 [--scroll METHOD --amount N --repeats N]
//                                 [--output FILE] [--app-label TEXT] [--note TEXT]
#include "capture/ScreenSnapshot.h"
#include "platform/WindowLevel.h"
#include "scrollcapture/FrameRowHashes.h"
#include "scrollcapture/FrameSettleDetector.h"
#include "scrollcapture/RowHashShiftEstimator.h"
#include "scrollcapture/ScrollAreaLocator.h"
#include "scrollcapture/ScrollInjector.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCursor>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QScreen>
#include <QSysInfo>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

using namespace snaptray::scrollcapture;

namespace {

constexpr int kSchemaVersion = 1;
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
constexpr int kEnhancedAccessibilityWaitMs = 500;
constexpr int kOverlayHideSettleMs = 150;
constexpr int kOverlayBorder = 3;
constexpr int kOverlayLabelPadding = 4;
constexpr double kP50 = 0.5;
constexpr double kP95 = 0.95;

struct ProbeOptions {
    bool measure = false;
    int level = 0;
    int delaySeconds = kDefaultDelaySeconds;
    int attempts = kDefaultAttempts;
    bool enableAx = false;
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
    };
}

QJsonValue optionalJson(const std::optional<qint64>& value)
{
    return value ? QJsonValue(*value) : QJsonValue();
}

struct RepeatOutcome {
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
    if (start.image.isNull()) {
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

    QElapsedTimer clock;
    clock.start();
    const InjectResult inject = injector.scroll(area, request);
    json.insert(QStringLiteral("inject"), QJsonObject{{QStringLiteral("ok"), inject.ok},
                                                      {QStringLiteral("detail"), inject.detail}});
    if (!inject.ok) {
        return outcome;
    }

    qint64 captureMs = 0;
    while (detector.result().outcome == FrameSettleDetector::Outcome::Pending) {
        QElapsedTimer captureClock;
        captureClock.start();
        const Frame frame = captureArea(before.bounds);
        captureMs += captureClock.elapsed();
        if (frame.image.isNull()) {
            json.insert(QStringLiteral("error"), QStringLiteral("screen capture failed while settling"));
            return outcome;
        }
        detector.addFrame(frame.image, clock.elapsed());
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
    });

    const ScrollAreaSnapshot after = area.snapshot();
    json.insert(QStringLiteral("after"), toJson(after.position));

    const QVector<quint64> beforeRows = computeRowHashes(start.image);
    const QVector<quint64> afterRows = computeRowHashes(detector.lastFrame());
    ShiftEstimatorOptions shiftOptions;
    shiftOptions.searchRadius = options.searchRadius;
    if (before.position.offset && after.position.offset) {
        const double apiShift = (*after.position.offset - *before.position.offset) * start.scale;
        json.insert(QStringLiteral("apiShiftPx"), apiShift);
        shiftOptions.expectedShift = qRound(apiShift);
    }
    const ShiftEstimate estimate = estimateVerticalShift(beforeRows, afterRows, shiftOptions);
    json.insert(QStringLiteral("shift"), shiftToJson(estimate));
    if (shiftOptions.expectedShift) {
        // The same frames without the platform prior, to show how far the
        // reported position is from the measured one.
        ShiftEstimatorOptions unconstrained = shiftOptions;
        unconstrained.expectedShift.reset();
        json.insert(QStringLiteral("shiftWithoutPrior"),
                    shiftToJson(estimateVerticalShift(beforeRows, afterRows, unconstrained)));
    }
    if (estimate.status == ShiftEstimate::Status::Found) {
        outcome.shiftPx = estimate.shift;
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
    std::vector<int> shifts;
    std::vector<qint64> settleTimes;
    const bool towardBottom = options.method == ScrollMethod::PatternSetPercent || options.amount >= 0;
    for (int index = 0; index < options.repeats; ++index) {
        const ScrollAreaSnapshot before = area.snapshot();
        const std::optional<bool> atEnd = towardBottom ? before.position.atBottom : before.position.atTop;
        if (atEnd.value_or(false) && options.method != ScrollMethod::PatternSetPercent) {
            phase.insert(QStringLiteral("stoppedAtEnd"), index);
            break;
        }
        const RepeatOutcome outcome = runRepeat(index, area, injector, options, before);
        repeats.append(outcome.json);
        if (outcome.shiftPx) {
            shifts.push_back(*outcome.shiftPx);
        }
        if (outcome.strictSettleMs) {
            settleTimes.push_back(*outcome.strictSettleMs);
        }
        QThread::msleep(static_cast<unsigned long>(options.repeatIntervalMs));
    }
    phase.insert(QStringLiteral("repeats"), repeats);
    phase.insert(QStringLiteral("stats"), statistics(shifts, settleTimes, static_cast<int>(repeats.size())));
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
    while (clock.elapsed() < totalMs) {
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
            const LocateResult result = locator.locateAt(QCursor::pos(), locateOptions);
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
        {QStringLiteral("screen"), screenToJson(cursor)},
        {QStringLiteral("environment"), locator->environment()},
    };

    LocateOptions locateOptions;
    locateOptions.level = options.level;
    locateOptions.maxAttempts = options.attempts;
    LocateResult located = locator->locateAt(cursor, locateOptions);
    record.insert(QStringLiteral("locate"), locateToJson(located));

    if (options.enableAx && located.processId != 0) {
        QString detail;
        const bool enabled = locator->enableEnhancedAccessibility(located.processId, &detail);
        QThread::msleep(kEnhancedAccessibilityWaitMs);
        located = locator->locateAt(cursor, locateOptions);
        record.insert(QStringLiteral("enhancedAccessibility"), QJsonObject{
            {QStringLiteral("ok"), enabled},
            {QStringLiteral("detail"), detail},
            {QStringLiteral("locate"), locateToJson(located)},
        });
    }

    if (options.method) {
        if (located.area) {
            record.insert(QStringLiteral("scroll"), runScrollPhase(*located.area, *injector, options));
        } else {
            record.insert(QStringLiteral("scroll"), QJsonObject{
                {QStringLiteral("skipped"), QStringLiteral("no scroll area")}});
        }
        QCursor::setPos(cursor);
    }

    writeRecord(record, options.output);
    return 0;
}

int runInspect(const ProbeOptions& options)
{
    auto locator = ScrollAreaLocator::create();
    if (!locator->isAvailable()) {
        err() << "scroll area detection is not available on this platform\n";
        return 1;
    }
    err() << "Hover over a scroll area. Ctrl+C to quit.\n";
    err().flush();

    auto* overlay = new HighlightOverlay;
    auto* timer = new QTimer(overlay);
    QObject::connect(timer, &QTimer::timeout, overlay, [&locator, overlay, options, lastLine = QString()]() mutable {
        LocateOptions locateOptions;
        locateOptions.level = options.level;
        const LocateResult result = locator->locateAt(QCursor::pos(), locateOptions);
        QString line;
        if (result.area) {
            const ScrollAreaSnapshot snapshot = result.area->snapshot();
            const QString description = describe(snapshot);
            overlay->showFor(snapshot.bounds, description);
            line = description;
        } else {
            overlay->hide();
            line = QStringLiteral("%1: %2").arg(QLatin1String(locateStatusName(result.status)), result.detail);
        }
        if (line != lastLine) {
            out() << line << '\n';
            out().flush();
            lastLine = line;
        }
    });
    timer->start(kInspectIntervalMs);
    const int code = QApplication::exec();
    delete overlay;
    return code;
}

std::optional<ProbeOptions> parseOptions(const QApplication& app, QCommandLineParser& parser)
{
    QStringList methodNames;
    for (ScrollMethod method : ScrollInjector::create()->supportedMethods()) {
        methodNames << QLatin1String(scrollMethodName(method));
    }
    const QCommandLineOption level(QStringLiteral("level"), QStringLiteral("Nested scroll area to use; 0 is innermost."),
                                   QStringLiteral("n"), QStringLiteral("0"));
    const QCommandLineOption delay(QStringLiteral("delay"), QStringLiteral("Seconds before measuring."),
                                   QStringLiteral("seconds"), QString::number(kDefaultDelaySeconds));
    const QCommandLineOption attempts(QStringLiteral("attempts"), QStringLiteral("Accessibility queries before giving up."),
                                      QStringLiteral("n"), QString::number(kDefaultAttempts));
    const QCommandLineOption enableAx(QStringLiteral("enable-ax"),
                                      QStringLiteral("macOS: set AXManualAccessibility on the target and locate again."));
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
    const QCommandLineOption appLabel(QStringLiteral("app-label"), QStringLiteral("Name of the case, e.g. \"Chrome sticky header\"."),
                                      QStringLiteral("text"));
    const QCommandLineOption note(QStringLiteral("note"), QStringLiteral("Free-form note for the record."), QStringLiteral("text"));

    parser.setApplicationDescription(QStringLiteral("Probe system-reported scroll areas and programmatic scrolling (#265)."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("inspect (default) or measure"));
    parser.addOptions({level, delay, attempts, enableAx, scroll, amount, percent, repeats, postToProcess, timeout,
                       strictPairs, searchRadius, interval, output, appLabel, note});
    parser.process(app);

    ProbeOptions options;
    const QStringList positional = parser.positionalArguments();
    const QString command = positional.value(0, QStringLiteral("inspect"));
    if (positional.size() > 1 || (command != QLatin1String("inspect") && command != QLatin1String("measure"))) {
        return std::nullopt;
    }
    options.measure = command == QLatin1String("measure");
    options.level = std::max(0, parser.value(level).toInt());
    options.delaySeconds = std::max(0, parser.value(delay).toInt());
    options.attempts = std::max(1, parser.value(attempts).toInt());
    options.enableAx = parser.isSet(enableAx);
    options.amount = parser.value(amount).toInt();
    options.percent = parser.value(percent).toDouble();
    options.repeats = std::max(1, parser.value(repeats).toInt());
    options.postToProcess = parser.isSet(postToProcess);
    options.timeoutMs = std::max(1, parser.value(timeout).toInt());
    options.strictPairs = std::max(1, parser.value(strictPairs).toInt());
    options.searchRadius = std::max(0, parser.value(searchRadius).toInt());
    options.repeatIntervalMs = std::max(0, parser.value(interval).toInt());
    options.output = parser.value(output);
    options.appLabel = parser.value(appLabel);
    options.note = parser.value(note);
    if (parser.isSet(scroll)) {
        options.method = scrollMethodFromName(parser.value(scroll));
        if (!options.method || !methodNames.contains(parser.value(scroll))) {
            err() << "unsupported --scroll method; available: " << methodNames.join(QStringLiteral(", ")) << '\n';
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
    QCommandLineParser parser;
    const std::optional<ProbeOptions> options = parseOptions(app, parser);
    if (!options) {
        parser.showHelp(2);
    }
    return options->measure ? runMeasure(*options) : runInspect(*options);
}
