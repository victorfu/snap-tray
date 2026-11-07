#include "annotation/AnnotationSnapEngine.h"
#include <algorithm>
#include <cmath>
#include <tuple>

namespace AnnotationSnap {
namespace {
qreal lo(const QRectF& r, int a) { return a ? r.top() : r.left(); }
qreal hi(const QRectF& r, int a) { return a ? r.bottom() : r.right(); }
qreal size(const QRectF& r, int a) { return a ? r.height() : r.width(); }
qreal anchor(const QRectF& r, int a, int k) { return lo(r, a) + size(r, a) * k / 2.0; }
QPointF point(qreal along, qreal across, int a) { return a ? QPointF(across, along) : QPointF(along, across); }
bool valid(const QRectF& r) {
    return r.width() > 0 && r.height() > 0 && std::isfinite(r.left())
        && std::isfinite(r.top()) && std::isfinite(r.right()) && std::isfinite(r.bottom());
}
bool row(const QRectF& a, const QRectF& b, int axis) {
    const int cross = 1 - axis;
    return std::min(hi(a, cross), hi(b, cross)) - std::max(lo(a, cross), lo(b, cross))
        >= Engine::RowOverlap * std::min(size(a, cross), size(b, cross));
}
bool near(const QRectF& a, const QRectF& b, qreal scale) {
    return a.adjusted(-Engine::Neighborhood / scale, -Engine::Neighborhood / scale,
                      Engine::Neighborhood / scale, Engine::Neighborhood / scale).intersects(b);
}
auto score(const Constraint& c) {
    return std::make_tuple(std::abs(c.correction), c.kind, c.first, c.second, c.anchor);
}
}
bool Constraint::sameKey(const Constraint& c) const {
    return axis == c.axis && kind == c.kind && first == c.first && second == c.second && anchor == c.anchor;
}
void Engine::setTargets(std::vector<Target> targets) {
    targets.erase(std::remove_if(targets.begin(), targets.end(), [](const Target& t) {
        return !t.id || !valid(t.bounds);
    }), targets.end());
    std::sort(targets.begin(), targets.end(), [](const Target& a, const Target& b) { return a.id < b.id; });
    m_targets = std::move(targets);
    for (int axis = 0; axis < 2; ++axis) {
        m_axisTargets[axis] = m_targets;
        std::sort(m_axisTargets[axis].begin(), m_axisTargets[axis].end(), [axis](const Target& a, const Target& b) {
            return std::make_tuple(lo(a.bounds, axis), a.id) < std::make_tuple(lo(b.bounds, axis), b.id);
        });
    }
}
Result Engine::query(const QRectF& raw, qreal scale,
                     const std::array<std::optional<Constraint>, 2>& locked,
                     const std::optional<QRectF>& allowed) const {
    Result result;
    if (!valid(raw) || !std::isfinite(scale) || scale <= 0) return result;
    std::array<std::vector<Constraint>, 2> candidates;
    const auto targetFor = [&](Id id) -> const QRectF* {
        for (const auto& t : m_targets) if (t.id == id) return &t.bounds;
        return nullptr;
    };
    const auto spacingValid = [&](const Constraint& c, const QRectF& moved) {
        const auto* a = targetFor(c.first); const auto* b = targetFor(c.second);
        if (!a || !b || !row(moved, *a, c.axis) || !row(moved, *b, c.axis)) return false;
        const qreal left = std::min(lo(*a, c.axis), lo(moved, c.axis));
        const qreal right = std::max(hi(*b, c.axis), hi(moved, c.axis));
        for (const auto& t : m_targets) {
            if (moved.intersects(t.bounds)) return false;
            if (t.id != c.first && t.id != c.second && row(moved, t.bounds, c.axis)
                && hi(t.bounds, c.axis) > left && lo(t.bounds, c.axis) < right) return false;
        }
        return true;
    };
    const auto acceptable = [&](const Constraint& c) {
        const bool retained = locked[c.axis] && c.sameKey(*locked[c.axis]);
        if (std::abs(c.correction) * scale > (retained ? ExitRadius : EnterRadius)) return false;
        QRectF moved = raw.translated(point(c.correction, 0, c.axis));
        if (allowed && !allowed->contains(moved)) return false;
        if (c.kind == Kind::Spacing && !spacingValid(c, moved)) return false;
        return true;
    };
    for (int axis = 0; axis < 2; ++axis) {
        std::vector<const Target*> band;
        for (const auto& t : m_axisTargets[axis]) {
            if (!near(raw, t.bounds, scale)) continue;
            for (int k = 0; k < 3; ++k) {
                Constraint c{axis, Kind::Alignment, t.id, 0, k,
                             anchor(t.bounds, axis, k) - anchor(raw, axis, k), {}};
                const qreal at = anchor(t.bounds, axis, k);
                c.guides.push_back(QLineF(point(at, std::min(lo(raw, 1-axis), lo(t.bounds, 1-axis)), axis),
                                         point(at, std::max(hi(raw, 1-axis), hi(t.bounds, 1-axis)), axis)));
                if (acceptable(c)) candidates[axis].push_back(c);
            }
            if (row(raw, t.bounds, axis)) band.push_back(&t);
        }
        for (std::size_t i = 1; i < band.size(); ++i) {
            const auto& a = *band[i-1]; const auto& b = *band[i];
            const qreal gap = lo(b.bounds, axis) - hi(a.bounds, axis);
            if (gap < 0 || !row(a.bounds, b.bounds, axis)) continue;
            for (int placement = -1; placement <= 1; ++placement) {
                const qreal start = placement < 0 ? lo(a.bounds, axis) - gap - size(raw, axis)
                    : placement > 0 ? hi(b.bounds, axis) + gap
                    : (hi(a.bounds, axis) + lo(b.bounds, axis) - size(raw, axis)) / 2;
                Constraint c{axis, Kind::Spacing, a.id, b.id, placement, start - lo(raw, axis), {}};
                const qreal end = start + size(raw, axis);
                if (placement == 0 && (start < hi(a.bounds, axis) || end > lo(b.bounds, axis))) continue;
                const qreal cross = anchor(raw, 1-axis, 1);
                auto line = [&](qreal p, qreal q) { return QLineF(point(p, cross, axis), point(q, cross, axis)); };
                if (placement < 0) c.guides = {line(end, lo(a.bounds, axis)), line(hi(a.bounds, axis), lo(b.bounds, axis))};
                else if (placement > 0) c.guides = {line(hi(a.bounds, axis), lo(b.bounds, axis)), line(hi(b.bounds, axis), start)};
                else c.guides = {line(hi(a.bounds, axis), start), line(end, lo(b.bounds, axis))};
                if (acceptable(c)) candidates[axis].push_back(c);
            }
        }
        std::sort(candidates[axis].begin(), candidates[axis].end(), [&](const Constraint& a, const Constraint& b) {
            const bool al = locked[axis] && a.sameKey(*locked[axis]);
            const bool bl = locked[axis] && b.sameKey(*locked[axis]);
            if (al != bl) return al;
            return score(a) < score(b);
        });
        if (!candidates[axis].empty()) result.constraints[axis] = candidates[axis].front();
    }
    auto delta = [&]() {
        return QPointF(result.constraints[0] ? result.constraints[0]->correction : 0,
                       result.constraints[1] ? result.constraints[1]->correction : 0);
    };
    // Recheck row membership and collision after combining the axes.
    const auto compatible = [&](const QRectF& moved) {
        if (allowed && !allowed->contains(moved)) return false;
        for (const auto& c : result.constraints) {
            if (!c) continue;
            if (c->kind == Kind::Spacing && !spacingValid(*c, moved)) return false;
            if (c->kind == Kind::Alignment && !near(moved, *targetFor(c->first), scale)) return false;
        }
        return true;
    };
    if (!compatible(raw.translated(delta())) && result.constraints[0] && result.constraints[1]) {
        const int worse = score(*result.constraints[0]) <= score(*result.constraints[1]) ? 1 : 0;
        result.constraints[worse].reset();
    }
    result.correction = delta();
    const QRectF finalBounds = raw.translated(result.correction);
    for (auto& constraint : result.constraints) {
        if (!constraint) continue;
        auto& c = *constraint;
        const auto& a = *targetFor(c.first);
        c.guides.clear();
        if (c.kind == Kind::Alignment) {
            const qreal at = anchor(a, c.axis, c.anchor);
            c.guides.emplace_back(point(at, std::min(lo(finalBounds, 1-c.axis), lo(a, 1-c.axis)), c.axis),
                                  point(at, std::max(hi(finalBounds, 1-c.axis), hi(a, 1-c.axis)), c.axis));
        } else {
            const auto& b = *targetFor(c.second);
            const int crossAxis = 1-c.axis;
            const qreal cross = (std::max({lo(finalBounds,crossAxis),lo(a,crossAxis),lo(b,crossAxis)})
                               + std::min({hi(finalBounds,crossAxis),hi(a,crossAxis),hi(b,crossAxis)}))/2;
            auto line = [&](qreal p, qreal q) { return QLineF(point(p,cross,c.axis),point(q,cross,c.axis)); };
            if (c.anchor < 0) c.guides = {line(hi(finalBounds,c.axis),lo(a,c.axis)),line(hi(a,c.axis),lo(b,c.axis))};
            else if (c.anchor > 0) c.guides = {line(hi(a,c.axis),lo(b,c.axis)),line(hi(b,c.axis),lo(finalBounds,c.axis))};
            else c.guides = {line(hi(a,c.axis),lo(finalBounds,c.axis)),line(hi(finalBounds,c.axis),lo(b,c.axis))};
        }
    }
    return result;
}
} // namespace AnnotationSnap
