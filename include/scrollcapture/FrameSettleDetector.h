#pragma once

#include <QImage>
#include <QVector>
#include <QtGlobal>

#include <optional>

namespace snaptray::scrollcapture {

// Decides when a scroll area has stopped changing after a scroll was sent.
//
// Feed it the frame captured just before scrolling (start), then every frame
// captured afterwards with its time since the scroll was sent. Two criteria
// are tracked side by side:
// - strict: two consecutive frames are bit-identical;
// - relaxed: consecutive frames differ only in a few small tiles (a blinking
//   caret, a spinner), so the content itself has stopped.
// Detection ends on a strict settle or when the timeout passes. A strict
// settle also needs the identical run to last `minStableMs`, because captures
// faster than the application's frame rate can repeat one intermediate frame.
// Until the frame differs from the start frame by more than a few small tiles,
// nothing counts as settled before the application has had `noMotionGraceMs`
// to react, so a slow application (or one whose caret blinked first) is not
// mistaken for one that did not move.
class FrameSettleDetector
{
public:
    struct Config {
        qint64 timeoutMs = 1500;
        qint64 noMotionGraceMs = 300;
        // An identical run must span at least this long (about three frames
        // at 60 Hz) before it counts as a strict settle.
        qint64 minStableMs = 50;
        // Consecutive identical pairs needed for a strict settle.
        int strictPairs = 1;
        int tileSize = 32;
        // A pair is relaxed-stable when at most this many tiles changed, and
        // they cover at most `relaxedMaxChangedFraction` of all tiles.
        int relaxedMaxChangedTiles = 4;
        double relaxedMaxChangedFraction = 0.02;
    };

    enum class Outcome {
        Pending,
        Settled,
        TimedOut,
    };

    struct Result {
        Outcome outcome = Outcome::Pending;
        // Time of the first frame of the identical run that settled.
        std::optional<qint64> strictSettleMs;
        // Time of the frame that confirmed the strict settle.
        std::optional<qint64> strictConfirmedMs;
        // Time from which every following pair stayed relaxed-stable.
        std::optional<qint64> relaxedSettleMs;
        // Whether any frame differed from the start frame by more than a few
        // small tiles.
        bool motionObserved = false;
        int frames = 0;
        // Tiles that changed in the last pair.
        int lastChangedTiles = 0;
    };

    FrameSettleDetector();
    explicit FrameSettleDetector(const Config& config);

    void start(const QImage& startFrame);
    // Returns the state after this frame. Frames passed after the result is
    // final are ignored.
    Result addFrame(const QImage& frame, qint64 elapsedMs);
    const Result& result() const { return m_result; }
    const QImage& lastFrame() const { return m_previous; }

    // Tiles whose pixels differ between two frames of the same size.
    // Frames of different sizes count every tile as changed.
    static int countChangedTiles(const QImage& a, const QImage& b, int tileSize, int* totalTiles = nullptr);

private:
    bool withinRelaxedLimits(int changedTiles, int totalTiles) const;
    bool isMinorChange(const QImage& a, const QImage& b) const;

    Config m_config;
    Result m_result;
    QImage m_start;
    QVector<quint64> m_startHashes;
    QImage m_previous;
    QVector<quint64> m_previousHashes;
    qint64 m_previousMs = 0;
    int m_identicalPairs = 0;
    std::optional<qint64> m_identicalRunStartMs;
    std::optional<qint64> m_relaxedSinceMs;
};

const char* settleOutcomeName(FrameSettleDetector::Outcome outcome);

} // namespace snaptray::scrollcapture
