#include "recording/ZoomTrack.h"

#include <QJsonObject>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace SnapTray {

namespace {

// Auto zoom tuning. Times in ms; distances as fractions of the framed area.
constexpr qint64 kClusterGapMs = 2000;       // interactions further apart in time start a new group
constexpr double kClusterRadius = 0.25;      // ... or further apart in space (of the shorter side)
constexpr double kMarginFraction = 0.08;     // room kept around the interaction (of the shorter side)
constexpr double kMinUsefulScale = 1.25;     // less zoom than this is not worth moving the camera
constexpr qint64 kLeadMs = 700;              // zoomed in this long before the first click
constexpr qint64 kTailMs = 1500;             // and held this long after the last
constexpr qint64 kMinClusterMs = 1500;       // a single click still gets a readable moment
constexpr qint64 kCooldownMs = 800;          // closer groups merge instead of zooming out and in

double smootherstep(double x)
{
    x = std::clamp(x, 0.0, 1.0);
    return x * x * x * (x * (x * 6.0 - 15.0) + 10.0);
}

QRectF lerp(const QRectF& a, const QRectF& b, double t)
{
    return QRectF(a.x() + (b.x() - a.x()) * t, a.y() + (b.y() - a.y()) * t,
                  a.width() + (b.width() - a.width()) * t, a.height() + (b.height() - a.height()) * t);
}

QRectF frameFor(const QPointF& focus, double scale, const QRectF& bounds)
{
    const QSizeF size = bounds.size() / std::max(1.0, scale);
    QRectF rect(focus - QPointF(size.width() / 2, size.height() / 2), size);
    // Keep the view inside the picture: corners and edges clamp, never show outside.
    rect.moveLeft(std::clamp(rect.left(), bounds.left(), bounds.right() - rect.width()));
    rect.moveTop(std::clamp(rect.top(), bounds.top(), bounds.bottom() - rect.height()));
    return rect;
}

struct Interaction {
    qint64 startMs;
    qint64 endMs;
    QRectF area; // points it touched (a drag's whole path)
    qint64 firstEventMs = 0; // the interactions themselves, without lead and tail
    qint64 lastEventMs = 0;
};

} // namespace

bool ZoomSegment::operator==(const ZoomSegment& o) const
{
    return startMs == o.startMs && endMs == o.endMs && qFuzzyCompare(scale, o.scale) && focus == o.focus
        && manual == o.manual;
}

void ZoomTrack::setSegments(std::vector<ZoomSegment> segments)
{
    std::sort(segments.begin(), segments.end(),
              [](const ZoomSegment& a, const ZoomSegment& b) { return a.startMs < b.startMs; });
    m_segments.clear();
    for (auto segment : segments) {
        segment.scale = std::clamp(segment.scale, kMinScale, kMaxScale);
        if (!m_segments.empty()) segment.startMs = std::max(segment.startMs, m_segments.back().endMs);
        if (segment.endMs - segment.startMs < kMinSegmentMs) continue;
        m_segments.push_back(segment);
    }
}

int ZoomTrack::segmentAt(qint64 sourceMs) const
{
    for (size_t i = 0; i < m_segments.size(); ++i) {
        if (sourceMs >= m_segments[i].startMs && sourceMs <= m_segments[i].endMs) return int(i);
    }
    return -1;
}

