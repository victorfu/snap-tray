#include "recording/ZoomTrack.h"

#include <QJsonObject>
#include <QPainter>
#include <array>
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
    bool menu = false;
};

} // namespace

bool ZoomSegment::operator==(const ZoomSegment& o) const
{
    return startMs == o.startMs && endMs == o.endMs && qFuzzyCompare(scale, o.scale) && focus == o.focus
        && follow == o.follow && manual == o.manual && followStart == o.followStart
        && stationaryScale.has_value() == o.stationaryScale.has_value()
        && (!stationaryScale || qFuzzyCompare(*stationaryScale, *o.stationaryScale));
}

void ZoomTrack::setSegments(std::vector<ZoomSegment> segments)
{
    std::sort(segments.begin(), segments.end(),
              [](const ZoomSegment& a, const ZoomSegment& b) { return a.startMs < b.startMs; });
    m_segments.clear();
    m_follow.clear();
    m_followButtons.clear();
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
    const auto frame = [&](const ZoomSegment& s, qint64 ms) {
        const size_t index = size_t(&s - m_segments.data());
        const bool following = !reduceMotion && bounds == m_followBounds && s.follow
            && index < m_follow.size() && !m_follow[index].empty();
        if (following) return frameFor(focusAt(s, ms), std::min(s.scale, maxScale), bounds);
        const double scale = std::min({s.scale, s.stationaryScale.value_or(s.scale), maxScale});
        return frameFor(s.focus, scale, bounds);
    };

    const auto visibleFrame = [&](QRectF view) {
        const int index = segmentAt(sourceMs);
        if (reduceMotion || bounds != m_followBounds || index < 0
            || !m_segments[size_t(index)].follow || m_follow.empty()) return view;
        auto event = std::lower_bound(m_followButtons.begin(), m_followButtons.end(), sourceMs,
            [](const CursorButtonEvent& e, qint64 t) { return e.tMs < t; });
        const double margin = kMarginFraction * std::min(bounds.width(), bounds.height());
        for (; event != m_followButtons.end() && event->tMs == sourceMs; ++event) {
            const QRectF required = QRectF(QPointF(event->position), QSizeF(1, 1))
                .adjusted(-margin, -margin, margin, margin).intersected(bounds);
            if (required.left() < view.left()) view.moveLeft(required.left());
            if (required.right() > view.right()) view.moveRight(required.right());
            if (required.top() < view.top()) view.moveTop(required.top());
            if (required.bottom() > view.bottom()) view.moveBottom(required.bottom());
        }
        return frameFor(view.center(), bounds.width() / view.width(), bounds);
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
                return visibleFrame(easedTransition(frame(a, qint64(from)), frame(b, qint64(to)), (sourceMs - from) / (to - from), bounds));
            }
        }
    }

    // The segment holding sourceMs, or its neighbours.
    const ZoomSegment* previous = nullptr;
    const ZoomSegment* next = nullptr;
    for (const auto& segment : m_segments) {
        if (sourceMs >= segment.startMs && sourceMs <= segment.endMs) return visibleFrame(frame(segment, sourceMs));
        if (segment.endMs < sourceMs) previous = &segment;
        if (segment.startMs > sourceMs && !next) next = &segment;
    }
    // Otherwise ease out to the whole picture and back in, sharing a short gap.
    const qint64 room = previous && next ? (next->startMs - previous->endMs) / 2 : transition;
    const qint64 ramp = std::max<qint64>(1, std::min(transition, room));
    if (previous && sourceMs < previous->endMs + ramp) {
        return easedTransition(frame(*previous, previous->endMs), bounds, double(sourceMs - previous->endMs) / ramp, bounds);
    }
    if (next && sourceMs > next->startMs - ramp) {
        return easedTransition(bounds, frame(*next, next->startMs), double(sourceMs - (next->startMs - ramp)) / ramp, bounds);
    }
    return bounds;
}

QPointF ZoomTrack::focusAt(const ZoomSegment& segment, qint64 ms) const
{
    const size_t index = size_t(&segment - m_segments.data());
    if (!segment.follow || index >= m_follow.size() || m_follow[index].empty()) return segment.focus;
    const auto& path = m_follow[index];
    const auto next = std::lower_bound(path.begin(), path.end(), ms,
        [](const FollowPoint& point, qint64 time) { return point.ms < time; });
    if (next == path.begin()) return next->center;
    if (next == path.end()) return path.back().center;
    const auto& previous = *(next - 1);
    const double t = double(ms - previous.ms) / double(next->ms - previous.ms);
    return previous.center + (next->center - previous.center) * t;
}

