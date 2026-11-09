#include "recording/CursorEffects.h"

#include <QJsonArray>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace SnapTray {

namespace {

struct SmoothingLevel {
    CursorEffectSettings::Smoothing smoothing;
    const char* name;
    SpringParameters spring;
};

// Critically damped: friction = 2 * sqrt(tension * mass).
constexpr SmoothingLevel kSmoothing[] = {
    {CursorEffectSettings::Smoothing::Off, "off", {2500.0, 100.0, 1.0}},
    {CursorEffectSettings::Smoothing::Low, "low", {2500.0, 100.0, 1.0}},
    {CursorEffectSettings::Smoothing::Medium, "medium", {900.0, 60.0, 1.0}},
    {CursorEffectSettings::Smoothing::High, "high", {400.0, 40.0, 1.0}},
};
constexpr SpringParameters kClickSpring{3600.0, 120.0, 1.0};
constexpr SpringParameters kDragSpring{6400.0, 160.0, 1.0};
constexpr double kSpringStepMs = 1000.0 / 120.0;
// The recorder polls every 16 ms but omits unchanged samples. Interpolate
// at most one poll before a change; longer gaps represent held positions.
constexpr double kInputInterpolationMs = 16.0;
constexpr double kLeadTransitionMs = 50.0;
constexpr qint64 kSpringSettleMs = 1000;
constexpr qint64 kJitterWindowMs = 100;
constexpr double kJitterDistanceSquared = 4.0;
constexpr double kCoalesceDistanceSquared = 0.25;

struct MotionBlurLevel {
    CursorEffectSettings::MotionBlur blur;
    const char* name;
    double windowFraction; // of kMaxBlurWindowMs
};

constexpr MotionBlurLevel kMotionBlur[] = {
    {CursorEffectSettings::MotionBlur::Off, "off", 0.0},
    {CursorEffectSettings::MotionBlur::Low, "low", 0.33},
    {CursorEffectSettings::MotionBlur::Medium, "medium", 0.66},
    {CursorEffectSettings::MotionBlur::High, "high", 1.0},
};

// The oldest ghost image is drawn this opaque; nearer ones more.
constexpr double kBlurGhostAlpha = 0.5;
// Ghosts closer than this to the pointer (output pixels) add nothing.
constexpr double kBlurMinDistancePx = 1.0;

// Ripple look, in logical pixels of the recorded screen.
constexpr double kRippleStartRadius = 6.0;
constexpr double kRippleEndRadius = 28.0;
constexpr double kRippleRingWidth = 3.0;
constexpr int kRippleFillAlpha = 70;
constexpr int kRippleRingAlpha = 220;

// The generic pointer for timelines without shapes, in logical pixels.
constexpr double kDefaultArrowHeight = 22.0;

QPainterPath defaultArrow()
{
    // Tip at (0, 0): the hotspot. Unit height, scaled when drawn.
    QPainterPath path;
    path.moveTo(0.0, 0.0);
    path.lineTo(0.0, 0.80);
    path.lineTo(0.20, 0.62);
    path.lineTo(0.33, 0.92);
    path.lineTo(0.44, 0.87);
    path.lineTo(0.31, 0.58);
    path.lineTo(0.56, 0.58);
    path.closeSubpath();
    return path;
}

} // namespace

bool CursorEffectSettings::operator==(const CursorEffectSettings& o) const
{
    return showCursor == o.showCursor && qFuzzyCompare(scale, o.scale) && smoothing == o.smoothing
        && clickRipple == o.clickRipple && idleHideMs == o.idleHideMs && motionBlur == o.motionBlur
        && stillEndMs == o.stillEndMs && hiddenRanges == o.hiddenRanges;
}