QRectF ZoomTrack::viewportAt(qint64 sourceMs, const QRectF& bounds) const
{
    if (!isActive() || bounds.isEmpty()) return bounds;
    const qint64 transition = reduceMotion ? kReducedMotionTransitionMs : kTransitionMs;
    const double maxScale = reduceMotion ? kReducedMotionMaxScale : kMaxScale;
    const auto frame = [&](const ZoomSegment& s) {
        return frameFor(s.focus, std::min(s.scale, maxScale), bounds);
    };

    // The segment holding sourceMs, or its neighbours.
    const ZoomSegment* previous = nullptr;
    const ZoomSegment* next = nullptr;
    for (const auto& segment : m_segments) {
        if (sourceMs >= segment.startMs && sourceMs <= segment.endMs) return frame(segment);
        if (segment.endMs < sourceMs) previous = &segment;
        if (segment.startMs > sourceMs && !next) next = &segment;
    }
    if (previous && next) {
        const qint64 gap = next->startMs - previous->endMs;
        if (!reduceMotion && gap <= 2 * transition) {
            // Close together: pan (and re-zoom) straight across.
            return lerp(frame(*previous), frame(*next), smootherstep(double(sourceMs - previous->endMs) / gap));
        }
    }
    // Otherwise ease out to the whole picture and back in, sharing a short gap.
    const qint64 room = previous && next ? (next->startMs - previous->endMs) / 2 : transition;
    const qint64 ramp = std::max<qint64>(1, std::min(transition, room));
    if (previous && sourceMs < previous->endMs + ramp) {
        return lerp(frame(*previous), bounds, smootherstep(double(sourceMs - previous->endMs) / ramp));
    }
    if (next && sourceMs > next->startMs - ramp) {
        return lerp(bounds, frame(*next), smootherstep(double(sourceMs - (next->startMs - ramp)) / ramp));
    }
    return bounds;
}

QJsonObject ZoomTrack::toJson() const
{
    QJsonArray segments;
    for (const auto& s : m_segments) {
        segments.append(QJsonObject{{QStringLiteral("start"), s.startMs},
                                    {QStringLiteral("end"), s.endMs},
                                    {QStringLiteral("scale"), s.scale},
                                    {QStringLiteral("x"), s.focus.x()},
                                    {QStringLiteral("y"), s.focus.y()},
                                    {QStringLiteral("manual"), s.manual}});
    }
    return {{QStringLiteral("enabled"), enabled},
            {QStringLiteral("reduceMotion"), reduceMotion},
            {QStringLiteral("segments"), segments}};
}

ZoomTrack ZoomTrack::fromJson(const QJsonObject& json)
{
    ZoomTrack track;
    track.enabled = json.value(QStringLiteral("enabled")).toBool(true);
    track.reduceMotion = json.value(QStringLiteral("reduceMotion")).toBool(false);
    std::vector<ZoomSegment> segments;
    for (const auto& value : json.value(QStringLiteral("segments")).toArray()) {
        const QJsonObject o = value.toObject();
        ZoomSegment s;
        s.startMs = o.value(QStringLiteral("start")).toInteger();
        s.endMs = o.value(QStringLiteral("end")).toInteger();
        s.scale = o.value(QStringLiteral("scale")).toDouble(2.0);
        s.focus = QPointF(o.value(QStringLiteral("x")).toDouble(), o.value(QStringLiteral("y")).toDouble());
        s.manual = o.value(QStringLiteral("manual")).toBool();
        segments.push_back(s);
    }
    track.setSegments(std::move(segments));
    return track;
}

bool ZoomTrack::operator==(const ZoomTrack& o) const
{
    return enabled == o.enabled && reduceMotion == o.reduceMotion && m_segments == o.m_segments;
}