QRectF ZoomTrack::loopViewportAt(qint64 sourceMs, const QRectF& bounds, const TimelineMap& timeline,
                                 bool loopBack, qint64 clipEndMs) const
{
    if (!loopBack) return viewportAt(sourceMs, bounds);
    const auto time = LoopTransition::forTimeline(timeline, clipEndMs);
    const auto output = timeline.isEmpty() ? std::optional<qint64>(sourceMs) : timeline.sourceToOutput(sourceMs);
    if (!time || !output || *output < time->startOutputMs || *output >= time->endOutputMs)
        return viewportAt(sourceMs, bounds);
    const QRectF from = viewportAt(time->returnSourceMs, bounds);
    const QRectF to = viewportAt(time->firstSourceMs, bounds);
    const double progress = time->progress(*output);
    return QRectF(from.topLeft() + (to.topLeft() - from.topLeft()) * progress,
                  from.size() + (to.size() - from.size()) * progress);
}

void ZoomTrack::resample(QImage& frame, qint64 sourceMs, const QRectF& bounds, const TimelineMap& timeline,
                         bool loopBack, double outputFps, qint64 clipEndMs) const
{
    if (frame.isNull() || bounds.isEmpty()) return;
    const auto viewport = [&](qint64 ms) { return loopViewportAt(ms, bounds, timeline, loopBack, clipEndMs); };
    const QRectF shown = viewport(sourceMs);
    const double sx = frame.width() / bounds.width(), sy = frame.height() / bounds.height();
    const auto sampleImage = [&](const QRectF& view) {
        QImage image(frame.size(), QImage::Format_ARGB32_Premultiplied);
        if (image.isNull()) return image;
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(QRectF(image.rect()), frame,
            QRectF((view.x() - bounds.x()) * sx, (view.y() - bounds.y()) * sy, view.width() * sx, view.height() * sy));
        return image;
    };
    constexpr int kSamples[] = {1, 4, 8};
    int count = reduceMotion || !enabled ? 1 : kSamples[int(motionBlur)];
    const double frameMs = std::isfinite(outputFps) && outputFps > 0 ? 1000.0 / outputFps : 1000.0 / 30.0;
    const int part = timeline.segmentAt(sourceMs);
    const auto output = timeline.isEmpty() ? std::optional<qint64>(sourceMs) : timeline.sourceToOutput(sourceMs);
    const auto timeAt = [&](double delta) {
        if (!output) return sourceMs;
        if (timeline.isEmpty()) return std::max<qint64>(0, clipEndMs > 0
            ? std::min(clipEndMs - 1, qRound64(*output + delta)) : qRound64(*output + delta));
        if (part < 0) return sourceMs;
        const qint64 begin = timeline.segmentOutputStartMs(part);
        const qint64 end = begin + timeline.segments()[size_t(part)].outputLengthMs();
        return timeline.outputToSource(std::clamp(qRound64(*output + delta), begin, std::max(begin, end - 1)));
    };
    if (count > 1) {
        const QRectF before = viewport(timeAt(-frameMs / 2)), after = viewport(timeAt(frameMs / 2));
        const auto distance = [&](QPointF a, QPointF b) {
            const QPointF d = b - a;
            return std::hypot(d.x() * frame.width() / shown.width(), d.y() * frame.height() / shown.height());
        };
        if (std::max(distance(before.topLeft(), after.topLeft()), distance(before.bottomRight(), after.bottomRight()))
            < kBlurMinPixels) count = 1;
    }
    if (count == 1) {
        if (shown != bounds) {
            const auto format = frame.format();
            frame = sampleImage(shown).convertToFormat(format);
        }
        return;
    }
    // Eight premultiplied samples fit in uint16_t, including alpha. This is
    // one shared model for panning, scaling and simultaneous motion.
    std::vector<std::array<quint16, 4>> sums(size_t(frame.width()) * frame.height());
    for (int i = 0; i < count; ++i) {
        const double offset = (double(i) / (count - 1) - 0.5) * frameMs * kShutterFraction;
        const QImage sample = sampleImage(viewport(timeAt(offset)));
        if (sample.isNull()) { frame = {}; return; }
        for (int y = 0; y < sample.height(); ++y) {
            const auto* pixels = reinterpret_cast<const QRgb*>(sample.constScanLine(y));
            auto* row = sums.data() + size_t(y) * sample.width();
            for (int x = 0; x < sample.width(); ++x) {
                row[x][0] += qRed(pixels[x]); row[x][1] += qGreen(pixels[x]);
                row[x][2] += qBlue(pixels[x]); row[x][3] += qAlpha(pixels[x]);
            }
        }
    }
    QImage result(frame.size(), QImage::Format_ARGB32_Premultiplied);
    if (result.isNull()) { frame = {}; return; }
    for (int y = 0; y < result.height(); ++y) {
        auto* pixels = reinterpret_cast<QRgb*>(result.scanLine(y));
        const auto* row = sums.data() + size_t(y) * result.width();
        for (int x = 0; x < result.width(); ++x)
            pixels[x] = qRgba((row[x][0] + count / 2) / count, (row[x][1] + count / 2) / count,
                             (row[x][2] + count / 2) / count, (row[x][3] + count / 2) / count);
    }
    frame = result.convertToFormat(frame.format());
}

