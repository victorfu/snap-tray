#pragma once
#include "recording/AudioTimeStretch.h"
#include "recording/TimelineEdit.h"
#include <QByteArray>
#include <functional>

namespace SnapTray {
// Source callbacks mix gain/mute/noise reduction first, then WSOLA stretches
// that mix. Each kept part starts a fresh stretcher; no history crosses cuts.
class AudioSegmentReader {
public:
    using Source = std::function<QByteArray(qint64, int)>;
    static constexpr int kSeamFadeMs = 5;
    explicit AudioSegmentReader(Source source) : m_source(std::move(source)) {}
    void begin(const TimelineSegment& part);
    QByteArray read(int frames);
    // Source position derived from the total output, rounded only once.
    qint64 sourceFrame() const;
private:
    Source m_source;
    TimelineSegment m_part;
    AudioTimeStretch m_stretch;
    qint64 m_inputFrame = 0, m_outputFrame = 0;
    bool m_finished = false;
};
} // namespace SnapTray
