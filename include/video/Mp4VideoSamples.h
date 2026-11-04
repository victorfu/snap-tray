#pragma once

#include <QString>
#include <QtGlobal>

#include <vector>

namespace SnapTray {

// Sizes and decode times of the first video track's samples in an MP4, read
// from the container alone (no decoding): the sample table in `moov` (as
// AVFoundation writes it) and any movie fragments (`moof`, as the crash-safe
// Media Foundation and FFmpeg recordings are written). Used to tell how many
// bytes a time range of a recording holds, which an average over the whole
// file cannot (PR #213 review).
class Mp4VideoSamples
{
public:
    static Mp4VideoSamples read(const QString& path);

    bool isValid() const { return !m_startUs.empty(); }
    int sampleCount() const { return int(m_startUs.size()); }
    qint64 totalBytes() const { return m_cumulativeBytes.empty() ? 0 : m_cumulativeBytes.back(); }
    // Bytes of the samples starting in [startMs, endMs).
    qint64 bytesBetween(qint64 startMs, qint64 endMs) const;

    // For tests and the parser: samples must be added in decode order.
    void append(qint64 startUs, qint64 bytes);

private:
    std::vector<qint64> m_startUs;
    std::vector<qint64> m_cumulativeBytes; // size + 1 entries once valid
};

} // namespace SnapTray