QJsonObject CursorEffectSettings::toJson() const
{
    const char* smoothingName = "off";
    for (const auto& entry : kSmoothing) {
        if (entry.smoothing == smoothing) smoothingName = entry.name;
    }
    const char* blurName = "off";
    for (const auto& entry : kMotionBlur) {
        if (entry.blur == motionBlur) blurName = entry.name;
    }
    QJsonArray ranges;
    for (const auto& range : hiddenRanges) ranges.append(QJsonArray{double(range.startMs), double(range.endMs)});
    QJsonObject json{{QStringLiteral("showCursor"), showCursor},
                     {QStringLiteral("scale"), scale},
                     {QStringLiteral("smoothing"), QLatin1String(smoothingName)},
                     {QStringLiteral("clickRipple"), clickRipple},
                     {QStringLiteral("idleHideMs"), double(idleHideMs)},
                     {QStringLiteral("motionBlur"), QLatin1String(blurName)},
                     {QStringLiteral("stillEndMs"), double(stillEndMs)}};
    if (!ranges.isEmpty()) json.insert(QStringLiteral("hiddenRanges"), ranges);
    return json;
}

CursorEffectSettings CursorEffectSettings::preferences() const
{
    CursorEffectSettings copy = *this;
    copy.hiddenRanges.clear();
    return copy;
}

bool CursorEffectSettings::hiddenAt(qint64 sourceMs) const
{
    auto it = std::upper_bound(hiddenRanges.begin(), hiddenRanges.end(), sourceMs,
                               [](qint64 t, const CursorHiddenRange& r) { return t < r.startMs; });
    return it != hiddenRanges.begin() && (it - 1)->contains(sourceMs);
}

std::vector<CursorHiddenRange> CursorEffectSettings::normalizedRanges(std::vector<CursorHiddenRange> ranges)
{
    ranges.erase(std::remove_if(ranges.begin(), ranges.end(),
                                [](const CursorHiddenRange& r) {
                                    return r.startMs < 0 || r.endMs - r.startMs < kMinHiddenRangeMs;
                                }),
                 ranges.end());
    std::sort(ranges.begin(), ranges.end(),
              [](const CursorHiddenRange& a, const CursorHiddenRange& b) { return a.startMs < b.startMs; });
    std::vector<CursorHiddenRange> merged;
    for (const auto& range : ranges) {
        if (!merged.empty() && range.startMs <= merged.back().endMs) {
            merged.back().endMs = std::max(merged.back().endMs, range.endMs);
        } else {
            merged.push_back(range);
        }
    }
    return merged;
}

CursorEffectSettings CursorEffectSettings::fromJson(const QJsonObject& json)
{
    CursorEffectSettings settings;
    settings.showCursor = json.value(QStringLiteral("showCursor")).toBool(settings.showCursor);
    settings.scale = std::clamp(json.value(QStringLiteral("scale")).toDouble(settings.scale), kMinScale, kMaxScale);
    const QString smoothing = json.value(QStringLiteral("smoothing")).toString();
    for (const auto& entry : kSmoothing) {
        if (smoothing == QLatin1String(entry.name)) settings.smoothing = entry.smoothing;
    }
    settings.clickRipple = json.value(QStringLiteral("clickRipple")).toBool(settings.clickRipple);
    settings.idleHideMs = std::clamp(qint64(json.value(QStringLiteral("idleHideMs")).toDouble(0.0)), qint64(0),
                                     kMaxIdleHideMs);
    const QString blur = json.value(QStringLiteral("motionBlur")).toString();
    for (const auto& entry : kMotionBlur) {
        if (blur == QLatin1String(entry.name)) settings.motionBlur = entry.blur;
    }
    settings.stillEndMs = std::clamp(qint64(json.value(QStringLiteral("stillEndMs")).toDouble(0.0)), qint64(0),
                                     kMaxStillEndMs);
    std::vector<CursorHiddenRange> ranges;
    for (const auto& value : json.value(QStringLiteral("hiddenRanges")).toArray()) {
        const QJsonArray pair = value.toArray();
        if (pair.size() == 2) ranges.push_back({qint64(pair.at(0).toDouble(-1)), qint64(pair.at(1).toDouble(-1))});
    }
    settings.hiddenRanges = normalizedRanges(std::move(ranges));
    return settings;
}

CursorEffectAvailability CursorEffectAvailability::of(const CursorTimeline* timeline)
{
    CursorEffectAvailability availability;
    if (!timeline || timeline->samples.empty() || timeline->info.cursorInVideo) {
        // No data, or the pointer is already in the picture: drawing another
        // one would double it.
        return availability;
    }
    availability.cursor = true;
    availability.clicks = timeline->info.clicksAvailable;
    return availability;
}

