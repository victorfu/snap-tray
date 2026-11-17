#include "recording/CursorEffects.h"
#include "recording/CrispCursor.h"

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

// RDP only chooses geometry. Retain sample times by resampling the fitted
// polyline, so the spring's short interpolation windows do not turn removed
// knots into artificial pauses. Callers split at every semantic/drag boundary.
void straightenWindow(std::vector<SpringPath::Knot>& knots, size_t first, size_t last, double tolerance)
{
    if (last <= first + 1) return;
    std::vector<bool> keep(last - first + 1, false);
    keep.front() = keep.back() = true;
    std::vector<std::pair<size_t, size_t>> pending{{first, last}};
    while (!pending.empty()) {
        const auto [a, b] = pending.back();
        pending.pop_back();
        const QPointF delta = knots[b].position - knots[a].position;
        const double lengthSquared = QPointF::dotProduct(delta, delta);
        double furthest = tolerance * tolerance;
        size_t at = b;
        for (size_t i = a + 1; i < b; ++i) {
            const QPointF offset = knots[i].position - knots[a].position;
            const double fraction = lengthSquared > 0 ? std::clamp(QPointF::dotProduct(offset, delta) / lengthSquared, 0.0, 1.0) : 0;
            const QPointF error = offset - delta * fraction;
            const double distance = QPointF::dotProduct(error, error);
            if (distance > furthest) { furthest = distance; at = i; }
        }
        if (at != b) {
            keep[at - first] = true;
            pending.push_back({a, at});
            pending.push_back({at, b});
        }
    }
    size_t a = first;
    for (size_t b = first + 1; b <= last; ++b) {
        if (!keep[b - first]) continue;
        for (size_t i = a + 1; i < b; ++i) {
            const double fraction = (knots[i].ms - knots[a].ms) / (knots[b].ms - knots[a].ms);
            knots[i].position = knots[a].position + (knots[b].position - knots[a].position) * fraction;
        }
        a = b;
    }
}

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

// All styles share recorded coordinates, but their lifetimes and drawing
// functions live with their recipe names in one table.
using ClickStyle = CursorEffectSettings::ClickStyle;
using PressBounce = CursorEffectSettings::PressBounce;
constexpr qint64 kDoubleRippleDurationMs = 600;
constexpr double kSecondRippleDelay = 0.25;
constexpr int kBurstRays = 8;
constexpr double kSpotlightRadius = 48.0;
constexpr int kSpotlightDimAlpha = 45;
constexpr int kSpotlightCenterAlpha = 110;
constexpr int kSpotlightHaloAlpha = 70;
constexpr double kSpotlightHaloStop = 0.45;
constexpr double kSpotlightDimStop = 0.7;
constexpr double kBurstTravel = 22.0;
constexpr double kBurstRayLength = 10.0;
constexpr double kMinimumBounceScale = 0.5;

QColor withAlpha(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}

void drawRing(QPainter& painter, const QPointF& center, double progress, double unit, const QColor& color)
{
    // Preserve the original ring's geometry, colors and rounding exactly.
    const double eased = 1.0 - std::pow(1.0 - progress, 3.0);
    const double radius = (kRippleStartRadius + (kRippleEndRadius - kRippleStartRadius) * eased) * unit;
    const double fade = 1.0 - progress;
    painter.setPen(QPen(QColor(0, 0, 0, int(kRippleRingAlpha * fade * 0.5)), (kRippleRingWidth + 1.5) * unit));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(center, radius, radius);
    painter.setPen(QPen(withAlpha(color, int(kRippleRingAlpha * fade)), kRippleRingWidth * unit));
    painter.setBrush(withAlpha(color, int(kRippleFillAlpha * fade)));
    painter.drawEllipse(center, radius, radius);
}

void drawRipple(QPainter& painter, const QPointF& center, double progress, double unit, const QColor& color)
{
    const double first = progress / (1.0 - kSecondRippleDelay);
    if (first < 1.0) drawRing(painter, center, first, unit, color);
    if (progress >= kSecondRippleDelay)
        drawRing(painter, center, (progress - kSecondRippleDelay) / (1.0 - kSecondRippleDelay), unit, color);
}

