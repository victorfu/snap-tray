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

// Keep a view inside the picture: corners and edges clamp, never show outside.
QRectF clampedInto(QRectF rect, const QRectF& bounds)
{
    rect.moveLeft(std::clamp(rect.left(), bounds.left(), bounds.right() - rect.width()));
    rect.moveTop(std::clamp(rect.top(), bounds.top(), bounds.bottom() - rect.height()));
    return rect;
}

QRectF frameFor(const QPointF& focus, double scale, const QRectF& bounds)
{
    const QSizeF size = bounds.size() / std::max(1.0, scale);
    return clampedInto(QRectF(focus - QPointF(size.width() / 2, size.height() / 2), size), bounds);
}

// How far a long pan pulls out on its way (#263): van Wijk and Nuij's rho;
// larger pulls out more. sqrt(2) is d3's default for interpolateZoom.
constexpr double kZoomPanRho = 1.4142135623730951;
// Views whose centres are closer than this (source pixels) only zoom.
constexpr double kPureZoomDistance = 1e-6;

// The view between two views (same aspect ratio as `bounds`) that van Wijk
// and Nuij show looks steadiest: J. J. van Wijk, W. A. A. Nuij, "Smooth and
// efficient zooming and panning", IEEE InfoVis 2003. Along it the perceived
// motion (pan measured in view widths, plus zoom) is uniform, so the scale
// changes by equal factors in equal steps and a far pan pulls out on the way
// instead of sweeping the screen at full zoom. t is 0..1 along the path; the
// ends are exactly `from` and `to`.
class ZoomPanPath {
public:
    ZoomPanPath(const QRectF& from, const QRectF& to)
        : m_from(from), m_to(to), m_c0(from.center()), m_delta(to.center() - from.center()),
          m_w0(from.width())
    {
        const double w1 = to.width();
        m_distance = std::hypot(m_delta.x(), m_delta.y());
        if (m_w0 <= 0.0 || w1 <= 0.0) return;
        if (m_distance < kPureZoomDistance) {
            m_widthRatio = w1 / m_w0;
            return;
        }
        constexpr double rho2 = kZoomPanRho * kZoomPanRho;
        const double d2 = m_distance * m_distance;
        const double b0 = (w1 * w1 - m_w0 * m_w0 + rho2 * rho2 * d2) / (2.0 * m_w0 * rho2 * m_distance);
        const double b1 = (w1 * w1 - m_w0 * m_w0 - rho2 * rho2 * d2) / (2.0 * w1 * rho2 * m_distance);
        // r = ln(sqrt(b^2 + 1) - b), written as -asinh(b) to stay exact for large b.
        m_r0 = -std::asinh(b0);
        m_length = (-std::asinh(b1) - m_r0) / kZoomPanRho;
        m_panning = true;
    }

    QRectF at(double t, const QRectF& bounds) const
    {
        if (t <= 0.0) return m_from;
        if (t >= 1.0) return m_to;
        double width = m_w0;
        double progress = t; // of the way from one centre to the other
        if (m_panning) {
            const double r = kZoomPanRho * t * m_length + m_r0;
            progress = m_w0 / (kZoomPanRho * kZoomPanRho * m_distance)
                * (std::cosh(m_r0) * std::tanh(r) - std::sinh(m_r0));
            width = m_w0 * std::cosh(m_r0) / std::cosh(r);
        } else {
            width = m_w0 * std::pow(m_widthRatio, t);
        }
        // Never wider than the picture (a long pan's pull-out stops at the whole view).
        width = std::min(width, bounds.width());
        const QSizeF size(width, width * bounds.height() / bounds.width());
        const QPointF centre = m_c0 + m_delta * progress;
        return clampedInto(QRectF(centre - QPointF(size.width() / 2, size.height() / 2), size), bounds);
    }

private:
    QRectF m_from;
    QRectF m_to;
    QPointF m_c0;
    QPointF m_delta;
    double m_w0 = 0.0;
    double m_distance = 0.0;
    double m_widthRatio = 1.0; // pure zoom: end width over start width
    double m_r0 = 0.0;
    double m_length = 0.0;     // the path's length S
    bool m_panning = false;
};