QPointF CursorFrameMapping::toOutput(const QPointF& sourcePoint) const
{
    const QRectF crop = sourceCrop.isEmpty() ? QRectF(QPointF(0, 0), QSizeF(sourceSize)) : sourceCrop;
    if (crop.isEmpty() || outputSize.isEmpty()) return {};
    return QPointF((sourcePoint.x() - crop.x()) * outputSize.width() / crop.width(),
                   (sourcePoint.y() - crop.y()) * outputSize.height() / crop.height());
}

double CursorFrameMapping::scale() const
{
    const QRectF crop = sourceCrop.isEmpty() ? QRectF(QPointF(0, 0), QSizeF(sourceSize)) : sourceCrop;
    return crop.isEmpty() ? 1.0 : double(outputSize.width()) / crop.width();
}

CursorEffectRenderer::CursorEffectRenderer(const CursorTimeline& timeline, const CursorEffectSettings& settings,
                                           qint64 clipEndMs)
    : m_timeline(timeline)
    , m_settings(settings)
    , m_clipEndMs(clipEndMs)
{
    rebuildSpringPaths();
}

void CursorEffectRenderer::setTimeline(const TimelineMap& timeline)
{
    if (m_kept == timeline.segments()) return;
    m_kept = timeline.segments();
    m_map = timeline;
    rebuildSpringPaths();
}

qint64 CursorEffectRenderer::partStart(qint64 ms) const
{
    if (m_kept.empty()) return std::numeric_limits<qint64>::min() / 2;
    for (const auto& segment : m_kept) {
        if (segment.contains(ms)) return segment.startMs;
    }
    return ms;
}

qint64 CursorEffectRenderer::partEnd(qint64 ms) const
{
    if (m_kept.empty()) return std::numeric_limits<qint64>::max() / 2;
    for (const auto& segment : m_kept) {
        if (segment.contains(ms)) return segment.endMs;
    }
    return ms;
}

QPointF CursorEffectRenderer::rawPosition(qint64 sourceMs) const
{
    const CursorSample* sample = m_timeline.sampleAt(sourceMs);
    return sample ? QPointF(sample->position) : QPointF();
}

