#pragma once

#include <QHash>
#include <QtGlobal>

namespace SnapTray {

// Turns a held global hotkey into one press. Auto-repeat activations are
// ignored until the key is released, not merely throttled, so holding
// Pause/Resume never toggles again. A quick second press after a release is
// still ignored for kMinIntervalMs (a double press must not start and cancel
// a recording). If a release is never reported, a pause of kHeldExpiryMs in
// the activations (auto-repeat is far faster) also ends the hold, so the
// hotkey cannot get stuck.
class HotkeyHoldFilter
{
public:
    static constexpr qint64 kMinIntervalMs = 400;
    static constexpr qint64 kHeldExpiryMs = 1000;

    // True when this activation is a new press.
    bool accept(int key, qint64 nowMs)
    {
        auto it = m_state.find(key);
        if (it != m_state.end()) {
            State& state = it.value();
            const bool stillHeld = state.held && nowMs - state.lastActivationMs < kHeldExpiryMs;
            const bool tooSoon = nowMs - state.acceptedMs < kMinIntervalMs;
            state.lastActivationMs = nowMs;
            if (stillHeld || tooSoon) return false;
            state.acceptedMs = nowMs;
            state.held = true;
            return true;
        }
        m_state.insert(key, State{nowMs, nowMs, true});
        return true;
    }

    void release(int key)
    {
        auto it = m_state.find(key);
        if (it != m_state.end()) it->held = false;
    }

private:
    struct State {
        qint64 acceptedMs = 0;       // last activation that counted as a press
        qint64 lastActivationMs = 0; // last activation of any kind (repeats too)
        bool held = false;
    };
    QHash<int, State> m_state;
};

} // namespace SnapTray
