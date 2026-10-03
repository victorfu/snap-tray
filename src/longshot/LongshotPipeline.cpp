#include "longshot/LongshotPipeline.h"

#include "longshot/PositionSolver.h"

#include <QDebug>

#include <algorithm>
#include <map>
#include <set>

namespace SnapTray::Longshot {

namespace {

constexpr int kProgressAnalyzeEnd = 70;   // percent at the end of the first decode pass
constexpr int kProgressClosureEnd = 95;
constexpr int kBytesPerPixel = 4;
constexpr int kRejoinNeighbours = 3;      // placed frames tried on each side of an unplaced frame
constexpr int kClosureStride = 4;         // frames skipped between overlap closure checks
constexpr int kMinClosureOverlapFraction = 4;     // closures need an overlap of at least frameHeight / this
constexpr double kClosureRowTolerance = 10.0;     // mean |luma diff| for a row to agree at the claimed shift
constexpr int kClosureMinInkedRows = 12;          // overlap rows with ink needed to trust a closure
constexpr double kClosureMinAgreement = 0.75;     // fraction of inked overlap rows that must agree

QImage grayThumbnail(const QImage& frame, int width)
{
    return frame.scaledToWidth(std::max(1, width), Qt::SmoothTransformation).convertToFormat(QImage::Format_Grayscale8);
}

// Loop closures are the only edges nothing else cross-checks, and the analyzer
// can score a wrong shift highly (flat templates, content beyond its search
// range, repeated lines). Confirm the claimed shift against the pixels: the
// overlap rows that carry ink must look the same in both frames.
bool closureAgreesWithPixels(const QImage& fromImage, const QImage& toImage, const FrameFeatures& toFeatures,
                             const ShiftObservation& obs, const AnalyzerParams& analyzer)
{
    const QImage from = fromImage.convertToFormat(QImage::Format_Grayscale8);
    const QImage to = toImage.convertToFormat(QImage::Format_Grayscale8);
    const int height = to.height();
    const int dy = obs.shift.dy;
    if (from.size() != to.size() || toFeatures.rowGradient.size() != size_t(height)) return false;
    // Row y of `to` shows row y + dy of `from`.
    const int top = std::max({0, -dy, obs.bandsTo.top, obs.bandsFrom.top - dy});
    const int bottom = std::min({height, height - dy, height - obs.bandsTo.bottom, height - obs.bandsFrom.bottom - dy});
    QRect span = obs.movingSpanTo.isEmpty() ? to.rect() : obs.movingSpanTo.intersected(to.rect());
    if (span.isEmpty()) span = to.rect();
    const std::vector<char> inkRows = LongshotAnalyzer::rowInkFlags(toFeatures, analyzer);
    int inked = 0;
    int agreeing = 0;
    for (int y = top; y < bottom; ++y) {
        if (!inkRows[y]) continue;
        const uchar* a = to.constScanLine(y);
        const uchar* b = from.constScanLine(y + dy);
        qint64 sum = 0;
        for (int x = span.left(); x <= span.right(); ++x) sum += std::abs(int(a[x]) - int(b[x]));
        ++inked;
        if (double(sum) / span.width() <= kClosureRowTolerance) ++agreeing;
    }
    return inked >= kClosureMinInkedRows && double(agreeing) >= kClosureMinAgreement * inked;
}

bool report(const ProgressFn& progress, int percent)
{
    return !progress || progress(percent);
}

// A stationary pair shows the same view twice, so it observes no bands of its
// own; the two frames must share one mask. Iterates to a fixed point so a
// whole pause run inherits what its moving neighbours measured.
void propagateStationaryMasks(std::vector<FrameFeatures>& frames, const std::vector<ShiftObservation>& observations)
{
    bool changed = true;
    while (changed) {
        changed = false;
        for (const ShiftObservation& o : observations) {
            if (!o.stationary) continue;
            if (o.shift.from < 0 || o.shift.to < 0 || o.shift.from >= int(frames.size()) || o.shift.to >= int(frames.size())) continue;
            FrameFeatures& a = frames[size_t(o.shift.from)];
            FrameFeatures& b = frames[size_t(o.shift.to)];
            StaticBands merged;
            merged.top = std::max(a.excludedBands.top, b.excludedBands.top);
            merged.bottom = std::max(a.excludedBands.bottom, b.excludedBands.bottom);
            merged.left = std::max(a.excludedBands.left, b.excludedBands.left);
            merged.right = std::max(a.excludedBands.right, b.excludedBands.right);
            const QRect rect = a.validContentRect.intersected(b.validContentRect);
            if (!(a.excludedBands == merged) || !(b.excludedBands == merged) || a.validContentRect != rect || b.validContentRect != rect) {
                a.excludedBands = b.excludedBands = merged;
                a.validContentRect = b.validContentRect = rect;
                changed = true;
            }
        }
    }
}

} // namespace

void LongshotPipeline::resolveFrameMasks(std::vector<FrameFeatures>& frames, const std::vector<ShiftObservation>& observations)
{
    std::vector<QRect> spans(frames.size());
    for (size_t i = 0; i < frames.size(); ++i) {
        frames[i].excludedBands = StaticBands{};
        spans[i] = frames[i].validContentRect;
    }
    for (const ShiftObservation& o : observations) {
        auto apply = [&](int index, const StaticBands& bands, const QRect& span) {
            if (index < 0 || index >= int(frames.size())) return;
            StaticBands& b = frames[index].excludedBands;
            b.top = std::max(b.top, bands.top);
            b.bottom = std::max(b.bottom, bands.bottom);
            b.left = std::max(b.left, bands.left);
            b.right = std::max(b.right, bands.right);
            if (!span.isEmpty()) spans[index] = spans[index].intersected(span);
        };
        apply(o.shift.from, o.bandsFrom, o.movingSpanFrom);
        apply(o.shift.to, o.bandsTo, o.movingSpanTo);
    }
    for (size_t i = 0; i < frames.size(); ++i) {
        const StaticBands& b = frames[i].excludedBands;
        QRect r = spans[i];
        r.setTop(std::max(r.top(), b.top));
        r.setBottom(std::min(r.bottom(), frames[i].validContentRect.bottom() - b.bottom));
        r.setLeft(std::max(r.left(), b.left));
        r.setRight(std::min(r.right(), frames[i].validContentRect.right() - b.right));
        frames[i].validContentRect = r.isValid() ? r : QRect();
    }
}

std::vector<std::pair<int, int>> LongshotPipeline::selectClosureCandidates(const SolveResult& solve, int frameHeight,
                                                                           const std::vector<qint64>& frameTimesMs,
                                                                           int loopMinGapFrames, int maxFrames)
{
    Q_UNUSED(frameTimesMs);
    std::vector<std::pair<int, int>> pairs;
    std::set<int> framesUsed;
    const int n = int(solve.positions.size());
    constexpr int kRejoinMinGap = 2; // chain neighbours (gap 1) were already tried and failed
    auto tryAdd = [&](int a, int b, int minGap) {
        if (a == b || a < 0 || b < 0 || a >= n || b >= n) return;
        if (a > b) std::swap(a, b);
        if (b - a < minGap) return;
        if (std::find(pairs.begin(), pairs.end(), std::make_pair(a, b)) != pairs.end()) return;
        std::set<int> next = framesUsed;
        next.insert(a);
        next.insert(b);
        if (int(next.size()) > maxFrames) return;
        framesUsed = next;
        pairs.push_back({a, b});
    };
    // Island rejoin first: each unplaced frame against the nearest placed frames in time.
    std::vector<int> placed;
    for (int i = 0; i < n; ++i) {
        if (solve.positions[i].has_value()) placed.push_back(i);
    }
    for (int i = 0; i < n; ++i) {
        if (solve.positions[i].has_value()) continue;
        int tried = 0;
        for (int p : placed) {
            if (p < i) continue;
            tryAdd(i, p, kRejoinMinGap);
            if (++tried >= kRejoinNeighbours) break;
        }
        tried = 0;
        for (auto it = placed.rbegin(); it != placed.rend(); ++it) {
            if (*it > i) continue;
            tryAdd(*it, i, kRejoinMinGap);
            if (++tried >= kRejoinNeighbours) break;
        }
    }
    // Overlap closures: placed frames far apart in time whose viewports overlap.
    for (size_t ai = 0; ai < placed.size(); ai += kClosureStride) {
        const int a = placed[ai];
        for (size_t bi = ai + 1; bi < placed.size(); ++bi) {
            const int b = placed[bi];
            if (b - a < loopMinGapFrames) continue;
            if (std::abs(*solve.positions[a] - *solve.positions[b]) > frameHeight - frameHeight / kMinClosureOverlapFraction) continue;
            tryAdd(a, b, loopMinGapFrames);
            break; // one closure per anchor keeps the budget for other anchors
        }
    }
    return pairs;
}

AnalysisResult LongshotPipeline::analyze(LongshotFrameSource& source, const QString& path, qint64 startMs, qint64 endMs,
                                         const QRect& crop, const PipelineParams& params, const ProgressFn& progress)
{
    return analyzeIncremental(source, path, startMs, endMs, crop, params, progress, {}, {}, nullptr);
}

AnalysisResult LongshotPipeline::analyzeIncremental(LongshotFrameSource& source, const QString& path, qint64 startMs, qint64 endMs,
                                                    const QRect& crop, const PipelineParams& params, const ProgressFn& progress,
                                                    const std::vector<FrameFeatures>& knownFrames,
                                                    const std::vector<QImage>& knownThumbnails, int* framesAnalyzed)
{
    std::map<qint64, size_t> knownByTime;
    if (knownFrames.size() == knownThumbnails.size()) {
        for (size_t i = 0; i < knownFrames.size(); ++i) knownByTime[knownFrames[i].tMs] = i;
    }
    int analyzed = 0;
    AnalysisResult result;
    result.frameSize = source.frameSize();
    result.frameRate = source.frameRate();
    if (result.frameSize.width() < kMinAnalysisSide || result.frameSize.height() < kMinAnalysisSide) {
        result.error = LongshotError::CropTooSmall;
        if (framesAnalyzed) *framesAnalyzed = analyzed;
        return result;
    }
    if (source.expectedFrameCount() > params.maxAnalyzedFrames) {
        qWarning() << "LongshotPipeline: range has" << source.expectedFrameCount() << "frames, limit is" << params.maxAnalyzedFrames;
        result.error = LongshotError::TooManyFrames;
        if (framesAnalyzed) *framesAnalyzed = analyzed;
        return result;
    }
    const int expected = std::max(1, source.expectedFrameCount());

    // Pass 1: sequential features + chain shifts. Only the previous frame stays in memory.
    std::vector<ShiftObservation> observations;
    QImage previous;
    qint64 tMs = 0;
    while (auto frame = source.next(&tMs)) {
        FrameFeatures features;
        const auto known = knownByTime.find(tMs);
        if (known != knownByTime.end()) {
            features = knownFrames[known->second];
            features.excludedBands = StaticBands{};
            features.stationary = false;
            result.thumbnails.push_back(knownThumbnails[known->second]);
        } else {
            features = LongshotAnalyzer::computeFeatures(*frame, tMs);
            result.thumbnails.push_back(grayThumbnail(*frame, params.thumbnailWidth));
            ++analyzed;
        }
        const int index = int(result.frames.size());
        if (!previous.isNull()) {
            const auto obs = LongshotAnalyzer::estimateShift(previous, result.frames.back(), *frame, features, index - 1, index, params.analyzer);
            if (obs) {
                features.stationary = obs->stationary;
                observations.push_back(*obs);
                result.edges.push_back(obs->shift);
            } else {
                ++result.rejectedPairs;
            }
        }
        result.frames.push_back(std::move(features));
        previous = *frame;
        if (!report(progress, std::min(kProgressAnalyzeEnd, int(qint64(result.frames.size()) * kProgressAnalyzeEnd / expected)))) {
            result.error = LongshotError::Cancelled;
            if (framesAnalyzed) *framesAnalyzed = analyzed;
            return result;
        }
    }
    if (!source.lastError().isEmpty() && !result.frames.empty()) {
        qWarning() << "LongshotPipeline: decoding stopped early after" << result.frames.size() << "frames:" << source.lastError();
    }
    if (!source.lastError().isEmpty() && result.frames.empty()) {
        result.error = LongshotError::SourceUnavailable;
        if (framesAnalyzed) *framesAnalyzed = analyzed;
        return result;
    }
    if (result.frames.size() < 2) {
        result.error = LongshotError::TooFewFrames;
        if (framesAnalyzed) *framesAnalyzed = analyzed;
        return result;
    }
    std::vector<qint64> times;
    for (const FrameFeatures& f : result.frames) times.push_back(f.tMs);

    // First solve on chain edges only. Unconverged positions are never used.
    result.solve = PositionSolver::solve(times, result.edges, params.maxResidualPx, result.frameSize.height());
    if (!result.solve.converged) {
        qWarning() << "LongshotPipeline: position solve did not converge; refusing to place frames";
        result.error = LongshotError::NoReliableContent;
        if (framesAnalyzed) *framesAnalyzed = analyzed;
        return result;
    }

    // Loop closures and island rejoin need full-resolution frames: pick the
    // pairs, then decode the range once more keeping only those frames,
    // within the memory budget.
    const int height = result.frameSize.height();
    const qint64 bytesPerFrame = qint64(result.frameSize.width()) * height * kBytesPerPixel;
    const qint64 fitFrames = params.closureFrameBudgetBytes / std::max<qint64>(1, bytesPerFrame);
    const int budgetFrames = int(std::min<qint64>(params.maxClosureFrames, fitFrames));
    if (budgetFrames < 2) qWarning() << "LongshotPipeline: closure frame budget too small for even one pair; skipping loop closures";
    const auto candidates = budgetFrames < 2 ? std::vector<std::pair<int, int>>()
                                             : selectClosureCandidates(result.solve, height, times, params.loopMinGapFrames, budgetFrames);
    const int islandsBefore = int(result.solve.breakTimesMs.size());
    if (!candidates.empty()) {
        std::set<int> wanted;
        for (const auto& [a, b] : candidates) { wanted.insert(a); wanted.insert(b); }
        std::map<int, QImage> kept;
        if (source.open(path, startMs, endMs, crop)) {
            int index = 0;
            while (auto frame = source.next(&tMs)) {
                if (wanted.count(index)) {
                    if (index < int(times.size()) && tMs == times[size_t(index)]) kept[index] = *frame;
                    else qDebug() << "LongshotPipeline: decode order changed at frame" << index << "time" << tMs << "expected" << (index < int(times.size()) ? times[size_t(index)] : -1) << "; skipping its closures";
                }
                ++index;
                if (!report(progress, kProgressAnalyzeEnd + std::min(int(qint64(index) * (kProgressClosureEnd - kProgressAnalyzeEnd) / (2 * expected)), (kProgressClosureEnd - kProgressAnalyzeEnd) / 2))) {
                    result.error = LongshotError::Cancelled;
                    if (framesAnalyzed) *framesAnalyzed = analyzed;
                    return result;
                }
            }
        } else {
            qWarning() << "LongshotPipeline: could not reopen source for loop closures:" << source.lastError();
        }
        std::vector<ShiftObservation> closureObservations;
        for (size_t k = 0; k < candidates.size(); ++k) {
            const auto [a, b] = candidates[k];
            const auto fa = kept.find(a);
            const auto fb = kept.find(b);
            if (fa == kept.end() || fb == kept.end()) continue;
            ++result.closuresTried;
            // Closure pairs may be far apart: let the analyzer search the whole viewport height.
            AnalyzerParams closureParams = params.analyzer;
            closureParams.maxShiftFraction = 1;
            const auto obs = LongshotAnalyzer::estimateShift(fa->second, result.frames[a], fb->second, result.frames[b], a, b, closureParams);
            if (obs && closureAgreesWithPixels(fa->second, fb->second, result.frames[b], *obs, params.analyzer)) {
                // A stationary closure (same view at two times) is still an edge.
                closureObservations.push_back(*obs);
                result.edges.push_back(obs->shift);
                ++result.closuresAccepted;
            } else if (obs) {
                ++result.pixelRejectedClosures;
            }
            if (!report(progress, kProgressAnalyzeEnd + (kProgressClosureEnd - kProgressAnalyzeEnd) / 2
                                      + int(qint64(k + 1) * (kProgressClosureEnd - kProgressAnalyzeEnd) / (2 * candidates.size())))) {
                result.error = LongshotError::Cancelled;
                if (framesAnalyzed) *framesAnalyzed = analyzed;
                return result;
            }
        }
        observations.insert(observations.end(), closureObservations.begin(), closureObservations.end());
        result.solve = PositionSolver::solve(times, result.edges, params.maxResidualPx, result.frameSize.height());
        if (!result.solve.converged) {
            qWarning() << "LongshotPipeline: position solve with closures did not converge; refusing to place frames";
            result.error = LongshotError::NoReliableContent;
            if (framesAnalyzed) *framesAnalyzed = analyzed;
            return result;
        }
    }
    result.islandsRejoined = std::max(0, islandsBefore - int(result.solve.breakTimesMs.size()));

    for (FrameFeatures& f : result.frames) f.validContentRect = QRect(QPoint(0, 0), result.frameSize);
    resolveFrameMasks(result.frames, observations);
    propagateStationaryMasks(result.frames, observations);

    bool anyPlaced = false;
    for (const auto& p : result.solve.positions) anyPlaced = anyPlaced || p.has_value();
    if (!anyPlaced) result.error = LongshotError::NoReliableContent;
    if (framesAnalyzed) *framesAnalyzed = analyzed;
    if (!report(progress, 100)) result.error = LongshotError::Cancelled;
    qDebug() << "LongshotPipeline: frames" << result.frames.size() << "rejected pairs" << result.rejectedPairs
             << "closures" << result.closuresAccepted << "/" << result.closuresTried << "breaks" << result.solve.breakTimesMs.size()
             << "rejoined" << result.islandsRejoined << "solver-rejected edges" << result.solve.rejectedEdges
             << "pixel-rejected closures" << result.pixelRejectedClosures;
    return result;
}

} // namespace SnapTray::Longshot
