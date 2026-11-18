#pragma once

#include "recording/CursorTimeline.h"
#include "recording/CursorEffects.h"

#include <QJsonArray>
#include <QPointF>
#include <QRectF>

#include <optional>
#include <vector>

// Zoom and pan after recording (#161): segments that frame part of the
// recording, generated from pointer activity or added by hand, and a camera
// that turns them into a viewport per frame. Everything is a pure function of
// the segments and the time, so preview, seek and export always agree.
namespace SnapTray {

enum class AutoZoomInteractionKind { DoubleClick, Selection, Menu, Field, Click };
// Distances are normalized by the crop's short side; durations are output ms.
struct AutoZoomInteractionMetrics {
    double previousPressMs = -1, previousPressDistance = 0;
    double heldMs = 0;
    QPointF drag;
    double downDistance = 0, pathX = 0, pathY = 0, maxDistance = 0;
    int samples = 0;
};
AutoZoomInteractionKind classifyAutoZoomInteraction(const AutoZoomInteractionMetrics& metrics);

struct ZoomSegment {
    qint64 startMs = 0; // fully zoomed from here...
    qint64 endMs = 0;   // ...to here (source time); transitions lie outside
    double scale = 2.0; // 1 = the whole picture
    QPointF focus;      // centre, in source video pixels
    bool manual = false; // added or changed by the user: regenerating keeps it
    bool follow = false; // legacy and manual segments keep their fixed focus
    std::optional<QPointF> followStart; // optional lead-in camera; focus remains the stationary fallback
    // Optional cap on scale while the camera holds still (Less motion, Follow
    // off), so a fixed view at focus still fits every interaction the
    // following camera tracks at the full scale.
    std::optional<double> stationaryScale;

    bool operator==(const ZoomSegment& o) const;
    bool operator!=(const ZoomSegment& o) const { return !(*this == o); }
};

class ZoomTrack {
public:
    enum class MotionBlur { Off, Low, High };
    static constexpr double kMinScale = 1.0;
    static constexpr double kMaxScale = 4.0;
    static constexpr double kReducedMotionMaxScale = 1.5;
    static constexpr qint64 kTransitionMs = 600;
    static constexpr qint64 kReducedMotionTransitionMs = 900;
    static constexpr qint64 kMinSegmentMs = 500;
    // The shortest blend between neighbouring segments, so touching ones
    // never cut from one frame to the next.
    static constexpr qint64 kMinBlendMs = 300;

    MotionBlur motionBlur = MotionBlur::Off;
    static constexpr double kShutterFraction = 0.5;
    static constexpr double kBlurMinPixels = 1.5;
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
    QRectF loopViewportAt(qint64 sourceMs, const QRectF& bounds, const TimelineMap& timeline,
                          bool loopBack, qint64 clipEndMs = -1) const;

    // Resamples already masked/cropped source pixels; pointers and overlays
    // are drawn afterwards. The output interval defines the shutter.
    void resample(QImage& frame, qint64 sourceMs, const QRectF& bounds, const TimelineMap& timeline,
                  bool loopBack = false, double outputFps = 30.0, qint64 clipEndMs = -1) const;

    // Derived data only: rebuilt when geometry, cursor settings or edits change.
    void prepareFollow(const CursorTimeline& cursor, const CursorEffectSettings& settings,
                       const TimelineMap& timeline, const QRectF& bounds);

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
    TimelineMap timeline;
    std::vector<qint64> keyTimes; // source times only, no key contents
};

struct AutoZoomResult {
    enum class Reason { Added, NoClickData, NoVisibleInteractions, NoFreeSlots, InsufficientScale };
    std::vector<ZoomSegment> segments;
    Reason reason = Reason::NoVisibleInteractions;
};
// Zoom segments from clicks and drags in the pointer timeline: nearby
// interactions are grouped into following segments, and short gaps merged
// so the view does not pump in and out. Hidden or off-frame activity
// is ignored. Empty without clicks.
AutoZoomResult generateAutoZoomResult(const CursorTimeline& timeline, const AutoZoomOptions& options);

std::vector<ZoomSegment> generateAutoZoom(const CursorTimeline& timeline, const AutoZoomOptions& options);

} // namespace SnapTray
