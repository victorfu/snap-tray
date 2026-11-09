#pragma once

#include "recording/KeyTimeline.h"
#include "recording/TimelineEdit.h"

#include <QJsonObject>
#include <QRectF>
#include <QSize>
#include <QStringList>

class QPainter;
class QImage;

// Shortcut hints drawn over the exported video (#187), from the key sidecar.
// Like the pointer effects, a pure function of source time: a seek always
// shows the same hint. Keys pressed close together are shown together, and
// the same shortcut repeated shows once with a count ("Ctrl + Z ×3").
namespace SnapTray {

struct KeyOverlaySettings {
    enum class Position { BottomCenter, BottomLeft, BottomRight, TopCenter };
    enum class Size { Small, Medium, Large };
    enum class Style { Dark, Light };

    bool enabled = true;
    Position position = Position::BottomCenter;
    Size size = Size::Medium;
    Style style = Style::Dark;
    qint64 displayMs = 1500; // how long a hint stays after its last key

    static constexpr qint64 kDisplayChoices[] = {1000, 1500, 2500};
    static constexpr qint64 kMergeGapMs = 800;  // keys closer than this share a hint
    static constexpr int kMaxTokens = 4;        // the newest ones in a hint
    static constexpr qint64 kFadeInMs = 80;
    static constexpr qint64 kFadeOutMs = 200;

    bool operator==(const KeyOverlaySettings& o) const;
    bool operator!=(const KeyOverlaySettings& o) const { return !(*this == o); }
    QJsonObject toJson() const;
    static KeyOverlaySettings fromJson(const QJsonObject& json);
};

class KeyOverlayRenderer {
public:
    KeyOverlayRenderer(const KeyTimeline& timeline, const KeyOverlaySettings& settings);
    // Parts shown (#185): nothing from a cut part, no hint across a cut.
    void setTimeline(const TimelineMap& timeline);

    // What the hint at sourceMs says ("Ctrl + C", "Ctrl + Z ×3"...), and how
    // opaque it is; empty when nothing shows.
    QStringList tokensAt(qint64 sourceMs) const;
    double opacityAt(qint64 sourceMs) const;

    // Draws onto a picture of `pictureSize` shown at `target` (the frame
    // itself for exports, a rectangle in view coordinates for the preview).
    void paint(QPainter& painter, qint64 sourceMs, const QRectF& target, const QSize& pictureSize) const;
    void paint(QImage& frame, qint64 sourceMs) const;

private:
    struct StrokeHint {
        QString label;
        size_t groupStart;
        size_t runStart;
    };

    // The strokes of the hint showing at sourceMs: [first, last] indexes.
    bool groupAt(qint64 sourceMs, size_t* first, size_t* last) const;
    qint64 partStart(qint64 ms) const;

    const KeyTimeline& m_timeline;
    KeyOverlaySettings m_settings;
    std::vector<TimelineSegment> m_kept;
    // Per-stroke group and repeat-run boundaries keep lookups independent
    // of the length of a continuous typing run.
    std::vector<StrokeHint> m_hints;
};

} // namespace SnapTray