void ZoomTrack::prepareFollow(const CursorTimeline& cursor, const CursorEffectSettings& settings,
                              const TimelineMap& timeline, const QRectF& bounds)
{
    m_follow.assign(m_segments.size(), {});
    m_followBounds = bounds;
    m_followButtons.clear();
    if (bounds.isEmpty() || cursor.samples.empty() || reduceMotion) return;
    // 125 Hz, in output time. Event times are additional exact knots.
    constexpr qint64 kStepMs = 8;
    constexpr double kInnerFraction = 0.6;
    constexpr double kSpringFrequency = 12.0;
    constexpr double kMaxWidthsPerSecond = 1.0;
    constexpr double kEdgeFraction = 0.08;
    CursorEffectRenderer renderer(cursor, settings, timeline.isEmpty() ? -1 : timeline.sourceEndMs());
    if (!timeline.isEmpty()) renderer.setTimeline(timeline);
    for (const auto& event : cursor.buttons) {
        if (!event.synthetic && bounds.contains(QPointF(event.position)) && renderer.pointerPosition(event.tMs))
            m_followButtons.push_back(event);
    }
    for (size_t index = 0; index < m_segments.size(); ++index) {
        const auto& segment = m_segments[index];
        if (!segment.follow) continue;
        const QSizeF size = bounds.size() / segment.scale;
        QPointF center = frameFor(segment.followStart.value_or(segment.focus), segment.scale, bounds).center();
        QPointF velocity;
        QPointF target = center;
        auto& path = m_follow[index];
        const qint64 from = timeline.isEmpty() ? segment.startMs : std::max(segment.startMs, timeline.sourceStartMs());
        const qint64 to = timeline.isEmpty() ? segment.endMs : std::min(segment.endMs, timeline.sourceEndMs());
        if (from > to) continue;
        std::vector<qint64> times{segment.startMs, from, to, segment.endMs};
        for (qint64 t = from; t < to;) {
            const double speed = timeline.isEmpty() ? 1.0 : timeline.speedAt(t);
            const qint64 step = std::max<qint64>(1, qRound64(kStepMs * speed));
            t = step >= to - t ? to : t + step;
            times.push_back(t);
        }
        for (const auto& event : cursor.buttons) {
            if (!event.synthetic && event.tMs >= segment.startMs && event.tMs <= segment.endMs)
                times.push_back(event.tMs);
        }
        for (const auto& part : timeline.segments()) {
            if (part.startMs > segment.startMs && part.startMs <= segment.endMs) {
                times.push_back(part.startMs - 1);
                times.push_back(part.startMs);
            }
        }
        std::sort(times.begin(), times.end());
        times.erase(std::unique(times.begin(), times.end()), times.end());
        qint64 previousMs = segment.startMs;
        // The part the camera last moved in. It only advances with a usable
        // pointer, so a cut crossed while the pointer is hidden still resets.
        int cameraPart = timeline.segmentAt(segment.startMs);
        for (const qint64 ms : times) {
            const int part = timeline.segmentAt(ms);
            const auto pointer = renderer.pointerPosition(ms);
            if (pointer && (timeline.isEmpty() || part >= 0)) {
                // A cut starts a fresh camera; deleted time never contributes momentum.
                if (part != cameraPart && part >= 0
                    && (cameraPart < 0 || timeline.segments()[size_t(part)].startMs
                        > timeline.segments()[size_t(cameraPart)].endMs)) {
                    center = frameFor(*pointer, segment.scale, bounds).center();
                    velocity = {};
                    target = center;
                }
                const auto axisTarget = [](double p, double c, double extent, double previous) {
                    const double half = extent * kInnerFraction / 2.0;
                    return p < c - half ? p + half : p > c + half ? p - half : previous;
                };
                target.setX(axisTarget(pointer->x(), center.x(), size.width(), target.x()));
                target.setY(axisTarget(pointer->y(), center.y(), size.height(), target.y()));
                // Snap the target at the source edges, before integration so
                // edge attraction cannot multiply the camera's speed.
                if (target.x() != center.x() && pointer->x() <= bounds.left() + bounds.width() * kEdgeFraction)
                    target.setX(bounds.left() + size.width() / 2);
                if (target.x() != center.x() && pointer->x() >= bounds.right() - bounds.width() * kEdgeFraction)
                    target.setX(bounds.right() - size.width() / 2);
                if (target.y() != center.y() && pointer->y() <= bounds.top() + bounds.height() * kEdgeFraction)
                    target.setY(bounds.top() + size.height() / 2);
                if (target.y() != center.y() && pointer->y() >= bounds.bottom() - bounds.height() * kEdgeFraction)
                    target.setY(bounds.bottom() - size.height() / 2);
                target = frameFor(target, segment.scale, bounds).center();
                const double speed = timeline.isEmpty() ? 1.0 : timeline.speedAt(ms);
                double dt = double(ms - previousMs) / (1000.0 * std::max(0.01, speed));
                if (!timeline.isEmpty()) {
                    const auto output = timeline.sourceToOutputMicroseconds(ms * 1000);
                    const auto previousOutput = timeline.sourceToOutputMicroseconds(previousMs * 1000);
                    if (output && previousOutput) dt = double(*output - *previousOutput) / 1000000.0;
                }
                const double decay = std::exp(-kSpringFrequency * dt);
                const QPointF displacement = center - target;
                const QPointF impulse = velocity + kSpringFrequency * displacement;
                QPointF next = target + (displacement + impulse * dt) * decay;
                velocity = (velocity - kSpringFrequency * impulse * dt) * decay;
                // A moving target can reverse suddenly; do not carry momentum past it.
                const auto clampAxis = [](double value, double from, double to) {
                    return std::clamp(value, std::min(from, to), std::max(from, to));
                };
                next.setX(clampAxis(next.x(), center.x(), target.x()));
                next.setY(clampAxis(next.y(), center.y(), target.y()));
                const QPointF delta = next - center;
                const double distance = std::hypot(delta.x(), delta.y());
                const double limit = size.width() * kMaxWidthsPerSecond * dt;
                if (distance > limit && distance > 0) next = center + delta * (limit / distance);
                center = frameFor(next, segment.scale, bounds).center();
                // Exact button knots override the speed limit for visibility.
                auto eventAt = std::lower_bound(cursor.buttons.begin(), cursor.buttons.end(), ms,
                    [](const CursorButtonEvent& event, qint64 t) { return event.tMs < t; });
                for (; eventAt != cursor.buttons.end() && eventAt->tMs == ms; ++eventAt) {
                    const auto& event = *eventAt;
                    if (event.synthetic || !bounds.contains(QPointF(event.position))) continue;
                    const double margin = kMarginFraction * std::min(bounds.width(), bounds.height());
                    const QRectF required = QRectF(QPointF(event.position), QSizeF(1, 1))
                        .adjusted(-margin, -margin, margin, margin).intersected(bounds);
                    QRectF view = frameFor(center, segment.scale, bounds);
                    if (required.left() < view.left()) view.moveLeft(required.left());
                    if (required.right() > view.right()) view.moveRight(required.right());
                    if (required.top() < view.top()) view.moveTop(required.top());
                    if (required.bottom() > view.bottom()) view.moveBottom(required.bottom());
                    const QPointF corrected = frameFor(view.center(), segment.scale, bounds).center();
                    if (corrected != center) { velocity = {}; target = corrected; }
                    center = corrected;
                }
                cameraPart = part;
            }
            path.push_back({ms, center});
            previousMs = ms;
        }
    }
}

