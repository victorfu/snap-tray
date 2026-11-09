#pragma once

#include <QLineF>
#include <QRectF>
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

// All geometry is in annotation coordinates; scale is view DIP per canvas unit.
namespace AnnotationSnap {
using Id = std::uint64_t;
struct Target { Id id; QRectF bounds; };
enum class Kind { Alignment, Spacing };
struct Constraint {
    int axis = 0;
    Kind kind = Kind::Alignment;
    Id first = 0, second = 0;
    int anchor = 0; // alignment anchor or spacing placement (-1, 0, 1)
    qreal correction = 0;
    std::vector<QLineF> guides;
    bool sameKey(const Constraint& other) const;
};
struct Result {
    QPointF correction;
    std::array<std::optional<Constraint>, 2> constraints;
};
class Engine {
public:
    static constexpr qreal EnterRadius = 5.0;
    static constexpr qreal ExitRadius = 8.0;
    static constexpr qreal Neighborhood = 300.0;
    static constexpr qreal RowOverlap = 0.5;
    void setTargets(std::vector<Target> targets);
    Result query(const QRectF& raw, qreal scale,
                 const std::array<std::optional<Constraint>, 2>& locked = {},
                 const std::optional<QRectF>& allowed = std::nullopt) const;
private:
    std::vector<Target> m_targets;
    std::array<std::vector<Target>, 2> m_axisTargets;
};
} // namespace AnnotationSnap