std::vector<ZoomSegment> generateAutoZoom(const CursorTimeline& timeline, const AutoZoomOptions& options)
{
    const QRectF bounds = options.bounds;
    if (bounds.isEmpty() || timeline.buttons.empty()) return {};
    const double shortSide = std::min(bounds.width(), bounds.height());
    const auto usable = [&](qint64 t, const QPoint& p) {
        const CursorSample* sample = timeline.sampleAt(t);
        return (!sample || sample->visible) && bounds.contains(QPointF(p));
    };

    // Presses (with a drag's whole path while held) on visible, on-frame positions.
    std::vector<Interaction> interactions;
    const auto& buttons = timeline.buttons;
    for (size_t i = 0; i < buttons.size(); ++i) {
        const auto& press = buttons[i];
        if (!press.down || press.synthetic || !usable(press.tMs, press.position)) continue;
        Interaction interaction{press.tMs, press.tMs, QRectF(QPointF(press.position), QSizeF(1, 1))};
        for (size_t j = i + 1; j < buttons.size(); ++j) {
            if (buttons[j].button == press.button && !buttons[j].down) {
                interaction.endMs = buttons[j].tMs;
                break;
            }
        }
        for (const auto& sample : timeline.samples) {
            if (sample.tMs < press.tMs || sample.tMs > interaction.endMs) continue;
            if (usable(sample.tMs, sample.position)) interaction.area |= QRectF(QPointF(sample.position), QSizeF(1, 1));
        }
        interactions.push_back(interaction);
    }

    // Group nearby interactions.
    std::vector<Interaction> groups;
    for (const auto& interaction : interactions) {
        if (!groups.empty()) {
            auto& group = groups.back();
            const QPointF delta = interaction.area.center() - group.area.center();
            if (interaction.startMs - group.endMs <= kClusterGapMs
                && std::hypot(delta.x(), delta.y()) <= kClusterRadius * shortSide) {
                group.endMs = std::max(group.endMs, interaction.endMs);
                group.area |= interaction.area;
                continue;
            }
        }
        groups.push_back(interaction);
    }

    const double maxScale = std::clamp(options.maxScale, ZoomTrack::kMinScale, ZoomTrack::kMaxScale);
    const auto scaleFor = [&](const QRectF& area) {
        const QRectF framed = area.adjusted(-kMarginFraction * shortSide, -kMarginFraction * shortSide,
                                            kMarginFraction * shortSide, kMarginFraction * shortSide);
        return std::min({maxScale, bounds.width() / framed.width(), bounds.height() / framed.height()});
    };
    const auto clampTime = [&](qint64 t) {
        return options.durationMs > 0 ? std::clamp<qint64>(t, 0, options.durationMs) : std::max<qint64>(t, 0);
    };

    // Merge groups that would otherwise pump out and back in, when the
    // merged frame is still worth zooming to.
    std::vector<Interaction> merged;
    for (auto group : groups) {
        group.firstEventMs = group.startMs;
        group.lastEventMs = group.endMs;
        group.startMs -= kLeadMs;
        group.endMs = std::max(group.endMs + kTailMs, group.startMs + kLeadMs + kMinClusterMs);
        if (!merged.empty() && group.startMs - merged.back().endMs < kCooldownMs
            && scaleFor(merged.back().area | group.area) >= kMinUsefulScale) {
            merged.back().endMs = std::max(merged.back().endMs, group.endMs);
            merged.back().area |= group.area;
            merged.back().lastEventMs = std::max(merged.back().lastEventMs, group.lastEventMs);
            continue;
        }
        if (!merged.empty() && group.startMs < merged.back().endMs) {
            // Too far apart to share a frame but close in time: hand over
            // between them, each keeping its own interactions zoomed, with
            // up to one transition of pan in the middle.
            auto& previous = merged.back();
            const qint64 middle = (previous.lastEventMs + group.firstEventMs) / 2;
            const qint64 halfPan = ZoomTrack::kTransitionMs / 2;
            previous.endMs = std::max(previous.lastEventMs, middle - halfPan);
            group.startMs = std::max(previous.endMs, std::min(group.firstEventMs, middle + halfPan));
        }
        merged.push_back(group);
    }

    std::vector<ZoomSegment> segments;
    for (const auto& group : merged) {
        const double scale = scaleFor(group.area);
        if (scale < kMinUsefulScale) continue; // the action spans most of the picture: keep the full view
        ZoomSegment segment;
        segment.startMs = clampTime(group.startMs);
        segment.endMs = clampTime(group.endMs);
        segment.scale = scale;
        segment.focus = frameFor(group.area.center(), scale, bounds).center();
        segments.push_back(segment);
    }
    ZoomTrack normalized;
    normalized.setSegments(std::move(segments));
    return normalized.segments();
}

} // namespace SnapTray