QJsonObject ZoomTrack::toJson() const
{
    QJsonArray segments;
    for (const auto& s : m_segments) {
        QJsonObject segment{{QStringLiteral("start"), s.startMs},
                                    {QStringLiteral("end"), s.endMs},
                                    {QStringLiteral("scale"), s.scale},
                                    {QStringLiteral("x"), s.focus.x()},
                                    {QStringLiteral("y"), s.focus.y()},
                                    {QStringLiteral("manual"), s.manual},
                                    {QStringLiteral("follow"), s.follow}};
        if (s.followStart) {
            segment.insert(QStringLiteral("followStart"),
                           QJsonObject{{QStringLiteral("x"), s.followStart->x()},
                                       {QStringLiteral("y"), s.followStart->y()}});
        }
        if (s.stationaryScale) segment.insert(QStringLiteral("stationaryScale"), *s.stationaryScale);
        segments.append(segment);
    }
    return {{QStringLiteral("enabled"), enabled},
            {QStringLiteral("reduceMotion"), reduceMotion},
            {QStringLiteral("motionBlur"), int(motionBlur)},
            {QStringLiteral("segments"), segments}};
}

ZoomTrack ZoomTrack::fromJson(const QJsonObject& json)
{
    ZoomTrack track;
    track.enabled = json.value(QStringLiteral("enabled")).toBool(true);
    track.motionBlur = MotionBlur(std::clamp(json.value(QStringLiteral("motionBlur")).toInt(), 0, 2));
    track.reduceMotion = json.value(QStringLiteral("reduceMotion")).toBool(false);
    std::vector<ZoomSegment> segments;
    for (const auto& value : json.value(QStringLiteral("segments")).toArray()) {
        const QJsonObject o = value.toObject();
        ZoomSegment s;
        s.startMs = o.value(QStringLiteral("start")).toInteger();
        s.endMs = o.value(QStringLiteral("end")).toInteger();
        s.scale = o.value(QStringLiteral("scale")).toDouble(2.0);
        s.focus = QPointF(o.value(QStringLiteral("x")).toDouble(), o.value(QStringLiteral("y")).toDouble());
        s.follow = o.value(QStringLiteral("follow")).toBool(false);
        s.manual = o.value(QStringLiteral("manual")).toBool();
        const QJsonObject start = o.value(QStringLiteral("followStart")).toObject();
        if (start.value(QStringLiteral("x")).isDouble() && start.value(QStringLiteral("y")).isDouble()) {
            const QPointF point(start.value(QStringLiteral("x")).toDouble(), start.value(QStringLiteral("y")).toDouble());
            if (std::isfinite(point.x()) && std::isfinite(point.y())) s.followStart = point;
        }
        const QJsonValue stationary = o.value(QStringLiteral("stationaryScale"));
        if (stationary.isDouble() && std::isfinite(stationary.toDouble()))
            s.stationaryScale = std::clamp(stationary.toDouble(), ZoomTrack::kMinScale, ZoomTrack::kMaxScale);
        segments.push_back(s);
    }
    track.setSegments(std::move(segments));
    return track;
}

