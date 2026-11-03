#pragma once

#include <QJsonObject>
#include <QtGlobal>

#include <cmath>
#include <optional>
#include <vector>

// Cutting parts out of a recording and changing the speed of others (#185).
//
// TimelineEdit is what the user edits: split points in source time, and for
// each part between them whether it is deleted and at what speed it plays.
// The trim (#149) still bounds the whole thing; a recording with no splits
// and no speed change is exactly the old single trim.
//
// TimelineMap is the one mapping between source time (the recording) and
// output time (the export) that the preview, the timeline, every export and
// the effects use. Effects stay functions of source time; the map tells
// them which source times are shown and where each kept part begins.
namespace SnapTray {

struct TimelineSegment {
    qint64 startMs = 0; // source time, inclusive
    qint64 endMs = 0;   // source time, exclusive
    double speed = 1.0; // 2.0 = twice as fast (half as long in the output)
    qint64 lengthMs() const { return endMs - startMs; }
    // Header-only: the platform transcoders use it without linking the core.
    qint64 outputLengthMs() const { return speed > 0.0 ? qint64(std::llround(double(lengthMs()) / speed)) : lengthMs(); }
    bool contains(qint64 sourceMs) const { return sourceMs >= startMs && sourceMs < endMs; }
    bool operator==(const TimelineSegment& o) const
    {
        return startMs == o.startMs && endMs == o.endMs && qFuzzyCompare(speed, o.speed);
    }
};

class TimelineMap {
public:
    TimelineMap() = default;
    // Kept parts in source order; empty ones are dropped.
    explicit TimelineMap(std::vector<TimelineSegment> segments);

    const std::vector<TimelineSegment>& segments() const { return m_segments; }
    bool isEmpty() const { return m_segments.empty(); }
    // One part at normal speed: what a plain trim exports.
    bool isPlainTrim() const;
    qint64 sourceStartMs() const { return m_segments.empty() ? 0 : m_segments.front().startMs; }
    qint64 sourceEndMs() const { return m_segments.empty() ? 0 : m_segments.back().endMs; }
    qint64 outputDurationMs() const { return m_outputStarts.empty() ? 0 : m_outputStarts.back(); }

    // The kept part showing sourceMs, or -1 when it is cut out (or outside).
    int segmentAt(qint64 sourceMs) const;
    qint64 segmentOutputStartMs(int index) const;
    // nullopt for source times that are cut out.
    std::optional<qint64> sourceToOutput(qint64 sourceMs) const;
    // The source time shown at outputMs (clamped to the kept range).
    qint64 outputToSource(qint64 outputMs) const;
    // Where playback continues from sourceMs: itself when kept, else the start
    // of the next kept part (-1 past the last one).
    qint64 nextShownSourceMs(qint64 sourceMs) const;
    double speedAt(qint64 sourceMs) const;

private:
    std::vector<TimelineSegment> m_segments;
    std::vector<qint64> m_outputStarts; // size = segments + 1 (the last is the total)
};

struct TimelineEdit {
    struct Part {
        bool deleted = false;
        double speed = 1.0;
        bool operator==(const Part& o) const { return deleted == o.deleted && qFuzzyCompare(speed, o.speed); }
    };

    std::vector<qint64> splits;     // sorted, strictly increasing, > 0
    std::vector<Part> parts{Part()}; // splits.size() + 1, in time order

    static constexpr double kSpeeds[] = {0.5, 1.0, 1.5, 2.0, 4.0};
    static constexpr qint64 kMinPartMs = 100;

    bool operator==(const TimelineEdit& o) const { return splits == o.splits && parts == o.parts; }
    bool operator!=(const TimelineEdit& o) const { return !(*this == o); }
    // No splits and the single part kept at normal speed.
    bool isEmpty() const;

    int partAt(qint64 sourceMs) const;
    // The source range of a part, unbounded (the trim bounds it later).
    qint64 partStartMs(int index) const;
    qint64 partEndMs(int index, qint64 durationMs) const;

    // False (nothing changes) when it would make a part shorter than
    // kMinPartMs or the point is already a split.
    bool splitAt(qint64 sourceMs, qint64 durationMs);
    // Joins the parts around split `index`; the earlier part's style is kept.
    bool removeSplit(int index);
    // False when it would cross a neighbour or make a part too short.
    bool moveSplit(int index, qint64 sourceMs, qint64 durationMs);
    bool setDeleted(int part, bool deleted);
    bool setSpeed(int part, double speed);

    // The kept parts inside the trim.
    TimelineMap map(qint64 trimStartMs, qint64 trimEndMs) const;

    QJsonObject toJson() const;
    static TimelineEdit fromJson(const QJsonObject& json); // invalid data gives the empty edit
};

} // namespace SnapTray
