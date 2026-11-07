#include "detection/TextLineSnapper.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace {
constexpr qreal kMinimumLineHeight = 1;
constexpr qreal kMaximumLineHeight = 512;
constexpr qreal kMinimumAspectRatio = 0.35;

bool finiteRect(const QRectF& r) {
    return std::isfinite(r.x()) && std::isfinite(r.y()) &&
           std::isfinite(r.width()) && std::isfinite(r.height()) &&
           std::isfinite(r.right()) && std::isfinite(r.bottom()) &&
           r.width() > 0 && r.height() > 0;
}
}
std::shared_ptr<const TextLineSnapshot> TextLineSnapshot::fromOCR(
    const QVector<OCRTextBlock>& blocks, const QRectF& crop,
    const TextLineSnapOptions& options)
{
    auto snapshot = std::make_shared<TextLineSnapshot>();
    snapshot->m_options = options;
    if (!finiteRect(crop) || !std::isfinite(options.verticalTolerance) || options.verticalTolerance <= 0 ||
        !std::isfinite(options.ambiguityMargin) || options.ambiguityMargin < 0 ||
        !std::isfinite(options.maximumWordGap) || options.maximumWordGap < 0 ||
        !std::isfinite(options.minimumVerticalOverlap) || options.minimumVerticalOverlap <= 0 ||
        options.minimumVerticalOverlap > 1 || !std::isfinite(options.maximumHeightRatio) ||
        options.maximumHeightRatio < 1 || options.maximumHeightRatio > 2 ||
        !std::isfinite(options.maximumWordOverlap) || options.maximumWordOverlap < 0 ||
        !std::isfinite(options.minimumOverlap) || options.minimumOverlap < 0 ||
        !std::isfinite(options.minimumConfidence) || options.minimumConfidence < 0 ||
        options.minimumConfidence > 1 || !std::isfinite(options.heightPadding) ||
        options.heightPadding < 0 || options.heightPadding > 1) return snapshot;
    QVector<QRectF> rects;
    for (const auto& block : blocks) {
        const auto& b = block.boundingRect;
        if (!finiteRect(b) || b.left() < 0 || b.top() < 0 ||
            b.right() > 1 || b.bottom() > 1 || block.text.trimmed().isEmpty() ||
            !std::isfinite(block.confidence) ||
            (block.confidence != -1 && block.confidence < options.minimumConfidence)) continue;
        QRectF rect(crop.x() + b.x()*crop.width(), crop.y() + b.y()*crop.height(),
                    b.width()*crop.width(), b.height()*crop.height());
        // Reject vertical and pathological observations rather than rotating them.
        if (!finiteRect(rect) || rect.height() < kMinimumLineHeight ||
            rect.height() > kMaximumLineHeight || rect.width() < rect.height()*kMinimumAspectRatio) continue;
        rects.append(rect);
    }
    std::sort(rects.begin(), rects.end(), [](const QRectF& a, const QRectF& b) {
        return a.left() < b.left();
    });
    // Windows words and macOS observations share this conservative grouping.
    // Ambiguous neighbors remain separate so query() can refuse the snap.
    QVector<QRectF> rows;
    std::multimap<qreal, int> rowCenters;
    for (const auto& rect : rects) {
        int candidate = -1;
        bool ambiguous = false;
        auto candidateEntry = rowCenters.end();
        const qreal centerRadius = rect.height();
        const auto end = rowCenters.upper_bound(rect.center().y() + centerRadius);
        for (auto entry = rowCenters.lower_bound(rect.center().y() - centerRadius); entry != end; ++entry) {
            const int i = entry->second;
            const auto& row = rows[i];
            const qreal h = qMin(row.height(), rect.height());
            const qreal overlap = qMin(row.bottom(),rect.bottom()) - qMax(row.top(),rect.top());
            const qreal gap = rect.left() - row.right();
            if (overlap >= h*options.minimumVerticalOverlap &&
                qMax(row.height(),rect.height()) <= h*options.maximumHeightRatio &&
                gap >= -h*options.maximumWordOverlap && gap <= h*options.maximumWordGap) {
                if (candidate >= 0) { ambiguous = true; break; }
                candidate = i;
                candidateEntry = entry;
            }
        }
        if (candidate >= 0 && !ambiguous) {
            rowCenters.erase(candidateEntry);
            rows[candidate] = rows[candidate].united(rect);
            rowCenters.emplace(rows[candidate].center().y(), candidate);
        } else {
            rowCenters.emplace(rect.center().y(), int(rows.size()));
            rows.append(rect);
        }
    }
    std::sort(rows.begin(), rows.end(), [](const QRectF& a, const QRectF& b) {
        return a.center().y() < b.center().y();
    });
    for (const auto& rect : rows) {
        snapshot->m_lines.append({int(snapshot->m_lines.size()), rect,
            qMax(1, qRound(rect.height()*(1 + options.heightPadding)))});
        snapshot->m_maxHeight = qMax(snapshot->m_maxHeight, rect.height());
    }
    return snapshot;
}
TextLineSnapshot::Match TextLineSnapshot::query(const QPointF& start, std::optional<qreal> endX) const
{
    if (!std::isfinite(start.x()) || !std::isfinite(start.y()) || (endX && !std::isfinite(*endX)))
        return {{}, Reason::InvalidInput};
    const qreal radius = m_maxHeight*m_options.verticalTolerance;
    auto first = std::lower_bound(m_lines.begin(), m_lines.end(), start.y()-radius,
        [](const TextLineSnap& line, qreal y) { return line.rect.center().y() < y; });
    const TextLineSnap* best = nullptr;
    qreal score = std::numeric_limits<qreal>::max();
    qreal runnerUp = score;
    for (auto it = first; it != m_lines.end() && it->rect.center().y() <= start.y()+radius; ++it) {
        const auto& rect = it->rect;
        if (endX) {
            const qreal overlap = qMin(qMax(start.x(), *endX), rect.right()) -
                                  qMax(qMin(start.x(), *endX), rect.left());
            if (overlap < rect.height()*m_options.minimumOverlap) continue;
        } else if (start.x() < rect.left() || start.x() > rect.right()) continue;
        const qreal distance = qAbs(start.y()-rect.center().y())/rect.height();
        if (distance > m_options.verticalTolerance) continue;
        if (distance < score) { runnerUp = score; score = distance; best = &*it; }
        else runnerUp = qMin(runnerUp, distance);
    }
    if (!best) return {};
    if (runnerUp-score <= m_options.ambiguityMargin) return {{}, Reason::Ambiguous};
    return {*best, Reason::Hit};
}