void drawSpotlight(QPainter& painter, const QPointF& center, double progress, double unit, const QColor& color)
{
    const double fade = std::sin(progress * M_PI);
    const double radius = kSpotlightRadius * unit;
    QRadialGradient light(center, radius);
    light.setColorAt(0.0, withAlpha(color, int(kSpotlightCenterAlpha * fade)));
    light.setColorAt(kSpotlightHaloStop, withAlpha(color, int(kSpotlightHaloAlpha * fade)));
    light.setColorAt(kSpotlightDimStop, QColor(0, 0, 0, int(kSpotlightDimAlpha * fade)));
    light.setColorAt(1.0, Qt::transparent);
    painter.setPen(Qt::NoPen);
    painter.setBrush(light);
    painter.drawEllipse(center, radius, radius);
}

void drawBurst(QPainter& painter, const QPointF& center, double progress, double unit, const QColor& color)
{
    const double eased = 1.0 - std::pow(1.0 - progress, 3.0);
    const double inner = (kRippleStartRadius + kBurstTravel * eased) * unit;
    const double outer = inner + kBurstRayLength * (1.0 - progress) * unit;
    painter.setPen(QPen(withAlpha(color, int(kRippleRingAlpha * (1.0 - progress))),
                        kRippleRingWidth * unit, Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < kBurstRays; ++i) {
        const double angle = 2.0 * M_PI * i / kBurstRays;
        const QPointF ray(std::cos(angle), std::sin(angle));
        painter.drawLine(center + ray * inner, center + ray * outer);
    }
}

struct ClickStyleEntry {
    ClickStyle style;
    const char* name;
    qint64 durationMs;
    void (*draw)(QPainter&, const QPointF&, double, double, const QColor&);
};
const ClickStyleEntry kClickStyles[] = {
    {ClickStyle::Off, "off", 0, nullptr},
    {ClickStyle::Ring, "ring", CursorEffectRenderer::kRippleDurationMs, drawRing},
    {ClickStyle::Ripple, "ripple", kDoubleRippleDurationMs, drawRipple},
    {ClickStyle::Spotlight, "spotlight", CursorEffectRenderer::kRippleDurationMs, drawSpotlight},
    {ClickStyle::Burst, "burst", CursorEffectRenderer::kRippleDurationMs, drawBurst},
};
struct BounceLevel { PressBounce level; const char* name; double amplitude; };
constexpr BounceLevel kBounce[] = {
    {PressBounce::Off, "off", 0.0}, {PressBounce::Low, "low", 0.08}, {PressBounce::High, "high", 0.16}
};

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
        && pointerStyle == o.pointerStyle && steadyShape == o.steadyShape && straighten == o.straighten
        && clickStyle == o.clickStyle && clickColor == o.clickColor && rightClickColor == o.rightClickColor
        && separateRightClickColor == o.separateRightClickColor && pressBounce == o.pressBounce
        && idleHideMs == o.idleHideMs && motionBlur == o.motionBlur
        && loopBack == o.loopBack && stillEndMs == o.stillEndMs && hiddenRanges == o.hiddenRanges;
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
    const char* clickName = "off";
    for (const auto& entry : kClickStyles) if (entry.style == clickStyle) clickName = entry.name;
    const char* bounceName = "off";
    for (const auto& entry : kBounce) if (entry.level == pressBounce) bounceName = entry.name;
    QJsonArray ranges;
    for (const auto& range : hiddenRanges) ranges.append(QJsonArray{double(range.startMs), double(range.endMs)});
    QJsonObject json{{QStringLiteral("showCursor"), showCursor},
                     {QStringLiteral("scale"), scale},
                     {QStringLiteral("smoothing"), QLatin1String(smoothingName)},
                     {QStringLiteral("steadyShape"), steadyShape},
                     {QStringLiteral("pointerStyle"), int(pointerStyle)},
                     {QStringLiteral("straighten"), int(straighten)},
                     {QStringLiteral("clickStyle"), QLatin1String(clickName)},
                     {QStringLiteral("clickColor"), clickColor.name()},
                     {QStringLiteral("rightClickColor"), rightClickColor.name()},
                     {QStringLiteral("separateRightClickColor"), separateRightClickColor},
                     {QStringLiteral("pressBounce"), QLatin1String(bounceName)},
                     {QStringLiteral("idleHideMs"), double(idleHideMs)},
                     {QStringLiteral("motionBlur"), QLatin1String(blurName)},
                     {QStringLiteral("loopBack"), loopBack},
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
    settings.pointerStyle = PointerStyle(std::clamp(json.value(QStringLiteral("pointerStyle")).toInt(), 0, 1));
    settings.steadyShape = json.value(QStringLiteral("steadyShape")).toBool();
    settings.straighten = Straighten(std::clamp(json.value(QStringLiteral("straighten")).toInt(), 0, 2));
    const QString smoothing = json.value(QStringLiteral("smoothing")).toString();
    for (const auto& entry : kSmoothing) {
        if (smoothing == QLatin1String(entry.name)) settings.smoothing = entry.smoothing;
    }
    if (!json.contains(QStringLiteral("clickStyle"))) {
        settings.clickStyle = json.value(QStringLiteral("clickRipple")).toBool() ? ClickStyle::Ring : ClickStyle::Off;
    } else {
        const QString style = json.value(QStringLiteral("clickStyle")).toString();
        for (const auto& entry : kClickStyles) if (style == QLatin1String(entry.name)) settings.clickStyle = entry.style;
    }
    const auto color = [&json](const char* key) {
        QColor value(json.value(QLatin1String(key)).toString());
        return value.isValid() ? QColor(value.red(), value.green(), value.blue()) : QColor(Qt::white);
    };
    settings.clickColor = color("clickColor");
    settings.rightClickColor = color("rightClickColor");
    settings.separateRightClickColor = json.value(QStringLiteral("separateRightClickColor")).toBool();
    const QString bounce = json.value(QStringLiteral("pressBounce")).toString();
    for (const auto& entry : kBounce) if (bounce == QLatin1String(entry.name)) settings.pressBounce = entry.level;
    settings.idleHideMs = std::clamp(qint64(json.value(QStringLiteral("idleHideMs")).toDouble(0.0)), qint64(0),
                                     kMaxIdleHideMs);
    const QString blur = json.value(QStringLiteral("motionBlur")).toString();
    for (const auto& entry : kMotionBlur) {
        if (blur == QLatin1String(entry.name)) settings.motionBlur = entry.blur;
    }
    settings.loopBack = json.value(QStringLiteral("loopBack")).toBool();
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
    rebuildBouncePath();
    rebuildShapeSpans();
    rebuildLoopReturn();
}

void CursorEffectRenderer::setTimeline(const TimelineMap& timeline)
{
    if (m_kept == timeline.segments()) return;
    m_kept = timeline.segments();
    m_map = timeline;
    rebuildSpringPaths();
    rebuildBouncePath();
    rebuildShapeSpans();
    rebuildLoopReturn();
}

void CursorEffectRenderer::setLoopBounds(const QRectF& bounds)
{
    if (m_loopBounds == bounds) return;
    m_loopBounds = bounds;
    rebuildLoopReturn();
}

void CursorEffectRenderer::rebuildLoopReturn()
{
    m_loopReturn.reset();
    if (!m_settings.loopBack || m_timeline.samples.empty()) return;
    const auto time = LoopTransition::forTimeline(m_map, m_clipEndMs);
    if (!time) return;
    const auto first = positionAt(effectiveTime(time->firstSourceMs));
    const auto from = positionAt(effectiveTime(time->returnSourceMs));
    const auto last = positionAt(effectiveTime(time->lastSourceMs));
    const QRectF bounds = m_loopBounds.isEmpty() ? QRectF(QPointF(), m_timeline.info.frameSize) : m_loopBounds;
    if (!first || !from || !last || bounds.isEmpty() || !bounds.contains(*first)
        || !bounds.contains(*from) || !bounds.contains(*last)
        || m_settings.hiddenAt(time->firstSourceMs) || m_settings.hiddenAt(time->returnSourceMs)
        || m_settings.hiddenAt(time->lastSourceMs)) return;
    const double firstOpacity = pointerOpacity(time->firstSourceMs);
    const double fromOpacity = pointerOpacity(time->returnSourceMs);
    if (firstOpacity <= 0.0 || fromOpacity <= 0.0 || pointerOpacity(time->lastSourceMs) <= 0.0) return;
    m_loopReturn = LoopReturn{*time, *from, *first, pointerShapeId(time->firstSourceMs), fromOpacity, firstOpacity};
}

std::optional<double> CursorEffectRenderer::loopProgress(qint64 sourceMs) const
{
    if (!m_loopReturn) return std::nullopt;
    const auto output = m_map.isEmpty() ? std::optional<qint64>(sourceMs) : m_map.sourceToOutput(sourceMs);
    if (!output || *output < m_loopReturn->time.startOutputMs || *output >= m_loopReturn->time.endOutputMs)
        return std::nullopt;
    return m_loopReturn->time.progress(*output);
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

void CursorEffectRenderer::rebuildShapeSpans()
{
    m_shapes.clear();
    if (!m_settings.steadyShape || m_timeline.samples.empty()) return;
    const auto& samples = m_timeline.samples;
    const qint64 last = m_clipEndMs >= 0 ? m_clipEndMs : samples.back().tMs + kShapeHoldMinMs;
    const auto parts = m_kept.empty() ? std::vector<TimelineSegment>{{samples.front().tMs, last, 1.0}} : m_kept;
    for (const auto& part : parts) {
        const qint64 start = std::max(part.startMs, samples.front().tMs);
        if (start >= part.endMs) continue;
        struct Run { qint64 start; qint64 end; quint32 id; bool visible; };
        const auto* initial = m_timeline.sampleAt(start);
        std::vector<Run> runs{{start, part.endMs, initial->shapeId, initial->visible}};
        auto sample = std::upper_bound(samples.begin(), samples.end(), start,
            [](qint64 t, const CursorSample& s) { return t < s.tMs; });
        for (; sample != samples.end() && sample->tMs < part.endMs; ++sample) {
            if (sample->shapeId == runs.back().id && sample->visible == runs.back().visible) continue;
            runs.back().end = sample->tMs;
            runs.push_back({sample->tMs, part.endMs, sample->shapeId, sample->visible});
        }
        std::vector<std::pair<qint64, qint64>> guards;
        auto event = std::lower_bound(m_timeline.buttons.begin(), m_timeline.buttons.end(), start,
            [](const CursorButtonEvent& e, qint64 t) { return e.tMs < t; });
        for (; event != m_timeline.buttons.end() && event->tMs < part.endMs; ++event) {
            if (event->synthetic) continue;
            const qint64 a = std::max(start, event->tMs - kShapeClickGuardMs);
            const qint64 b = std::min(part.endMs, event->tMs + kShapeClickGuardMs + 1);
            if (!guards.empty() && a <= guards.back().second) guards.back().second = std::max(b, guards.back().second);
            else guards.push_back({a, b});
        }
        quint32 stable = initial->shapeId;
        bool wasVisible = initial->visible;
        size_t guard = 0;
        for (const auto& run : runs) {
            if (run.end - run.start >= kShapeHoldMinMs || run.visible != wasVisible) stable = run.id;
            wasVisible = run.visible;
            qint64 at = run.start;
            while (at < run.end) {
                while (guard < guards.size() && guards[guard].second <= at) ++guard;
                const bool guarded = guard < guards.size() && guards[guard].first <= at;
                const qint64 end = guard == guards.size() ? run.end
                    : std::min(run.end, guarded ? guards[guard].second : guards[guard].first);
                const quint32 id = guarded ? run.id : stable;
                if (!m_shapes.empty() && m_shapes.back().endMs == at && m_shapes.back().id == id)
                    m_shapes.back().endMs = end;
                else m_shapes.push_back({at, end, id});
                at = end;
            }
        }
    }
}

quint32 CursorEffectRenderer::pointerShapeId(qint64 sourceMs) const
{
    if (loopProgress(sourceMs)) return m_loopReturn->shape;
    const qint64 shown = effectiveTime(sourceMs);
    auto span = std::upper_bound(m_shapes.begin(), m_shapes.end(), shown,
        [](qint64 t, const ShapeSpan& s) { return t < s.startMs; });
    if (span != m_shapes.begin() && shown < (span - 1)->endMs) return (span - 1)->id;
    const auto* sample = m_timeline.sampleAt(shown);
    return sample ? sample->shapeId : 0;
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
        auto dragEvent = std::upper_bound(buttons.begin(), buttons.end(), start,
            [](qint64 t, const CursorButtonEvent& e) { return t < e.tMs; });
        Qt::MouseButtons dragging = m_timeline.buttonsDownAt(start);
        for (; sample != samples.end() && sample->tMs < part.endMs; ++sample) {
            bool inDrag = bool(dragging);
            while (dragEvent != buttons.end() && dragEvent->tMs <= sample->tMs) {
                if (dragEvent->down) dragging |= dragEvent->button; else dragging &= ~Qt::MouseButtons(dragEvent->button);
                inDrag |= bool(dragging);
                ++dragEvent;
            }
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
            if ((m_settings.straighten == CursorEffectSettings::Straighten::Off || inDrag)
                && interior && !semantic && !lastSemantic && input.knots.size() > 1
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
        if (m_settings.straighten != CursorEffectSettings::Straighten::Off && unique.size() > 2) {
            constexpr double kTolerancePx[] = {0.0, 4.0, 10.0};
            const double tolerance = kTolerancePx[int(m_settings.straighten)] * m_timeline.info.pixelRatio;
            auto boundary = std::upper_bound(samples.begin(), samples.end(), start,
                [](qint64 t, const CursorSample& s) { return t < s.tMs; });
            auto button = event;
            Qt::MouseButtons held = m_timeline.buttonsDownAt(start);
            size_t anchor = 0;
            for (size_t i = 1; i < unique.size(); ++i) {
                const qint64 ms = qRound64(unique[i].ms);
                const bool gap = unique[i].ms - unique[i - 1].ms > kInputInterpolationMs;
                bool fixed = bool(held);
                while (button != eventEnd && button->tMs <= ms) {
                    fixed = true;
                    if (button->down) held |= button->button; else held &= ~Qt::MouseButtons(button->button);
                    ++button;
                }
                while (boundary != samples.end() && boundary->tMs <= ms) {
                    const auto& previous = *(boundary - 1);
                    fixed |= boundary->shapeId != previous.shapeId || boundary->visible != previous.visible;
                    ++boundary;
                }
                if (gap) {
                    // Both sides of a gap are anchors. Including i in the
                    // previous window would move the last held position.
                    straightenWindow(unique, anchor, i - 1, tolerance);
                    anchor = i;
                    continue;
                }
                if (fixed || i + 1 == unique.size() || unique[i].ms - unique[anchor].ms >= kStraightenWindowMs) {
                    // Drag intervals have every point fixed. A gap's endpoints
                    // stay fixed too, retaining the recorder's held position.
                    straightenWindow(unique, anchor, i, tolerance);
                    anchor = i;
                }
            }
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
    if (const auto progress = loopProgress(sourceMs))
        return m_loopReturn->from + (m_loopReturn->to - m_loopReturn->from) * *progress;
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
    if (const auto p = loopProgress(sourceMs))
        return m_loopReturn->fromOpacity + (m_loopReturn->toOpacity - m_loopReturn->fromOpacity) * *p;
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
    if (m_settings.clickStyle != ClickStyle::Off && m_timeline.info.clicksAvailable) paintClicks(painter, sourceMs, mapping);
    if (m_settings.showCursor) paintPointer(painter, sourceMs, mapping);
    painter.restore();
}

void CursorEffectRenderer::paintClicks(QPainter& painter, qint64 sourceMs, const CursorFrameMapping& mapping) const
{
    if (loopProgress(sourceMs)) return;
    const ClickStyleEntry* style = nullptr;
    for (const auto& entry : kClickStyles) if (entry.style == m_settings.clickStyle) style = &entry;
    if (!style || !style->draw) return;
    const double unit = m_timeline.info.pixelRatio * mapping.scale() * m_settings.scale;
    const auto& buttons = m_timeline.buttons;
    const qint64 from = std::max(sourceMs - style->durationMs + 1, partStart(sourceMs));
    auto it = std::lower_bound(buttons.begin(), buttons.end(), from,
                               [](const CursorButtonEvent& e, qint64 t) { return e.tMs < t; });
    painter.save();
    painter.setClipRect(QRect(QPoint(), mapping.outputSize), Qt::IntersectClip);
    for (; it != buttons.end() && it->tMs <= sourceMs; ++it) {
        if (!it->down || it->synthetic) continue;
        const QColor color = it->button == Qt::RightButton && m_settings.separateRightClickColor
            ? m_settings.rightClickColor : m_settings.clickColor;
        style->draw(painter, mapping.toOutput(QPointF(it->position)),
                    double(sourceMs - it->tMs) / style->durationMs, unit, color);
    }
    painter.restore();
}

double CursorEffectRenderer::bounceDepth(const BouncePoint& point, qint64 ms)
{
    const double progress = std::clamp(double(ms - point.tMs) / (kBounceDurationMs / 2.0), 0.0, 1.0);
    // Two halves of a sine bounce: compress, hold while dragging, recover.
    return point.held ? point.depth + (1.0 - point.depth) * std::sin(progress * M_PI_2)
                      : point.depth * std::cos(progress * M_PI_2);
}

void CursorEffectRenderer::rebuildBouncePath()
{
    m_bounces.clear();
    if (m_settings.pressBounce == PressBounce::Off || !m_timeline.info.clicksAvailable) return;
    Qt::MouseButtons held;
    qint64 start = std::numeric_limits<qint64>::min();
    for (const auto& event : m_timeline.buttons) {
        const qint64 boundary = partStart(event.tMs);
        if (!m_kept.empty() && partEnd(event.tMs) == event.tMs) continue;
        if (start != boundary) { held = {}; start = boundary; }
        const double depth = m_bounces.empty() || m_bounces.back().tMs < boundary
            ? 0.0 : bounceDepth(m_bounces.back(), event.tMs);
        if (event.synthetic) {
            held.setFlag(event.button, false);
            m_bounces.push_back({event.tMs, held ? depth : 0.0, bool(held)});
            continue;
        }
        // A release whose press was cut out cannot animate the next part.
        if (!event.down && !held.testFlag(event.button)) continue;
        held.setFlag(event.button, event.down);
        m_bounces.push_back({event.tMs, depth, bool(held)});
    }
}

double CursorEffectRenderer::pointerBounceScale(qint64 sourceMs) const
{
    if (loopProgress(sourceMs)) return 1.0;
    auto it = std::upper_bound(m_bounces.begin(), m_bounces.end(), sourceMs,
                               [](qint64 t, const BouncePoint& point) { return t < point.tMs; });
    if (it == m_bounces.begin() || (it - 1)->tMs < partStart(sourceMs)) return 1.0;
    double amplitude = 0.0;
    for (const auto& entry : kBounce) if (entry.level == m_settings.pressBounce) amplitude = entry.amplitude;
    return std::max(kMinimumBounceScale, 1.0 - amplitude * bounceDepth(*--it, sourceMs));
}

void CursorEffectRenderer::paintPointer(QPainter& painter, qint64 sourceMs, const CursorFrameMapping& mapping) const
{
    const auto position = pointerPosition(sourceMs);
    if (!position) return;
    const double opacity = pointerOpacity(sourceMs);
    if (opacity <= 0.0) return;
    const QPointF at = mapping.toOutput(*position);
    const double scale = mapping.scale() * m_settings.scale;
    const CursorShape* shape = m_timeline.shape(pointerShapeId(sourceMs));
    const double baseOpacity = painter.opacity();
    // Motion blur: fainter copies where the pointer was a moment before, at
    // fixed time offsets, so a frame never depends on what was drawn before.
    if (const double window = blurWindowMs(); window > 0.0 && !loopProgress(sourceMs)) {
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
    drawPointerAt(painter, at, shape, scale * pointerBounceScale(sourceMs));
    painter.setOpacity(baseOpacity);
}

void CursorEffectRenderer::drawPointerAt(QPainter& painter, const QPointF& at, const CursorShape* shape,
                                         double scale) const
{
    if (shape) {
        if (m_settings.pointerStyle == CursorEffectSettings::PointerStyle::Crisp && m_settings.scale > kVectorCursorMinScale
            && drawCrispCursor(painter, shape->kind, at, shape->image.height() * scale)) return;
        // Shape images are in recorded (video) pixels; the hotspot stays on the point.
        const QRectF target(at - QPointF(shape->hotspot) * scale, QSizeF(shape->image.size()) * scale);
        painter.save();
        if (m_settings.pointerStyle == CursorEffectSettings::PointerStyle::Crisp)
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(target, shape->image);
        painter.restore();
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
