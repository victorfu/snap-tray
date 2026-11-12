#pragma once

#include <algorithm>

namespace snaptray::scrollcapture::probe {

// Keep a pending/active measurement exclusive even while Qt processes events.
class InspectState {
public:
    explicit InspectState(int initialLevel) : m_level(initialLevel) {}
    int level() const { return m_level; }
    bool busy() const { return m_busy; }
    void updateLevels(int count)
    {
        m_levels = std::max(0, count);
        if (m_level >= m_levels) m_level = 0;
    }
    void cycle(int direction)
    {
        if (!m_busy && m_levels > 0)
            m_level = (m_level + direction + m_levels) % m_levels;
    }
    bool beginMeasurement()
    {
        if (m_busy || m_levels == 0) return false;
        m_busy = true;
        return true;
    }
    void endMeasurement() { m_busy = false; }

private:
    int m_level = 0;
    int m_levels = 0;
    bool m_busy = false;
};
}