bool ZoomTrack::operator==(const ZoomTrack& o) const
{
    return enabled == o.enabled && reduceMotion == o.reduceMotion && motionBlur == o.motionBlur && m_segments == o.m_segments;
}

AutoZoomInteractionKind classifyAutoZoomInteraction(const AutoZoomInteractionMetrics& m)
{
    using Kind = AutoZoomInteractionKind;
    constexpr double kDoubleClickMs = 350, kDoubleClickDistance = 0.04;
    constexpr double kSelectionHoldMs = 200, kSelectionDistance = 0.03, kSelectionAspect = 1.8;
    constexpr double kMenuDistance = 0.03, kMenuAspect = 1.5, kFieldDistance = 0.02;
    const std::pair<bool, Kind> rules[] = {
        {m.previousPressMs >= 0 && m.previousPressMs <= kDoubleClickMs && m.previousPressDistance <= kDoubleClickDistance, Kind::DoubleClick},
        {m.heldMs >= kSelectionHoldMs && std::abs(m.drag.x()) > kSelectionDistance
            && std::abs(m.drag.x()) > kSelectionAspect * std::abs(m.drag.y()), Kind::Selection},
        {m.downDistance > kMenuDistance && m.pathY > kMenuAspect * m.pathX, Kind::Menu},
        {m.samples < 3 || m.maxDistance < kFieldDistance, Kind::Field},
        {true, Kind::Click}
    };
    return std::find_if(std::begin(rules), std::end(rules), [](const auto& rule) { return rule.first; })->second;
}

