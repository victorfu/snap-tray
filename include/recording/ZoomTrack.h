#pragma once

#include "recording/CursorTimeline.h"
#include "recording/CursorEffects.h"

#include <QJsonArray>
#include <QPointF>
#include <QRectF>

#include <vector>

// Zoom and pan after recording (#161): segments that frame part of the
// recording, generated from pointer activity or added by hand, and a camera
// that turns them into a viewport per frame. Everything is a pure function of
// the segments and the time, so preview, seek and export always agree.
namespace SnapTray {

struct ZoomSegment {
    qint64 startMs = 0; // fully zoomed from here...
    qint64 endMs = 0;   // ...to here (source time); transitions lie outside
    double scale = 2.0; // 1 = the whole picture
    QPointF focus;      // centre, in source video pixels
    bool manual = false; // added or changed by the user: regenerating keeps it
    bool follow = false; // legacy and manual segments keep their fixed focus

    bool operator==(const ZoomSegment& o) const;
    bool operator!=(const ZoomSegment& o) const { return !(*this == o); }
};

class ZoomTrack {
public:
    static constexpr double kMinScale = 1.0;
    static constexpr double kMaxScale = 4.0;
    static constexpr double kReducedMotionMaxScale = 1.5;
    static constexpr qint64 kTransitionMs = 600;
    static constexpr qint64 kReducedMotionTransitionMs = 900;
    static constexpr qint64 kMinSegmentMs = 500;
    // The shortest blend between neighbouring segments, so touching ones
    // never cut from one frame to the next.
    static constexpr qint64 kMinBlendMs = 300;

    bool enabled = true;
    bool reduceMotion = false; // gentler: smaller zoom, slower transitions, no direct pans

    const std::vector<ZoomSegment>& segments() const { return m_segments; }
    bool isEmpty() const { return m_segments.empty(); }
    // Draws anything: enabled with at least one segment.
    bool isActive() const { return enabled && !m_segments.empty(); }

    // Keeps segments sorted, non-overlapping (a later start is pushed after
    // the previous end; too-short leftovers are dropped) and scales legal.
    void setSegments(std::vector<ZoomSegment> segments);
    // Index of the segment covering sourceMs, or -1.
    int segmentAt(qint64 sourceMs) const;

    // The part of `bounds` (the crop, or the whole frame, in source pixels)
    // shown at sourceMs. Always inside bounds, with bounds' aspect ratio.
    QRectF viewportAt(qint64 sourceMs, const QRectF& bounds) const;

    // Derived data only: rebuilt when geometry, cursor settings or edits change.
    void prepareFollow(const CursorTimeline& cursor, const CursorEffectSettings& settings,
                       const TimelineMap& timeline, const QRectF& bounds, bool cursorHasTimelineEdits = true);

    QJsonObject toJson() const;
    static ZoomTrack fromJson(const QJsonObject& json);
    bool operator==(const ZoomTrack& o) const;
    bool operator!=(const ZoomTrack& o) const { return !(*this == o); }

private:
    struct FollowPoint { qint64 ms; QPointF center; };
    std::vector<std::vector<FollowPoint>> m_follow;
    QRectF m_followBounds;
    std::vector<CursorButtonEvent> m_followButtons;
    QPointF focusAt(const ZoomSegment& segment, qint64 ms) const;
    std::vector<ZoomSegment> m_segments;
};

struct AutoZoomOptions {
    QRectF bounds;          // crop or whole frame, source pixels
    qint64 durationMs = 0;  // recording length (segments are clamped to it)
    double maxScale = 2.0;  // never upscale the picture more than this
};

// Zoom segments from clicks and drags in the pointer timeline: nearby
// interactions are grouped into following segments, and short gaps merged
// so the view does not pump in and out. Hidden or off-frame activity
// is ignored. Empty without clicks.
std::vector<ZoomSegment> generateAutoZoom(const CursorTimeline& timeline, const AutoZoomOptions& options);

} // namespace SnapTray
