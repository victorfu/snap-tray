#pragma once

#include <QtGlobal>
#include <cmath>

namespace SnapTray {

// Media time advances independently of frame decoding. Callers supply a
// monotonic wall clock, so waits account for decode time and VFR gaps.
class FramePresentationClock {
public:
    qint64 positionUs(qint64 nowUs) const {
        return m_mediaUs + (m_playing ? qRound64(qMax<qint64>(0, nowUs - m_wallUs) * m_rate) : 0);
    }
    void seek(qint64 mediaUs, qint64 nowUs) { m_mediaUs = qMax<qint64>(0, mediaUs); m_wallUs = nowUs; }
    void setPlaying(bool playing, qint64 nowUs) {
        seek(positionUs(nowUs), nowUs);
        m_playing = playing;
    }
    void setRate(double rate, qint64 nowUs) {
        seek(positionUs(nowUs), nowUs);
        m_rate = qMax(0.01, rate);
    }
    qint64 delayUs(qint64 frameUs, qint64 nowUs) const {
        return m_playing ? qMax<qint64>(0, qint64(std::ceil((frameUs - positionUs(nowUs)) / m_rate))) : 0;
    }
private:
    qint64 m_mediaUs = 0, m_wallUs = 0;
    double m_rate = 1.0;
    bool m_playing = false;
};
} // namespace SnapTray