void CursorEffectRenderer::rebuildSpringPaths()
{
    m_paths.clear();
    if (m_settings.smoothing == CursorEffectSettings::Smoothing::Off || m_timeline.samples.empty()) return;
    SpringParameters normal = kSmoothing[1].spring;
    for (const auto& level : kSmoothing)
        if (level.smoothing == m_settings.smoothing) normal = level.spring;
    const auto& samples = m_timeline.samples;
    const auto& buttons = m_timeline.buttons;
    qint64 last = samples.back().tMs;
    if (!buttons.empty()) last = std::max(last, buttons.back().tMs);
    const auto parts = m_kept.empty()
        ? std::vector<TimelineSegment>{{samples.front().tMs, last + kSpringSettleMs, 1.0}}
        : m_kept;
    for (const auto& part : parts) {
        const qint64 start = std::max(part.startMs, samples.front().tMs);
        const qint64 end = std::min(part.endMs, last + kSpringSettleMs);
        if (end <= start) continue;
        SpringPath input;
        input.knots.push_back({double(start), rawPosition(start)});
        auto sample = std::upper_bound(samples.begin(), samples.end(), start,
            [](qint64 t, const CursorSample& s) { return t < s.tMs; });
        bool lastSemantic = true;
        for (; sample != samples.end() && sample->tMs < part.endMs; ++sample) {
            const auto& previous = *(sample - 1);
            const bool semantic = sample->shapeId != previous.shapeId || sample->visible != previous.visible;
            const QPointF delta = QPointF(sample->position) - input.knots.back().position;
            const bool interior = sample + 1 != samples.end() && (sample + 1)->tMs < part.endMs;
            if (interior && !semantic && (sample + 1)->shapeId == sample->shapeId
                && (sample + 1)->visible == sample->visible) {
                const QPointF back = QPointF((sample + 1)->position) - QPointF(sample->position);
                if ((sample + 1)->tMs - previous.tMs <= kJitterWindowMs
                    && QPointF::dotProduct(delta, delta) <= kJitterDistanceSquared
                    && QPointF::dotProduct(back, back) <= kJitterDistanceSquared
                    && QPointF::dotProduct(delta, back) < 0.0) continue;
                if (sample->tMs - input.knots.back().ms < kJitterWindowMs
                    && QPointF::dotProduct(delta, delta) < kCoalesceDistanceSquared) continue;
            }
            // Keep first/last and shape/visibility boundaries. Coalesce dense
            // interior samples into the latest sample in their 120 Hz cell.
            if (interior && !semantic && !lastSemantic && input.knots.size() > 1
                && std::floor((sample->tMs - start) / kSpringStepMs)
                   == std::floor((input.knots.back().ms - start) / kSpringStepMs)) {
                input.knots.back() = {double(sample->tMs), QPointF(sample->position)};
            } else {
                input.knots.push_back({double(sample->tMs), QPointF(sample->position)});
            }
            lastSemantic = semantic;
        }
        auto event = std::lower_bound(buttons.begin(), buttons.end(), start,
            [](const CursorButtonEvent& e, qint64 t) { return e.tMs < t; });
        const auto eventEnd = std::lower_bound(event, buttons.end(), part.endMs,
            [](const CursorButtonEvent& e, qint64 t) { return e.tMs < t; });
        // Event positions are authoritative, and lead must stop at the next
        // event so a drag cannot run beyond its release then snap backwards.
        for (auto e = event; e != eventEnd; ++e)
            input.knots.push_back({double(e->tMs), QPointF(e->position)});
        std::stable_sort(input.knots.begin(), input.knots.end(),
            [](const SpringPath::Knot& a, const SpringPath::Knot& b) { return a.ms < b.ms; });
        std::vector<SpringPath::Knot> unique;
        unique.reserve(input.knots.size());
        for (const auto& knot : input.knots) {
            if (!unique.empty() && unique.back().ms == knot.ms) unique.back() = knot;
            else unique.push_back(knot);
        }
        input.knots.clear();
        input.knots.reserve(unique.size() * 2);
        for (const auto& knot : unique) {
            if (!input.knots.empty() && knot.ms - input.knots.back().ms > kInputInterpolationMs) {
                const QPointF heldPosition = input.knots.back().position;
                input.knots.push_back({knot.ms - kInputInterpolationMs, heldPosition});
            }
            input.knots.push_back(knot);
        }
        std::vector<const CursorButtonEvent*> presses;
        for (auto e = event; e != eventEnd; ++e) if (e->down) presses.push_back(&*e);
        size_t nextPress = 0;
        // Only held state is carried into a part, never spring momentum.
        Qt::MouseButtons held = m_timeline.buttonsDownAt(start);
        QPointF position = rawPosition(start), velocity;
        double time = double(start), leadMs = normal.leadSeconds() * 1000.0;
        PartPath result{part.startMs, part.endMs, {}};
        result.path.knots.reserve(size_t((end - start) / kSpringStepMs) + 2);
        result.path.knots.push_back({time, position});
        qint64 step = 1;
        while (time < end) {
            while (event != eventEnd && event->tMs <= time) {
                position = QPointF(event->position);
                velocity = {};
                if (event->down) held |= event->button; else held &= ~Qt::MouseButtons(event->button);
                result.path.knots.back().position = position;
                ++event;
            }
            while (nextPress < presses.size() && presses[nextPress]->tMs <= time) ++nextPress;
            const bool anticipate = !held && nextPress < presses.size()
                && presses[nextPress]->tMs - time <= kClickLookaheadMs;
            const auto parameters = held ? kDragSpring : (anticipate ? kClickSpring : normal);
            const double grid = start + step * kSpringStepMs;
            double next = std::min(double(end), grid);
            if (event != eventEnd) next = std::min(next, double(event->tMs));
            const double dt = next - time;
            const double nextLead = leadMs + (parameters.leadSeconds() * 1000.0 - leadMs)
                * (1.0 - std::exp(-dt / kLeadTransitionMs));
            const auto target = [&](double t, double lead) {
                if (anticipate) return QPointF(presses[nextPress]->position);
                const double targetTime = event != eventEnd ? std::min(t + lead, double(event->tMs)) : t + lead;
                return input.at(targetTime);
            };
            advanceSpring(position, velocity, target(time, leadMs), target(next, nextLead), dt / 1000.0, parameters);
            time = next;
            leadMs = nextLead;
            if (time >= grid) ++step;
            result.path.knots.push_back({time, position});
        }
        m_paths.push_back(std::move(result));
    }
}

