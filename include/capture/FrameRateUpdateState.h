#pragma once

#include <QtGlobal>
#include <optional>

namespace SnapTray {

// Used on the capture engine's affinity thread. Native completions carry a
// generation so a stopped stream cannot acknowledge a replacement stream.
class FrameRateUpdateState
{
public:
    struct Update { quint64 generation; int fps; };

    void reset(int fps)
    {
        ++m_generation;
        m_requested = m_applied = fps;
        m_inFlight = false;
    }
    void request(int fps) { m_requested = fps; }
    std::optional<Update> begin()
    {
        if (m_inFlight || m_requested == m_applied) return std::nullopt;
        m_inFlight = true;
        return Update{m_generation, m_requested};
    }
    bool complete(Update update, bool success)
    {
        if (update.generation != m_generation || !m_inFlight) return false;
        m_inFlight = false;
        if (success) m_applied = update.fps;
        return true;
    }

private:
    quint64 m_generation = 0;
    int m_requested = 30;
    int m_applied = 30;
    bool m_inFlight = false;
};

} // namespace SnapTray
