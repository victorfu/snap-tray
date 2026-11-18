#include "recording/AudioTimeStretch.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace SnapTray {
void AudioTimeStretch::reset(double speed)
{
    m_speed = std::isfinite(speed) && speed > 0 ? std::clamp(speed, 0.1, 16.0) : 1.0;
    m_finished = m_seeded = false;
    m_inputStart = m_inputEnd = m_outputFrames = m_window = 0;
    m_readSamples = 0;
    m_input.clear(); m_output.clear(); m_tail.clear();
}
void AudioTimeStretch::push(const std::int16_t* samples, std::size_t frames)
{
    if (m_finished || !samples || frames == 0) return;
    m_inputEnd += std::int64_t(frames);
    if (m_speed == 1.0) {
        m_output.insert(m_output.end(), samples, samples + frames * kChannels);
        m_outputFrames += std::int64_t(frames);
        return;
    }
    m_input.insert(m_input.end(), samples, samples + frames * kChannels);
    process();
}
void AudioTimeStretch::finish()
{
    if (m_finished) return;
    m_finished = true;
    if (m_speed != 1.0) process();
}
std::size_t AudioTimeStretch::availableFrames() const
{
    return (m_output.size() - m_readSamples) / kChannels;
}
std::size_t AudioTimeStretch::read(std::int16_t* samples, std::size_t frames)
{
    if (!samples) return 0;
    frames = std::min(frames, availableFrames());
    if (frames) std::memcpy(samples, m_output.data() + m_readSamples, frames * kChannels * sizeof(std::int16_t));
    m_readSamples += frames * kChannels;
    if (m_readSamples >= std::size_t(kWindowFrames * kChannels)) {
        m_output.erase(m_output.begin(), m_output.begin() + std::ptrdiff_t(m_readSamples));
        m_readSamples = 0;
    }
    return frames;
}
std::int16_t AudioTimeStretch::inputAt(std::int64_t frame, int channel) const
{
    if (frame < m_inputStart || frame >= m_inputEnd) return 0;
    return m_input[std::size_t(frame - m_inputStart) * kChannels + channel];
}
std::int64_t AudioTimeStretch::bestStart(std::int64_t expected) const
{
    const auto last = std::max<std::int64_t>(0, m_inputEnd - kWindowFrames);
    const auto lower = std::max(m_inputStart, expected - kSearchFrames);
    const auto upper = std::min(last, expected + kSearchFrames);
    if (upper <= lower) return std::clamp(expected, m_inputStart, last);
    const auto center = std::clamp(expected, lower, upper);
    // Coarse correlation followed by sample-accurate refinement. Both stereo
    // channels contribute independently, so opposite-phase channels do not cancel.
    constexpr int kCandidateStride = 16, kRefinementStride = 4, kCorrelationStride = 8;
    const auto score = [&](std::int64_t at) {
        double dot = 0, energy = 0;
        // Candidates are bounded to complete windows above. Direct indexing
        // avoids repeating inputAt's streaming boundary checks in the hot loop.
        const auto* input = m_input.data() + (at - m_inputStart) * kChannels;
        for (int i = 0; i < kHopFrames * kChannels; i += kCorrelationStride * kChannels) {
            const double left = input[i], right = input[i + 1];
            dot += left * m_tail[std::size_t(i)] + right * m_tail[std::size_t(i + 1)];
            energy += left * left + right * right;
        }
        // Prefer the intended position for silence or equally good periods.
        return dot / std::sqrt(std::max(1.0, energy)) - std::abs(double(at - expected)) * 1e-7;
    };
    auto best = center;
    double highest = score(best);
    for (auto at = lower; at <= upper; at += kCandidateStride) {
        const double value = score(at);
        if (value > highest) { highest = value; best = at; }
    }
    const auto coarse = best;
    for (auto at = std::max(lower, coarse - kCandidateStride); at <= std::min(upper, coarse + kCandidateStride); at += kRefinementStride) {
        const double value = score(at);
        if (value > highest) { highest = value; best = at; }
    }
    const auto refined = best;
    for (auto at = std::max(lower, refined - kRefinementStride); at <= std::min(upper, refined + kRefinementStride); ++at) {
        const double value = score(at);
        if (value > highest) { highest = value; best = at; }
    }
    return best;
}
void AudioTimeStretch::process()
{
    const auto target = std::int64_t(std::llround(double(m_inputEnd) / m_speed));
    while (m_outputFrames < target) {
        if (!m_finished && target - m_outputFrames < kHopFrames) break;
        const auto expected = std::int64_t(std::llround(double(m_window) * kHopFrames * m_speed));
        if (!m_finished && m_inputEnd < (m_seeded ? expected + kSearchFrames : 0) + kWindowFrames) break;
        const auto at = m_seeded ? bestStart(expected) : 0;
        const int count = int(std::min<std::int64_t>(kHopFrames, target - m_outputFrames));
        for (int i = 0; i < count; ++i) {
            const double weight = double(i) / kHopFrames;
            for (int c = 0; c < kChannels; ++c) {
                const double next = inputAt(at + i, c);
                const double value = m_seeded ? m_tail[std::size_t(i * kChannels + c)] * (1.0 - weight) + next * weight : next;
                m_output.push_back(std::int16_t(std::clamp(std::lround(value), -32768l, 32767l)));
            }
        }
        m_tail.resize(kHopFrames * kChannels);
        for (int i = 0; i < kHopFrames; ++i)
            for (int c = 0; c < kChannels; ++c)
                m_tail[std::size_t(i * kChannels + c)] = inputAt(at + kHopFrames + i, c);
        m_seeded = true;
        ++m_window;
        m_outputFrames += count;
        // Retain the next search window only. Working memory stays bounded
        // when the caller drains output as it supplies input blocks.
        const auto next = std::int64_t(std::llround(double(m_window) * kHopFrames * m_speed));
        const auto before = std::clamp(next - kSearchFrames, m_inputStart,
                                      std::max(m_inputStart, m_inputEnd - kWindowFrames));
        if (before - m_inputStart >= kWindowFrames) {
            m_input.erase(m_input.begin(), m_input.begin() + (before - m_inputStart) * kChannels);
            m_inputStart = before;
        }
    }
}
} // namespace SnapTray