QPointF CursorEffectRenderer::springPosition(qint64 sourceMs) const
{
    auto it = std::upper_bound(m_paths.begin(), m_paths.end(), sourceMs,
        [](qint64 t, const PartPath& p) { return t < p.startMs; });
    if (it == m_paths.begin()) return rawPosition(sourceMs);
    --it;
    if (!m_kept.empty() && sourceMs >= it->endMs) return rawPosition(sourceMs);
    return it->path.at(double(sourceMs));
}

qint64 CursorEffectRenderer::distanceToButtonEvent(qint64 sourceMs) const
{
    const auto& buttons = m_timeline.buttons;
    auto it = std::lower_bound(buttons.begin(), buttons.end(), sourceMs,
                               [](const CursorButtonEvent& e, qint64 t) { return e.tMs < t; });
    qint64 distance = std::numeric_limits<qint64>::max();
    // Only events in the shown part count.
    if (it != buttons.end() && it->tMs < partEnd(sourceMs)) distance = it->tMs - sourceMs;
    if (it != buttons.begin() && (it - 1)->tMs >= partStart(sourceMs)) {
        distance = std::min(distance, sourceMs - (it - 1)->tMs);
    }
    return distance;
}

qint64 CursorEffectRenderer::effectiveTime(qint64 sourceMs) const
{
    if (m_settings.stillEndMs <= 0 || m_timeline.samples.empty()) return sourceMs;
    const qint64 firstSample = m_timeline.samples.front().tMs;
    if (!m_map.isEmpty()) {
        // With parts cut out, "the end" is the exported end: the hold starts
        // that long before it in output time, at a source time that is kept,
        // never in a part that was cut (PR #199 review).
        const auto outputMs = m_map.sourceToOutput(sourceMs);
        if (!outputMs) return sourceMs; // cut out: never drawn
        const qint64 holdFrom = std::max<qint64>(0, m_map.outputDurationMs() - m_settings.stillEndMs);
        if (*outputMs < holdFrom) return sourceMs;
        return std::max(m_map.outputToSource(holdFrom), firstSample);
    }
    if (m_clipEndMs < 0) return sourceMs;
    // A clip shorter than the still interval (Still End is remembered across
    // recordings) holds the pointer from its first sample, never from a time
    // before the timeline, where there is no pointer at all.
    const qint64 freezeAt = std::max(m_clipEndMs - m_settings.stillEndMs, firstSample);
    return std::min(sourceMs, freezeAt);
}

std::optional<QPointF> CursorEffectRenderer::pointerPosition(qint64 sourceMs) const
{
    if (m_settings.hiddenAt(sourceMs)) return std::nullopt;
    return positionAt(effectiveTime(sourceMs));
}

qint64 CursorEffectRenderer::idleBefore(qint64 ms, qint64 limit) const
{
    // A cut is a fresh start: the pointer shows from the start of its part.
    qint64 idle = std::min(limit, ms - partStart(ms));
    const auto& samples = m_timeline.samples;
    auto it = std::upper_bound(samples.begin(), samples.end(), ms,
                               [](qint64 t, const CursorSample& s) { return t < s.tMs; });
    // Walk back over the samples that can still matter: bounded by `limit`
    // and by the start of the shown part.
    const qint64 from = partStart(ms);
    while (it != samples.begin()) {
        --it;
        if (ms - it->tMs >= limit || it->tMs < from) break;
        if (it == samples.begin() || it->position != (it - 1)->position) {
            idle = std::min(idle, ms - it->tMs); // appeared or moved here
            break;
        }
    }
    const auto& buttons = m_timeline.buttons;
    auto b = std::upper_bound(buttons.begin(), buttons.end(), ms,
                              [](qint64 t, const CursorButtonEvent& e) { return t < e.tMs; });
    if (b != buttons.begin()) idle = std::min(idle, ms - (b - 1)->tMs);
    return std::max<qint64>(0, idle);
}