QRectF easedTransition(const QRectF& from, const QRectF& to, double t, const QRectF& bounds)
{
    return ZoomPanPath(from, to).at(smootherstep(t), bounds);
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

    // Close together in time: pan (and re-zoom) directly across the gap,
    // along the zoom-and-pan path (#263), which pulls out on the way when the
    // views are far apart. Segments that touch or nearly touch still get
    // kMinBlendMs, centred on their boundary, instead of a cut (#176 review).
    // Less Motion keeps the cut: no motion at all is gentler than a fast pan.
    if (!reduceMotion) {
        for (size_t i = 0; i + 1 < m_segments.size(); ++i) {
            const ZoomSegment& a = m_segments[i];
            const ZoomSegment& b = m_segments[i + 1];
            if (b.startMs - a.endMs > 2 * transition) continue;
            const double middle = (a.endMs + b.startMs) / 2.0;
            const double from = std::min(double(a.endMs), middle - kMinBlendMs / 2.0);
            const double to = std::max(double(b.startMs), middle + kMinBlendMs / 2.0);
            if (sourceMs > from && sourceMs < to) {
                return easedTransition(frame(a), frame(b), (sourceMs - from) / (to - from), bounds);
            }
        }
    }

    // The segment holding sourceMs, or its neighbours.
    const ZoomSegment* previous = nullptr;
    const ZoomSegment* next = nullptr;
    for (const auto& segment : m_segments) {
        if (sourceMs >= segment.startMs && sourceMs <= segment.endMs) return frame(segment);
        if (segment.endMs < sourceMs) previous = &segment;
        if (segment.startMs > sourceMs && !next) next = &segment;
    }
    // Otherwise ease out to the whole picture and back in, sharing a short gap.
    const qint64 room = previous && next ? (next->startMs - previous->endMs) / 2 : transition;
    const qint64 ramp = std::max<qint64>(1, std::min(transition, room));
    if (previous && sourceMs < previous->endMs + ramp) {
        return easedTransition(frame(*previous), bounds, double(sourceMs - previous->endMs) / ramp, bounds);
    }
    if (next && sourceMs > next->startMs - ramp) {
        return easedTransition(bounds, frame(*next), double(sourceMs - (next->startMs - ramp)) / ramp, bounds);
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

    const auto build = [&](const std::vector<Interaction>& planned) {
        std::vector<ZoomSegment> segments;
        for (const auto& group : planned) {
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
        return normalized;
    };

    // Every interaction must be in view when it happens. Groups squeezed by
    // close neighbours can end up too short to keep (or mid-pan): then that
    // group and its neighbours share one wider frame, or the full view when
    // that is too wide. Each pass removes a group, so this ends.
    for (;;) {
        const ZoomTrack track = build(merged);
        const auto hidden = std::find_if(interactions.cbegin(), interactions.cend(), [&](const Interaction& i) {
            return !track.viewportAt(i.startMs, bounds).contains(i.area)
                || !track.viewportAt(i.endMs, bounds).contains(i.area);
        });
        if (hidden == interactions.cend()) return track.segments();
        if (merged.size() <= 1) return {};
        auto owner = std::find_if(merged.begin(), merged.end(), [&](const Interaction& group) {
            return hidden->startMs >= group.firstEventMs && hidden->startMs <= group.lastEventMs;
        });
        if (owner == merged.end()) return {};
        const auto first = owner == merged.begin() ? owner : owner - 1;
        const auto last = owner + 1 == merged.end() ? owner : owner + 1;
        Interaction wide = *first;
        for (auto it = first + 1; it <= last; ++it) {
            wide.endMs = std::max(wide.endMs, it->endMs);
            wide.area |= it->area;
            wide.lastEventMs = std::max(wide.lastEventMs, it->lastEventMs);
        }
        const auto at = merged.erase(first, last + 1);
        merged.insert(at, wide);
    }
}

} // namespace SnapTray
