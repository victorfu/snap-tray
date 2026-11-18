#include "recording/AudioSegmentReader.h"
#include "recording/AudioTracks.h"
#include <QtEndian>
#include <algorithm>
#include <cmath>

namespace SnapTray {
void AudioSegmentReader::begin(const TimelineSegment& part)
{
    m_part = part;
    m_stretch.reset(part.speed);
    m_inputFrame = m_outputFrame = 0;
    m_finished = false;
}
qint64 AudioSegmentReader::sourceFrame() const
{
    return m_part.startMs * AudioTracks::kSampleRate / 1000
        + qint64(std::llround(m_outputFrame * m_part.speed));
}
QByteArray AudioSegmentReader::read(int frames)
{
    if (frames <= 0) return {};
    constexpr int rate = AudioTracks::kSampleRate, bytes = AudioTracks::kBytesPerFrame;
    const bool normal = qFuzzyCompare(m_part.speed, 1.0);
    const qint64 outputEnd = m_part.outputLengthMs() * rate / 1000;
    if (frames > outputEnd - m_outputFrame) return {};
    QByteArray pcm(frames * bytes, '\0');
    if (normal) {
        pcm = m_source(m_part.startMs * rate / 1000 + m_outputFrame, frames);
        if (pcm.size() != frames * bytes) return {};
    } else if (!m_part.isSilent()) {
        const qint64 sourceEnd = m_part.lengthMs() * rate / 1000;
        constexpr int kInputChunkFrames = 4800;
        while (m_stretch.availableFrames() < size_t(frames) && !m_finished) {
            const int count = int(std::min<qint64>(kInputChunkFrames, sourceEnd - m_inputFrame));
            if (count > 0) {
                const auto input = m_source(m_part.startMs * rate / 1000 + m_inputFrame, count);
                if (input.size() != count * bytes) return {};
                std::vector<std::int16_t> samples(size_t(count) * AudioTracks::kChannels);
                for (size_t i = 0; i < samples.size(); ++i)
                    samples[i] = qFromLittleEndian<qint16>(input.constData() + i * 2);
                m_stretch.push(samples.data(), size_t(count));
                m_inputFrame += count;
            }
            if (m_inputFrame >= sourceEnd) { m_stretch.finish(); m_finished = true; }
        }
        std::vector<std::int16_t> samples(size_t(frames) * AudioTracks::kChannels);
        const size_t read = m_stretch.read(samples.data(), size_t(frames));
        for (size_t i = 0; i < read * AudioTracks::kChannels; ++i)
            qToLittleEndian<qint16>(samples[i], pcm.data() + i * 2);
        // The timeline rounds once to milliseconds. A fractional WSOLA tail
        // is trimmed/zero-filled here, never carried into the next segment.
    }
    if (!m_part.isSilent()) {
        constexpr qint64 fadeFrames = kSeamFadeMs * rate / 1000;
        for (int i = 0; i < frames; ++i) {
            const qint64 at = m_outputFrame + i;
            const double gain = std::clamp(double(std::min(at, outputEnd - 1 - at)) / fadeFrames, 0.0, 1.0);
            if (gain >= 1.0) continue;
            for (int c = 0; c < AudioTracks::kChannels; ++c) {
                char* sample = pcm.data() + i * bytes + c * 2;
                qToLittleEndian<qint16>(qint16(std::lround(qFromLittleEndian<qint16>(sample) * gain)), sample);
            }
        }
    }
    m_outputFrame += frames;
    return pcm;
}
} // namespace SnapTray
