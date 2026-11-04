#pragma once

#include <QString>
#include <QtGlobal>

#include <vector>

namespace SnapTray {

// Sizes, presentation times and keyframes of the first video track's samples
// in an MP4, read from the container alone (no decoding): the sample table in
// `moov` (as AVFoundation writes it) and any movie fragments (`moof`, as the
// crash-safe Media Foundation and FFmpeg recordings are written). Times follow
// the composition offsets and the edit list, as the preview's source time
// does. Used to tell how many bytes a time range of a recording holds, which
// an average over the whole file cannot (PR #213 review).
class Mp4VideoSamples
{
public:
    static Mp4VideoSamples read(const QString& path);

    // The bytes a kept range [startMs, endMs) costs at the recording's
    // quality: its own samples, and, when it starts mid-GOP, the keyframe its
    // first sample was predicted from (the export starts with a whole frame).
    struct RangeBytes {
        qint64 frames = 0;          // the samples starting in the range
        qint64 leadingKeyframe = 0; // the keyframe before, for a mid-GOP start
        qint64 firstKeyframe = 0;   // the first of frames, when it is a keyframe
    };
    RangeBytes rangeBytes(qint64 startMs, qint64 endMs) const;

    bool isValid() const { return !m_startUs.empty(); }
    int sampleCount() const { return int(m_startUs.size()); }
    qint64 totalBytes() const { return m_cumulativeBytes.empty() ? 0 : m_cumulativeBytes.back(); }
    // Where the last sample ends: how far the table covers the recording.
    qint64 endMs() const;
    // Shorthands for rangeBytes().
    qint64 bytesBetween(qint64 startMs, qint64 endMs) const;
    qint64 bytesToEncode(qint64 startMs, qint64 endMs) const;
    qint64 leadingKeyframeBytes(qint64 startMs, qint64 endMs) const;

    // For tests and the parser: samples must be added in presentation order.
    void append(qint64 startUs, qint64 bytes, bool keyframe = true);
    void setEndUs(qint64 endUs) { m_endUs = endUs; }

private:
    std::vector<qint64> m_startUs;
    std::vector<size_t> m_keyframes; // sample indexes, ascending
    std::vector<qint64> m_cumulativeBytes; // size + 1 entries once valid
    qint64 m_endUs = 0; // where the last sample ends
};

} // namespace SnapTray
