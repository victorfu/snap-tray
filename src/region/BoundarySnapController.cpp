#include "region/BoundarySnapController.h"
#include "utils/CoordinateHelper.h"
#include <QElapsedTimer>
#include <cmath>

namespace {
constexpr int kEnterRadius = 6;
constexpr int kExitRadius = 9;
int edgePosition(const QRect& r, Qt::Edge edge)
{
    if (edge == Qt::LeftEdge) return r.x();
    if (edge == Qt::RightEdge) return r.x() + r.width();
    if (edge == Qt::TopEdge) return r.y();
    return r.y() + r.height();
}
}

void BoundarySnapController::setSource(const QImage& image, qreal devicePixelRatio)
{
    ++m_generation;
    reset();
    m_dpr = devicePixelRatio;
    m_index.setImage(std::isfinite(m_dpr) && m_dpr > 0 && m_dpr <= 8 ? image : QImage());
}

void BoundarySnapController::reset()
{
    m_vertical.reset();
    m_horizontal.reset();
}

std::optional<BoundarySnapController::Lock> BoundarySnapController::query(
    const QRect& raw, Qt::Edge edge, std::optional<Lock> previous, const QElapsedTimer& timer, qint64 budgetNanoseconds) const
{
    using Axis = BoundarySnapIndex::Axis;
    const bool vertical = edge == Qt::LeftEdge || edge == Qt::RightEdge;
    const QRect physical = CoordinateHelper::toPhysicalCoveringRect(raw, m_dpr);
    const int begin = vertical ? physical.y() : physical.x();
    const int end = begin + (vertical ? physical.height() : physical.width());
    const int target = edgePosition(raw, edge);
    const bool leading = edge == Qt::LeftEdge || edge == Qt::TopEdge;
    const auto evaluate = [&](int logical) -> std::optional<Lock> {
        // Enumerate only representable crop boundaries, using exactly the same
        // floor/ceil convention as the actual export. No rounded false hits.
        const double scaled = logical * m_dpr;
        const int boundary = leading ? int(std::floor(scaled)) : int(std::ceil(scaled));
        const auto hit = m_index.evaluate(vertical ? Axis::Vertical : Axis::Horizontal,
                                          boundary, begin, end, {});
        if (!hit) return std::nullopt;
        return Lock{logical, *hit};
    };
    if (timer.nsecsElapsed() >= budgetNanoseconds) return std::nullopt;
    if (previous && std::abs(previous->logical - target) <= kExitRadius) {
        if (auto retained = evaluate(previous->logical)) return retained;
    }
    std::optional<Lock> best;
    for (int pos = qMax(0, target - kEnterRadius); pos <= target + kEnterRadius; ++pos) {
        if (timer.nsecsElapsed() >= budgetNanoseconds) return std::nullopt;
        auto candidate = evaluate(pos);
        if (!candidate) continue;
        const int distance = std::abs(pos - target);
        const int bestDistance = best ? std::abs(best->logical - target) : kEnterRadius + 1;
        if (!best || distance < bestDistance
            || (distance == bestDistance && candidate->hit.meanStrength > best->hit.meanStrength)
            || (distance == bestDistance && candidate->hit.meanStrength == best->hit.meanStrength
                && candidate->hit.supportFraction > best->hit.supportFraction)) best = candidate;
        // Remaining exact ties keep the lower coordinate (ascending scan).
    }
    return best;
}

BoundarySnapController::Result BoundarySnapController::snap(const QRect& raw, Qt::Edges movingEdges, QueryOptions options)
{
    const qint64 budgetNanoseconds = options.budgetNanoseconds;
    Result result{raw, {}};
    if (!raw.isValid() || m_index.size().isEmpty()) { reset(); return result; }
    // Reject out-of-image coordinates before any floating-to-integer conversion.
    if (raw.x() < 0 || raw.y() < 0
        || (qreal(raw.x()) + raw.width()) * m_dpr > m_index.size().width()
        || (qreal(raw.y()) + raw.height()) * m_dpr > m_index.size().height()) {
        reset(); return result;
    }
    const QRect physical = CoordinateHelper::toPhysicalCoveringRect(raw, m_dpr);
    constexpr int kMaximumSpan = 16384;
    if (physical.width() > kMaximumSpan || physical.height() > kMaximumSpan
        || !QRect(QPoint(), m_index.size()).contains(physical)) { reset(); return result; }
    QElapsedTimer timer;
    timer.start();
    const Qt::Edge xEdge = movingEdges.testFlag(Qt::LeftEdge) ? Qt::LeftEdge : Qt::RightEdge;
    const Qt::Edge yEdge = movingEdges.testFlag(Qt::TopEdge) ? Qt::TopEdge : Qt::BottomEdge;
    m_vertical = (movingEdges & (Qt::LeftEdge | Qt::RightEdge))
        ? query(raw, xEdge, m_vertical, timer, budgetNanoseconds) : std::nullopt;
    m_horizontal = (movingEdges & (Qt::TopEdge | Qt::BottomEdge))
        ? query(raw, yEdge, m_horizontal, timer, budgetNanoseconds) : std::nullopt;
    // Both axes query the same raw span. Discard the entire update on timeout.
    if (timer.nsecsElapsed() >= budgetNanoseconds) { reset(); return result; }
    if (m_vertical) {
        if (xEdge == Qt::LeftEdge) result.rect.setLeft(m_vertical->logical);
        else result.rect.setRight(m_vertical->logical - 1);
        result.edges |= xEdge;
    }
    if (m_horizontal) {
        if (yEdge == Qt::TopEdge) result.rect.setTop(m_horizontal->logical);
        else result.rect.setBottom(m_horizontal->logical - 1);
        result.edges |= yEdge;
    }
    const QRect crop = CoordinateHelper::toPhysicalCoveringRect(result.rect, m_dpr);
    if (!result.rect.isValid()
        || (m_vertical && edgePosition(crop, xEdge) != m_vertical->hit.pixelBoundary)
        || (m_horizontal && edgePosition(crop, yEdge) != m_horizontal->hit.pixelBoundary)) {
        reset(); return {raw, {}};
    }
    return result;
}
