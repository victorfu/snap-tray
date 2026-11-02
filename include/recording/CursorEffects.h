#pragma once

#include "recording/CursorTimeline.h"

#include <QJsonObject>
#include <QPointF>
#include <QRect>
#include <QSize>

class QPainter;

// Post-recording pointer effects (#160): one recipe drawn identically by the
// preview and by every export, from the pointer sidecar (#159). Rendering is
// a pure function of the timeline, the recipe and the source time, so seeking
// to a time always draws the same frame, whatever was played before.
namespace SnapTray {

struct CursorEffectSettings {
    enum class Smoothing { Off, Low, Medium, High };

    bool showCursor = true;
    double scale = 1.0;                  // 1.0 = recorded size
    Smoothing smoothing = Smoothing::Off; // off by default: never move the real pointer path unasked
    bool clickRipple = false;

    static constexpr double kMinScale = 0.5;
    static constexpr double kMaxScale = 4.0;

    // True when exports must draw anything (otherwise the plain fast paths apply).
    bool drawsAnything() const { return showCursor || clickRipple; }
    bool operator==(const CursorEffectSettings& o) const;
    bool operator!=(const CursorEffectSettings& o) const { return !(*this == o); }

    QJsonObject toJson() const;
    static CursorEffectSettings fromJson(const QJsonObject& json); // missing keys keep defaults
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
    QRect sourceCrop;  // source video pixels; empty = whole frame
    QSize sourceSize;  // the recording's frame size
    QSize outputSize;  // pixels of the image being drawn on

    QPointF toOutput(const QPointF& sourcePoint) const;
    double scale() const; // output pixels per source pixel (x)
};

class CursorEffectRenderer {
public:
    // Ripple timing and the windows around clicks where smoothing yields to
    // the real position.
    static constexpr qint64 kRippleDurationMs = 450;
    static constexpr qint64 kClickSnapMs = 120;
    static constexpr double kDragSmoothingFactor = 0.25; // drags follow the hand more closely

    CursorEffectRenderer(const CursorTimeline& timeline, const CursorEffectSettings& settings);

    // Where the pointer is drawn at sourceMs, in source video pixels; nullopt
    // when hidden or before the first sample.
    std::optional<QPointF> pointerPosition(qint64 sourceMs) const;
    // Draws the effects for sourceMs onto an image already showing that
    // frame (cropped and scaled as `mapping` says).
    void paint(QPainter& painter, qint64 sourceMs, const CursorFrameMapping& mapping) const;
    void paint(QImage& frame, qint64 sourceMs, const CursorFrameMapping& mapping) const;

    // Time constant of the smoothing filter for a setting (0 = off).
    static double smoothingTauMs(CursorEffectSettings::Smoothing smoothing);

private:
    QPointF rawPosition(qint64 sourceMs) const;
    QPointF smoothedPosition(qint64 sourceMs, double tauMs) const;
    qint64 distanceToButtonEvent(qint64 sourceMs) const;
    void paintRipples(QPainter& painter, qint64 sourceMs, const CursorFrameMapping& mapping) const;
    void paintPointer(QPainter& painter, qint64 sourceMs, const CursorFrameMapping& mapping) const;

    const CursorTimeline& m_timeline;
    CursorEffectSettings m_settings;
};

} // namespace SnapTray