static std::vector<ZoomSegment> buildAutoZoom(const CursorTimeline& timeline,
                                             const AutoZoomOptions& options, bool& insufficientScale);

AutoZoomResult generateAutoZoomResult(const CursorTimeline& timeline, const AutoZoomOptions& options)
{
    if (!timeline.info.clicksAvailable) return {{}, AutoZoomResult::Reason::NoClickData};
    if (!std::isfinite(options.maxScale) || options.maxScale < kMinUsefulScale)
        return {{}, AutoZoomResult::Reason::InsufficientScale};
    bool insufficientScale = false;
    auto segments = buildAutoZoom(timeline, options, insufficientScale);
    const auto reason = !segments.empty() ? AutoZoomResult::Reason::Added
        : insufficientScale ? AutoZoomResult::Reason::InsufficientScale : AutoZoomResult::Reason::NoVisibleInteractions;
    return {std::move(segments), reason};
}

std::vector<ZoomSegment> generateAutoZoom(const CursorTimeline& timeline, const AutoZoomOptions& options)
{
    bool insufficientScale = false;
    return buildAutoZoom(timeline, options, insufficientScale);
}

static std::vector<ZoomSegment> buildAutoZoom(const CursorTimeline& timeline,
                                             const AutoZoomOptions& options, bool& insufficientScale)
{
    const QRectF bounds = options.bounds;
    if (bounds.isEmpty() || timeline.buttons.empty() || !std::isfinite(options.maxScale)) return {};
    const double shortSide = std::min(bounds.width(), bounds.height());
    const auto usable = [&](qint64 t, const QPoint& p) {
        const CursorSample* sample = timeline.sampleAt(t);
        return (!sample || sample->visible) && bounds.contains(QPointF(p));
    };

    const auto partAt = [&](qint64 t) { return options.timeline.isEmpty() ? 0 : options.timeline.segmentAt(t); };
    const auto speedAt = [&](qint64 t) { return options.timeline.isEmpty() ? 1.0 : options.timeline.speedAt(t); };
    const auto sourceDelta = [&](qint64 t, qint64 outputMs) { return qint64(std::llround(outputMs * speedAt(t))); };
    const auto samePart = [&](qint64 a, qint64 b) { return partAt(a) >= 0 && partAt(a) == partAt(b); };
    const auto clampToPart = [&](qint64 anchor, qint64 t) {
        if (options.timeline.isEmpty()) return options.durationMs > 0
            ? std::clamp<qint64>(t, 0, options.durationMs) : std::max<qint64>(0, t);
        const auto& part = options.timeline.segments()[size_t(partAt(anchor))];
        return std::clamp(t, part.startMs, part.endMs);
    };

    // Presses (with a drag's whole path while held) on visible, on-frame positions.
    std::vector<Interaction> interactions;
    const auto& buttons = timeline.buttons;
    for (size_t i = 0; i < buttons.size(); ++i) {
        const auto& press = buttons[i];
        if (!press.down || press.synthetic || !usable(press.tMs, press.position) || partAt(press.tMs) < 0) continue;
        Interaction interaction{press.tMs, press.tMs, QRectF(QPointF(press.position), QSizeF(1, 1))};
        for (size_t j = i + 1; j < buttons.size(); ++j) {
            if (buttons[j].button == press.button && !buttons[j].down) {
                interaction.endMs = clampToPart(press.tMs, buttons[j].tMs);
                break;
            }
        }
        auto heldSample = std::lower_bound(timeline.samples.begin(), timeline.samples.end(), press.tMs,
            [](const CursorSample& sample, qint64 ms) { return sample.tMs < ms; });
        for (; heldSample != timeline.samples.end() && heldSample->tMs <= interaction.endMs; ++heldSample) {
            if (usable(heldSample->tMs, heldSample->position)) interaction.area |= QRectF(QPointF(heldSample->position), QSizeF(1, 1));
        }
        // Classify on the visible path in this kept part. Distances are
        // fractions of the crop's short side; all windows are output time.
        constexpr qint64 kDoubleClickMs = 350;
        constexpr qint64 kLookStartMs = 100;
        constexpr qint64 kLookEndMs = 2000;
        constexpr double kFieldLeaveDistance = 0.03;
        constexpr qint64 kFieldHoldMaxMs = 6000;
        constexpr qint64 kTypingJoinMs = 1500;
        constexpr qint64 kTypingGapMs = 1200;
        constexpr qint64 kTypingHoldMaxMs = 15000;
        const QPointF origin(press.position);
        const auto distance = [](QPointF p) { return std::hypot(p.x(), p.y()); };
        AutoZoomInteractionMetrics metrics;
        for (size_t j = i; j-- > 0;) {
            const auto& prior = buttons[j];
            if (press.tMs - prior.tMs > sourceDelta(press.tMs, kDoubleClickMs)) break;
            if (prior.down && !prior.synthetic && prior.button == press.button && samePart(prior.tMs, press.tMs)
                && usable(prior.tMs, prior.position)) {
                metrics.previousPressMs = double(press.tMs - prior.tMs) / speedAt(press.tMs);
                metrics.previousPressDistance = distance(QPointF(prior.position) - origin) / shortSide;
                break;
            }
        }
        const auto* release = timeline.sampleAt(interaction.endMs);
        const QPointF drag = release ? QPointF(release->position) - origin : QPointF();
        double dx = 0, dy = 0, maxDistance = 0;
        QPointF last = origin;
        int count = 0;
        const qint64 lookStart = press.tMs + sourceDelta(press.tMs, kLookStartMs);
        const qint64 lookEnd = clampToPart(press.tMs, press.tMs + sourceDelta(press.tMs, kLookEndMs));
        auto sample = std::lower_bound(timeline.samples.begin(), timeline.samples.end(), lookStart,
            [](const CursorSample& s, qint64 t) { return s.tMs < t; });
        for (; sample != timeline.samples.end() && sample->tMs < lookEnd; ++sample) {
            if (!usable(sample->tMs, sample->position)) continue;
            const QPointF pos(sample->position);
            dx += std::abs(pos.x() - last.x()); dy += std::abs(pos.y() - last.y());
            maxDistance = std::max(maxDistance, distance(pos - origin));
            last = pos; ++count;
        }
        metrics.heldMs = double(interaction.endMs - press.tMs) / speedAt(press.tMs);
        metrics.drag = drag / shortSide;
        metrics.downDistance = (last.y() - origin.y()) / shortSide;
        metrics.pathX = dx / shortSide; metrics.pathY = dy / shortSide;
        metrics.maxDistance = maxDistance / shortSide; metrics.samples = count;
        using Kind = AutoZoomInteractionKind;
        const Kind kind = classifyAutoZoomInteraction(metrics);
        interaction.menu = kind == Kind::Menu;
        if (kind == Kind::Field) {
            const qint64 limit = clampToPart(press.tMs, press.tMs + sourceDelta(press.tMs, kFieldHoldMaxMs));
            qint64 leave = limit;
            auto it = std::lower_bound(timeline.samples.begin(), timeline.samples.end(), interaction.endMs,
                [](const CursorSample& s, qint64 t) { return s.tMs < t; });
            for (; it != timeline.samples.end() && it->tMs < limit; ++it) {
                if (!it->visible || distance(QPointF(it->position) - origin) > kFieldLeaveDistance * shortSide) {
                    leave = it->tMs; break;
                }
            }
            // The ordinary tail is added below; a field extends only while
            // the pointer actually remains there, never beyond its departure.
            interaction.endMs = std::max(interaction.endMs, leave - sourceDelta(press.tMs, kTailMs));
        }
        qint64 lastKey = press.tMs;
        bool typing = false;
        for (const qint64 key : options.keyTimes) {
            if (key < press.tMs) continue;
            if (!samePart(press.tMs, key) || key - lastKey > sourceDelta(press.tMs, typing ? kTypingGapMs : kTypingJoinMs)) break;
            const qint64 limit = press.tMs + sourceDelta(press.tMs, kTypingHoldMaxMs - kTailMs);
            lastKey = std::min(key, limit);
            typing = true;
            if (key >= limit) break;
        }
        if (typing) interaction.endMs = std::max(interaction.endMs, lastKey);
        interactions.push_back(interaction);
    }

    // Group nearby interactions.
    std::vector<Interaction> groups;
    for (const auto& interaction : interactions) {
        if (!groups.empty()) {
            auto& group = groups.back();
            const QPointF delta = interaction.area.center() - group.area.center();
            constexpr qint64 kMenuFollowMs = 4000;
            if (samePart(group.startMs, interaction.startMs)
                && ((interaction.startMs - group.endMs <= sourceDelta(group.startMs, kClusterGapMs)
                     && std::hypot(delta.x(), delta.y()) <= kClusterRadius * shortSide)
                    || (group.menu && interaction.startMs - group.endMs <= sourceDelta(group.startMs, kMenuFollowMs)))) {
                group.endMs = std::max(group.endMs, interaction.endMs);
                group.area |= interaction.area;
                group.menu = interaction.menu;
                continue;
            }
        }
        groups.push_back(interaction);
    }

    const double maxScale = std::clamp(options.maxScale, ZoomTrack::kMinScale, ZoomTrack::kMaxScale);
    // The scale at which a fixed view shows the whole area, with a margin.
    const auto scaleFor = [&](const QRectF& area) {
        const QRectF framed = area.adjusted(-kMarginFraction * shortSide, -kMarginFraction * shortSide,
                                            kMarginFraction * shortSide, kMarginFraction * shortSide);
        return std::clamp(std::min(bounds.width() / framed.width(), bounds.height() / framed.height()),
                          ZoomTrack::kMinScale, maxScale);
    };

    // Follow through nearby bursts instead of pumping out and back in.
    std::vector<Interaction> merged;
    for (auto group : groups) {
        group.firstEventMs = group.startMs;
        group.lastEventMs = group.endMs;
        group.startMs = clampToPart(group.firstEventMs, group.startMs - sourceDelta(group.firstEventMs, kLeadMs));
        group.endMs = clampToPart(group.firstEventMs, std::max(group.endMs + sourceDelta(group.firstEventMs, kTailMs),
            group.firstEventMs + sourceDelta(group.firstEventMs, kMinClusterMs)));
        if (!merged.empty() && samePart(group.firstEventMs, merged.back().firstEventMs)
            && group.startMs - merged.back().endMs < sourceDelta(group.firstEventMs, kCooldownMs)) {
            merged.back().endMs = std::max(merged.back().endMs, group.endMs);
            merged.back().area |= group.area;
            merged.back().lastEventMs = std::max(merged.back().lastEventMs, group.lastEventMs);
            continue;
        }
        merged.push_back(group);
    }

    const auto build = [&](const std::vector<Interaction>& planned) {
        std::vector<ZoomSegment> segments;
        for (const auto& group : planned) {
            const double scale = maxScale;
            if (scale < kMinUsefulScale) continue; // the requested magnification is not useful
            ZoomSegment segment;
            segment.startMs = group.startMs;
            segment.endMs = group.endMs;
            segment.scale = scale;
            segment.follow = true;
            const auto* initial = timeline.sampleAt(segment.startMs);
            // The following camera tracks the pointer at full scale; holding
            // still, the view must fit the whole (possibly merged) area.
            const double stationary = scaleFor(group.area);
            if (stationary < scale) segment.stationaryScale = stationary;
            segment.focus = frameFor(group.area.center(), stationary, bounds).center();
            if (initial) segment.followStart = frameFor(QPointF(initial->position), scale, bounds).center();
            segments.push_back(segment);
        }
        ZoomTrack normalized;
        normalized.setSegments(std::move(segments));
        normalized.prepareFollow(timeline, {}, options.timeline, bounds);
        return normalized;
    };

    // Check the moving viewport at each interaction, including transitions.
    // If a transition hides one, join neighbouring groups and retry. Each
    // pass removes a group, so this ends.
    for (;;) {
        const ZoomTrack track = build(merged);
        const auto hidden = std::find_if(interactions.cbegin(), interactions.cend(), [&](const Interaction& i) {
            for (const auto& event : timeline.buttons) {
                if (event.synthetic || !samePart(event.tMs, i.startMs) || event.tMs < i.startMs || event.tMs > i.endMs
                    || !usable(event.tMs, event.position)) continue;
                if (!track.viewportAt(event.tMs, bounds).contains(QPointF(event.position))) return true;
            }
            return false;
        });
        if (hidden == interactions.cend()) return track.segments();
        if (merged.size() <= 1) { insufficientScale = true; return {}; }
        auto owner = std::find_if(merged.begin(), merged.end(), [&](const Interaction& group) {
            return hidden->startMs >= group.firstEventMs && hidden->startMs <= group.lastEventMs;
        });
        if (owner == merged.end()) { insufficientScale = true; return {}; }
        const auto first = owner == merged.begin() || !samePart((owner - 1)->firstEventMs, owner->firstEventMs) ? owner : owner - 1;
        const auto last = owner + 1 == merged.end() || !samePart((owner + 1)->firstEventMs, owner->firstEventMs) ? owner : owner + 1;
        if (first == last) return track.segments(); // never merge through a cut
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
