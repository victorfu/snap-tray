#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace SnapTray {
// Streaming WSOLA for interleaved stereo PCM16 at 48 kHz. No platform or Qt
// dependencies. Analysis windows are independent of caller block boundaries.
class AudioTimeStretch {
public:
    static constexpr int kSampleRate = 48000;
    static constexpr int kChannels = 2;
    static constexpr int kWindowMs = 30;
    static constexpr int kSearchMs = 10;
    static constexpr int kWindowFrames = kSampleRate * kWindowMs / 1000;
    static constexpr int kHopFrames = kWindowFrames / 2;
    static constexpr int kSearchFrames = kSampleRate * kSearchMs / 1000;

    explicit AudioTimeStretch(double speed = 1.0) { reset(speed); }
    void reset(double speed);
    void push(const std::int16_t* samples, std::size_t frames);
    void finish();
    std::size_t availableFrames() const;
    std::size_t read(std::int16_t* samples, std::size_t frames);

private:
    void process();
    std::int16_t inputAt(std::int64_t frame, int channel) const;
    std::int64_t bestStart(std::int64_t expected) const;
    double m_speed = 1.0;
    bool m_finished = false;
    bool m_seeded = false;
    std::int64_t m_inputStart = 0, m_inputEnd = 0, m_outputFrames = 0, m_window = 0;
    std::size_t m_readSamples = 0;
    std::vector<std::int16_t> m_input, m_output, m_tail;
};
} // namespace SnapTray