qint64 CursorEffectRenderer::idleAfter(qint64 ms, qint64 limit) const
{
    // Nothing after the end of this part is shown next.
    limit = std::max<qint64>(0, std::min(limit, partEnd(ms) - ms));
    qint64 idle = limit;
    const auto& samples = m_timeline.samples;
    auto it = std::upper_bound(samples.begin(), samples.end(), ms,
                               [](qint64 t, const CursorSample& s) { return t < s.tMs; });
    for (; it != samples.end() && it->tMs - ms < limit; ++it) {
        if (it != samples.begin() && it->position != (it - 1)->position) {
            idle = it->tMs - ms;
            break;
        }
    }
    const auto& buttons = m_timeline.buttons;
    auto b = std::upper_bound(buttons.begin(), buttons.end(), ms,
                              [](qint64 t, const CursorButtonEvent& e) { return t < e.tMs; });
    if (b != buttons.end() && b->tMs - ms < limit) idle = std::min(idle, b->tMs - ms);
    return std::max<qint64>(0, idle);
}

double CursorEffectRenderer::pointerOpacity(qint64 sourceMs) const
{
    const qint64 hideAfter = m_settings.idleHideMs;
    if (hideAfter <= 0) return 1.0;
    const qint64 frozenAt = effectiveTime(sourceMs);
    // Once the pointer is held still for the clip end, movement after that
    // point is not shown, so it neither wakes the pointer nor keeps it awake.
    const qint64 sinceActivity = idleBefore(frozenAt, hideAfter + kIdleFadeMs) + (sourceMs - frozenAt);
    qint64 untilActivity = frozenAt < sourceMs ? kIdleFadeMs : idleAfter(sourceMs, kIdleFadeMs);
    if (sourceMs + untilActivity > effectiveTime(sourceMs + untilActivity)) untilActivity = kIdleFadeMs; // not shown
    const double fadingOut = std::clamp(double(sinceActivity - hideAfter) / kIdleFadeMs, 0.0, 1.0);
    const double fadingIn = 1.0 - std::clamp(double(untilActivity) / kIdleFadeMs, 0.0, 1.0);
    return std::max(1.0 - fadingOut, fadingIn);
}

double CursorEffectRenderer::blurWindowMs() const
{
    for (const auto& entry : kMotionBlur) {
        if (entry.blur == m_settings.motionBlur) return entry.windowFraction * kMaxBlurWindowMs;
    }
    return 0.0;
}

std::optional<QPointF> CursorEffectRenderer::positionAt(qint64 sourceMs) const
{
    const CursorSample* sample = m_timeline.sampleAt(sourceMs);
    if (!sample || sourceMs < m_timeline.samples.front().tMs || !sample->visible) return std::nullopt;
    const QPointF raw(sample->position);
    if (m_settings.smoothing == CursorEffectSettings::Smoothing::Off) return raw;
    return springPosition(sourceMs);
}

void CursorEffectRenderer::paint(QImage& frame, qint64 sourceMs, const CursorFrameMapping& mapping) const
{
    if (!m_settings.drawsAnything()) return;
    QPainter painter(&frame);
    paint(painter, sourceMs, mapping);
}

void CursorEffectRenderer::paint(QPainter& painter, qint64 sourceMs, const CursorFrameMapping& mapping) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    // A hidden range hides the click highlights too: nothing of the pointer shows there.
    if (m_settings.hiddenAt(sourceMs)) {
        painter.restore();
        return;
    }
    if (m_settings.clickRipple && m_timeline.info.clicksAvailable) paintRipples(painter, sourceMs, mapping);
    if (m_settings.showCursor) paintPointer(painter, sourceMs, mapping);
    painter.restore();
}

