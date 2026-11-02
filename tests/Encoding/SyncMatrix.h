#pragma once

#include <QImage>
#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

// End-to-end recording verification (#163): synthetic frames and audio go
// through the recording's real timing pieces (SessionClock, FramePacer,
// TimestampedPcmMixer) into the platform encoder and muxer, with injected
// faults; the written file is then decoded and checked frame by frame.
// Passing means the decoded media is right, not that a state machine ran.
namespace SyncMatrix {

enum class Audio { None, Microphone, SystemAudio, Mixed };

// Deliberate pipeline bugs: the analysis must catch each of them.
// BrokenPacer stands for a FramePacer bug (it paces at half the frame rate
// wherever FramePacer is used), not a pipeline wiring mistake.
// ShiftMicrophone and DropSystemAudio break one source of a mixed recording.
enum class Mutation {
    None, IgnorePause, ShiftAudio, TruncateTail, ReorderTimestamps, DropRepeats, BrokenPacer,
    ShiftMicrophone, DropSystemAudio
};

struct Pause {
    qint64 atMs = 0;     // wall time (arrival clock) the pause begins
    qint64 lengthMs = 0;
};

struct Scenario {
    QString name;
    QSize size{640, 360};
    int fps = 30;
    qint64 durationMs = 3000;   // capture span (source clock), pauses included
    Audio audio = Audio::Microphone;

    // Source timestamps and callback arrival are controlled separately.
    qint64 sourceJitterMs = 0;  // capture timestamps wobble by up to this
    qint64 arrivalDelayMs = 0;  // every frame arrives this late...
    int burstEvery = 0;         // ...and every Nth frame releases a held burst
    int dropEvery = 0;          // capture drops every Nth frame
    int duplicateEvery = 0;     // every Nth frame is delivered twice
    int backwardEvery = 0;      // every Nth frame repeats an older timestamp
    qint64 staticFromMs = -1;   // screen unchanged (same content) in this span
    qint64 staticToMs = -1;
    QVector<Pause> pauses;
    qint64 audioGapFromMs = -1; // one audio source delivers nothing here
    qint64 audioGapToMs = -1;
    qint64 audioClockOffsetMs = 0; // audio engine started this much later
    double audioDriftPpm = 0;      // audio clock runs fast (+) or slow (-)
    qint64 slowEncoderMs = 0;      // the encoder is unavailable this long every 2 s (simulated clock)

    Mutation mutation = Mutation::None;
    QJsonObject toJson() const;
};

struct Result {
    QString scenario;
    QStringList failures; // what is wrong with the decoded media
    QStringList skipped;  // checks that could not run, and why (never a pass)
    QJsonObject expected;
    QJsonObject measured;
    QJsonObject cost;     // wall/CPU time, memory, file size

    bool passed() const { return failures.isEmpty(); }
    // `mustPass`: a detect-* row succeeds when the analysis fails; reports
    // keep both so comparisons judge the test, not the raw media.
    QJsonObject toJson(const Scenario& scenario, bool mustPass) const;
};

// Runs one scenario in `workDir`. The analysis tolerance for A/V offset is
// one frame interval plus kAvToleranceMs.
constexpr qint64 kAvToleranceMs = 25;
Result run(const Scenario& scenario, const QString& workDir);

// The synthetic frame (a 32-bit frame ID barcode across the top) and its
// reader, exposed for the barcode round-trip test.
QImage makeFrame(const QSize& size, int id, bool flash, int motion);
int readId(const QImage& frame);
// Whether a flash (and its audio pulse) of `lengthMs` starting on the
// schedule covers wall time `wallMs`.
bool flashActiveAt(qint64 wallMs, qint64 lengthMs);

// Writes <dir>/<scenario>.json when SNAPTRAY_SYNC_REPORT_DIR is set.
void writeReport(const Scenario& scenario, const Result& result, bool mustPass);

} // namespace SyncMatrix
