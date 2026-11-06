#pragma once

#include "recording/CursorTimeline.h"
#include "recording/TimelineEdit.h"
#include "recording/SpringPath.h"

#include <QJsonObject>
#include <QPointF>
#include <QRect>
#include <QSize>

#include <vector>

class QPainter;

// Post-recording pointer effects (#160): one recipe drawn identically by the
// preview and by every export, from the pointer sidecar (#159). Rendering is
// a pure function of the timeline, the recipe and the source time, so seeking
// to a time always draws the same frame, whatever was played before.
namespace SnapTray {

// A span of source time where the pointer is not drawn (#186).
struct CursorHiddenRange {
    qint64 startMs = 0;
    qint64 endMs = 0; // exclusive
    bool contains(qint64 ms) const { return ms >= startMs && ms < endMs; }
    bool operator==(const CursorHiddenRange& o) const { return startMs == o.startMs && endMs == o.endMs; }
};

struct CursorEffectSettings {
    enum class Smoothing { Off, Low, Medium, High };
    enum class MotionBlur { Off, Low, Medium, High };

    bool showCursor = true;
    double scale = 1.0;                  // 1.0 = recorded size
    Smoothing smoothing = Smoothing::Off; // off by default: never move the real pointer path unasked
    bool clickRipple = false;
    // #186: every one off by default, so older recipes draw as before.
    qint64 idleHideMs = 0;     // fade the pointer out after this long without movement; 0 = never
    MotionBlur motionBlur = MotionBlur::Off;
    qint64 stillEndMs = 0;     // the pointer stops moving this long before the clip ends; 0 = off
    // In time order, not overlapping. Specific to one recording: never part
    // of the remembered preferences (see preferences()).
    std::vector<CursorHiddenRange> hiddenRanges;

    static constexpr double kMinScale = 0.5;
    static constexpr double kMaxScale = 4.0;
    static constexpr qint64 kMaxIdleHideMs = 60000;
    static constexpr qint64 kMaxStillEndMs = 10000;
    static constexpr qint64 kMinHiddenRangeMs = 100;

    // True when exports must draw anything (otherwise the plain fast paths apply).
    bool drawsAnything() const { return showCursor || clickRipple; }
    bool operator==(const CursorEffectSettings& o) const;
    bool operator!=(const CursorEffectSettings& o) const { return !(*this == o); }

    QJsonObject toJson() const;
    static CursorEffectSettings fromJson(const QJsonObject& json); // missing keys keep defaults
    // The look worth carrying to the next recording: these settings without
    // anything tied to this recording's timeline (hidden ranges).
    CursorEffectSettings preferences() const;
    bool hiddenAt(qint64 sourceMs) const;
    // Sorts, drops ranges shorter than kMinHiddenRangeMs and merges overlaps.
    static std::vector<CursorHiddenRange> normalizedRanges(std::vector<CursorHiddenRange> ranges);
};

// What a recording's sidecar allows; options it cannot support are disabled.
struct CursorEffectAvailability {
    bool cursor = false; // a usable timeline whose pointer is not already in the video
    bool clicks = false; // ripples need recorded clicks
    static CursorEffectAvailability of(const CursorTimeline* timeline);
};

// Where source video pixels land in the output: crop, then scale to the
// output size (letterboxing is the caller's; output fills the crop).
struct CursorFrameMapping {
    QRectF sourceCrop; // source video pixels (a zoom viewport is fractional); empty = whole frame
    QSize sourceSize;  // the recording's frame size
    QSize outputSize;  // pixels of the image being drawn on

    QPointF toOutput(const QPointF& sourcePoint) const;
    double scale() const; // output pixels per source pixel (x)
};

class CursorEffectRenderer {
public:
    // Ripple timing and source-time anticipation of a button press.
    static constexpr qint64 kRippleDurationMs = 450;
    static constexpr qint64 kClickLookaheadMs = 450;
    // Idle hiding: how long the fade out (and the fade back in, which starts
    // this long before the next movement) takes.
    static constexpr qint64 kIdleFadeMs = 250;
    // Motion blur: ghost images spread over this much time before the frame
    // at High (Low and Medium use a fraction), at fixed offsets.
    static constexpr qint64 kMaxBlurWindowMs = 48;
    static constexpr int kBlurSamples = 6;

    // clipEndMs: the source time the exported clip ends at, for "still end";
    // negative when unknown (the effect is then off).
    CursorEffectRenderer(const CursorTimeline& timeline, const CursorEffectSettings& settings,
                         qint64 clipEndMs = -1);

    // The parts of the recording that are shown (#185): nothing reaches across
    // a cut (no smoothing from, ripples of or blur toward cut-out time).
    // Without one the whole recording is one part.
    void setTimeline(const TimelineMap& timeline);

    // Where the pointer is drawn at sourceMs, in source video pixels; nullopt
    // when hidden or before the first sample.
    std::optional<QPointF> pointerPosition(qint64 sourceMs) const;
    // How visible the pointer is at sourceMs with idle hiding (1 = fully).
    double pointerOpacity(qint64 sourceMs) const;
    // Draws the effects for sourceMs onto an image already showing that
    // frame (cropped and scaled as `mapping` says).
    void paint(QPainter& painter, qint64 sourceMs, const CursorFrameMapping& mapping) const;
    void paint(QImage& frame, qint64 sourceMs, const CursorFrameMapping& mapping) const;

private:
    QPointF rawPosition(qint64 sourceMs) const;
    void rebuildSpringPaths();
    QPointF springPosition(qint64 sourceMs) const;
    struct PartPath { qint64 startMs; qint64 endMs; SpringPath path; };
    std::vector<PartPath> m_paths;
    // The time the pointer is shown as being at: frozen near the clip end.
    qint64 effectiveTime(qint64 sourceMs) const;
    std::optional<QPointF> positionAt(qint64 ms) const;
    // Time since the last movement or button event (at or before ms), and
    // until the next one; capped at `limit`.
    qint64 idleBefore(qint64 ms, qint64 limit) const;
    qint64 idleAfter(qint64 ms, qint64 limit) const;
    double blurWindowMs() const;
    void drawPointerAt(QPainter& painter, const QPointF& at, const CursorShape* shape, double scale) const;
    qint64 distanceToButtonEvent(qint64 sourceMs) const;
    void paintRipples(QPainter& painter, qint64 sourceMs, const CursorFrameMapping& mapping) const;
    void paintPointer(QPainter& painter, qint64 sourceMs, const CursorFrameMapping& mapping) const;

    const CursorTimeline& m_timeline;
    CursorEffectSettings m_settings;
    qint64 m_clipEndMs = -1;
    std::vector<TimelineSegment> m_kept; // empty = everything
    TimelineMap m_map;                   // the same parts, for output times
    // The start and end of the kept part showing ms (ms itself when cut out).
    qint64 partStart(qint64 ms) const;
    qint64 partEnd(qint64 ms) const;
};

} // namespace SnapTray