void CursorEffectRenderer::paintRipples(QPainter& painter, qint64 sourceMs, const CursorFrameMapping& mapping) const
{
    const double unit = m_timeline.info.pixelRatio * mapping.scale() * m_settings.scale;
    const auto& buttons = m_timeline.buttons;
    // A click in a part that is cut out never shows, not even as a ripple
    // running on into the next part.
    const qint64 from = std::max(sourceMs - kRippleDurationMs + 1, partStart(sourceMs));
    auto it = std::lower_bound(buttons.begin(), buttons.end(), from,
                               [](const CursorButtonEvent& e, qint64 t) { return e.tMs < t; });
    for (; it != buttons.end() && it->tMs <= sourceMs; ++it) {
        if (!it->down || it->synthetic) continue;
        // Anchored to the real press: time and place, never the smoothed path.
        const double progress = double(sourceMs - it->tMs) / kRippleDurationMs;
        const double eased = 1.0 - std::pow(1.0 - progress, 3.0);
        const double radius = (kRippleStartRadius + (kRippleEndRadius - kRippleStartRadius) * eased) * unit;
        const double fade = 1.0 - progress;
        const QPointF center = mapping.toOutput(QPointF(it->position));
        painter.setPen(QPen(QColor(0, 0, 0, int(kRippleRingAlpha * fade * 0.5)), (kRippleRingWidth + 1.5) * unit));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(center, radius, radius);
        painter.setPen(QPen(QColor(255, 255, 255, int(kRippleRingAlpha * fade)), kRippleRingWidth * unit));
        painter.setBrush(QColor(255, 255, 255, int(kRippleFillAlpha * fade)));
        painter.drawEllipse(center, radius, radius);
    }
}

void CursorEffectRenderer::paintPointer(QPainter& painter, qint64 sourceMs, const CursorFrameMapping& mapping) const
{
    const auto position = pointerPosition(sourceMs);
    if (!position) return;
    const double opacity = pointerOpacity(sourceMs);
    if (opacity <= 0.0) return;
    const qint64 shownMs = effectiveTime(sourceMs);
    const QPointF at = mapping.toOutput(*position);
    const double scale = mapping.scale() * m_settings.scale;
    const CursorSample* sample = m_timeline.sampleAt(shownMs);
    const CursorShape* shape = sample ? m_timeline.shape(sample->shapeId) : nullptr;
    const double baseOpacity = painter.opacity();
    // Motion blur: fainter copies where the pointer was a moment before, at
    // fixed time offsets, so a frame never depends on what was drawn before.
    if (const double window = blurWindowMs(); window > 0.0) {
        for (int k = kBlurSamples; k >= 1; --k) {
            const qint64 ghostMs = sourceMs - qint64(std::lround(window * k / kBlurSamples));
            if (ghostMs < partStart(sourceMs)) continue; // no trail from before a cut
            const auto ghost = pointerPosition(ghostMs);
            if (!ghost) continue;
            const QPointF ghostAt = mapping.toOutput(*ghost);
            if (QLineF(ghostAt, at).length() < kBlurMinDistancePx) continue;
            const double fade = kBlurGhostAlpha * (1.0 - double(k - 1) / kBlurSamples);
            painter.setOpacity(baseOpacity * opacity * fade * pointerOpacity(ghostMs));
            drawPointerAt(painter, ghostAt, shape, scale);
        }
    }
    painter.setOpacity(baseOpacity * opacity);
    drawPointerAt(painter, at, shape, scale);
    painter.setOpacity(baseOpacity);
}

void CursorEffectRenderer::drawPointerAt(QPainter& painter, const QPointF& at, const CursorShape* shape,
                                         double scale) const
{
    if (shape) {
        // Shape images are in recorded (video) pixels; the hotspot stays on the point.
        const QRectF target(at - QPointF(shape->hotspot) * scale, QSizeF(shape->image.size()) * scale);
        painter.drawImage(target, shape->image);
        return;
    }
    // No recorded shape: a plain arrow with an outline, readable on any background.
    const double height = kDefaultArrowHeight * m_timeline.info.pixelRatio * scale;
    QPainterPath arrow = defaultArrow();
    QTransform transform;
    transform.translate(at.x(), at.y());
    transform.scale(height, height);
    arrow = transform.map(arrow);
    painter.setPen(QPen(Qt::white, std::max(1.0, height / 14.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::black);
    painter.drawPath(arrow);
}

} // namespace SnapTray
