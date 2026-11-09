#pragma once

#include <QPointF>
#include <algorithm>
#include <cmath>
#include <vector>

namespace SnapTray {

// An immutable, source-time path after construction. Extra knots (for example
// button events) can sit between the regular simulation steps.
struct SpringPath {
    struct Knot { double ms; QPointF position; };
    std::vector<Knot> knots;

    QPointF at(double ms) const
    {
        if (knots.empty()) return {};
        auto next = std::upper_bound(knots.begin(), knots.end(), ms,
            [](double t, const Knot& k) { return t < k.ms; });
        if (next == knots.begin()) return next->position;
        const auto& before = *(next - 1);
        if (next == knots.end()) return before.position;
        const double fraction = (ms - before.ms) / (next->ms - before.ms);
        return before.position + (next->position - before.position) * fraction;
    }
};

struct SpringParameters {
    double tension;
    double friction;
    double mass;
    double leadSeconds() const { return friction / tension; }
};

// Exact integration of a critically damped spring with a linearly moving
// target. All callers use friction = 2*sqrt(tension*mass). Unlike explicit
// Euler this stays stable for stiff drag springs and fractional event steps.
inline void advanceSpring(QPointF& position, QPointF& velocity,
                          const QPointF& from, const QPointF& to,
                          double seconds, const SpringParameters& p)
{
    if (seconds <= 0.0) return;
    const double omega = std::sqrt(p.tension / p.mass);
    const QPointF targetVelocity = (to - from) / seconds;
    const QPointF offset = position - from + targetVelocity * p.leadSeconds();
    const QPointF rate = velocity - targetVelocity + offset * omega;
    const double decay = std::exp(-omega * seconds);
    const QPointF old = position;
    position = to - targetVelocity * p.leadSeconds() + (offset + rate * seconds) * decay;
    velocity = targetVelocity + (rate - (offset + rate * seconds) * omega) * decay;
    // A moving target can reverse while the spring still has momentum. Stop
    // at the target instead of ringing past it, independently on each axis.
    const auto constrain = [](double oldValue, double target, double& value, double& speed) {
        const double bounded = std::clamp(value, std::min(oldValue, target), std::max(oldValue, target));
        if (bounded != value) { value = bounded; speed = 0.0; }
    };
    constrain(old.x(), to.x(), position.rx(), velocity.rx());
    constrain(old.y(), to.y(), position.ry(), velocity.ry());
}

} // namespace SnapTray
