#pragma once

#include "capture/IAudioCaptureEngine.h"

#include <QByteArray>
#include <QVector>

#include <memory>

namespace SnapTray::Audio {

enum class Source {
    Microphone,
    SystemAudio,
};

enum class ByteOrder {
    LittleEndian,
    BigEndian,
};

struct Pcm16Format {
    int sampleRate = 0;
    int channels = 0;
    ByteOrder byteOrder = ByteOrder::LittleEndian;
    bool signedSamples = true;
    bool interleaved = true;

    bool isValid() const;
    int bytesPerFrame() const;

    friend bool operator==(const Pcm16Format& lhs, const Pcm16Format& rhs)
    {
        return lhs.sampleRate == rhs.sampleRate
            && lhs.channels == rhs.channels
            && lhs.byteOrder == rhs.byteOrder
            && lhs.signedSamples == rhs.signedSamples
            && lhs.interleaved == rhs.interleaved;
    }

    friend bool operator!=(const Pcm16Format& lhs, const Pcm16Format& rhs)
    {
        return !(lhs == rhs);
    }
};

struct InputChunk {
    QByteArray pcm;
    // Timestamp of the first PCM frame on the shared recording-active timeline.
    qint64 startTimeNs = 0;
    Pcm16Format format;
};

struct OutputChunk {
    QByteArray pcm;
    qint64 startFrame = 0;

    bool isEmpty() const { return pcm.isEmpty(); }
    qint64 timestampNs() const;
};

/**
 * Thread-safe canonical PCM normalizer and two-source timeline mixer.
 *
 * Inputs must be signed, little-endian, interleaved PCM16. Each source keeps
 * independent streaming resampler state. Output is always interleaved PCM16
 * at 48 kHz stereo. Mono is duplicated, stereo is preserved, and sources with
 * more than two channels use a layout-agnostic arithmetic mean duplicated to
 * left/right. Missing timeline coverage is silence; long wholly uncovered
 * spans may be represented by a timestamp gap after the configured silence
 * budget. Coincident samples are added with int16 saturation.
 */
class TimestampedPcmMixer final
{
public:
    static constexpr int kOutputSampleRate = 48000;
    static constexpr int kOutputChannels = 2;
    static constexpr int kOutputBitsPerSample = 16;

    struct Config {
        bool microphoneEnabled = false;
        bool systemAudioEnabled = false;
        qint64 maxSkewFrames = 12000;             // 250 ms at 48 kHz
        qint64 maxPendingFramesPerSource = 24000; // 500 ms at 48 kHz
        qsizetype maxPendingBytesPerSource = 2 * 1024 * 1024;
        qint64 maxFutureLeadFrames = 24000;        // 500 ms at 48 kHz
        qint64 maxMaterializedSilenceFramesPerCall = 24000; // 500 ms per API call
        int outputChunkFrames = 480;              // 10 ms at 48 kHz
    };

    enum class ResultCode {
        Accepted,
        AcceptedWithDrops,
        SourceDisabled,
        MalformedInput,
        UnsupportedFormat,
        TooLate,
        FutureTimestamp,
        Paused,
        Closed,
    };

    struct ProcessResult {
        ResultCode code = ResultCode::Accepted;
        QVector<OutputChunk> output;
        qint64 droppedFrames = 0; // Canonical 48 kHz frames
        qint64 materializedSilenceFrames = 0;
    };

    struct Stats {
        qint64 lateFrames = 0;
        qint64 overlapFrames = 0;
        qint64 overflowFrames = 0;
        qint64 synthesizedGapFrames = 0;
        qint64 skippedSilenceFrames = 0;
        qint64 futureTimestampPackets = 0;
        qsizetype peakPendingBytes = 0;
    };

    TimestampedPcmMixer();
    explicit TimestampedPcmMixer(const Config& config);
    ~TimestampedPcmMixer();

    TimestampedPcmMixer(const TimestampedPcmMixer&) = delete;
    TimestampedPcmMixer& operator=(const TimestampedPcmMixer&) = delete;

    static IAudioCaptureEngine::AudioFormat outputFormat();

    ProcessResult push(Source source,
                       const InputChunk& chunk,
                       qint64 activeTimeNs); // Current pause-adjusted capture horizon
    ProcessResult setSourceEnabled(Source source, bool enabled, qint64 effectiveTimeNs);
    ProcessResult advanceTo(qint64 activeTimeNs);
    ProcessResult pause(qint64 activeTimeNs);
    ProcessResult resume(qint64 activeTimeNs);
    ProcessResult flush(qint64 endTimeNs);
    void reset(qint64 timelineStartNs = 0);

    Stats stats() const;
    qint64 pendingFrames(Source source) const;
    qsizetype pendingBytes(Source source) const;

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace SnapTray::Audio
