#include "longshot/LongshotRenderer.h"

#include <QDebug>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace SnapTray::Longshot {

namespace {

constexpr double kStationaryBonus = 2.0;
constexpr double kKeyFrameBonus = 1.0;
constexpr double kLaterTieBreak = 0.01;
constexpr int kSeamSearchRows = 8;
constexpr double kMajorityDiffThreshold = 12.0; // mean |luma| difference on thumbnails
// Row means (full resolution) closer than this count as the same row content.
constexpr float kRowDeviation = 6.0f;
// Two frames disagree on a tile when their row means deviate on more than
// 1/kRowMeanOutlierRowsDivisor of its rows (at least kMinDeviatingRows): that is
// a transient (hover, lazy load), not codec ringing on an edge row.
constexpr int kRowMeanOutlierRowsDivisor = 8;
constexpr int kMinDeviatingRows = 3;

int clampInt(int v, int lo, int hi) { return std::max(lo, std::min(v, hi)); }

// Output rows [top, bottom) of frame i, valid rows only.
bool frameCoverage(const AnalysisResult& a, int i, int minPosition, int* top, int* bottom)
{
    if (!a.solve.positions[i].has_value()) return false;
    const QRect valid = a.frames[i].validContentRect;
    if (!valid.isValid()) return false;
    const int pos = *a.solve.positions[i] - minPosition;
    *top = pos + valid.top();
    *bottom = pos + valid.top() + valid.height();
    return true;
}

// Mean |difference| of two frames' thumbnails over the output rows [top, bottom).
double thumbnailDisagreement(const AnalysisResult& a, int i, int j, int top, int bottom, int minPosition)
{
    const QImage& ti = a.thumbnails[i];
    const QImage& tj = a.thumbnails[j];
    if (ti.isNull() || tj.isNull() || a.frameSize.height() == 0) return 0.0;
    const double scale = double(ti.height()) / a.frameSize.height();
    const int posI = *a.solve.positions[i] - minPosition;
    const int posJ = *a.solve.positions[j] - minPosition;
    double sum = 0.0;
    int count = 0;
    for (int y = top; y < bottom; y += 2) {
        const int yi = clampInt(int((y - posI) * scale), 0, ti.height() - 1);
        const int yj = clampInt(int((y - posJ) * scale), 0, tj.height() - 1);
        const uchar* ri = ti.constScanLine(yi);
        const uchar* rj = tj.constScanLine(yj);
        for (int x = 0; x < ti.width(); ++x) { sum += std::abs(int(ri[x]) - int(rj[x])); ++count; }
    }
    return count ? sum / count : 0.0;
}

// Rows of output [top, bottom) on which both frames have row means, and how many
// of those differ by more than kRowDeviation.
void compareRowMeans(const AnalysisResult& a, int i, int j, int minPosition, int top, int bottom, int* compared,
                     int* differing)
{
    const int posI = *a.solve.positions[i] - minPosition;
    const int posJ = *a.solve.positions[j] - minPosition;
    const std::vector<float>& mi = a.frames[i].rowMean;
    const std::vector<float>& mj = a.frames[j].rowMean;
    *compared = 0;
    *differing = 0;
    for (int y = top; y < bottom; ++y) {
        const int ri = y - posI;
        const int rj = y - posJ;
        if (ri < 0 || rj < 0 || ri >= int(mi.size()) || rj >= int(mj.size())) continue; // no features for this row
        ++*compared;
        if (std::abs(mi[ri] - mj[rj]) > kRowDeviation) ++*differing;
    }
}

// A seam must not swap in rows the other frame renders differently; it only
// moves where at least one row could be compared and none differ.
bool framesAgreeOnRows(const AnalysisResult& a, int i, int j, int minPosition, int top, int bottom)
{
    int compared = 0, differing = 0;
    compareRowMeans(a, i, j, minPosition, top, bottom, &compared, &differing);
    return compared > 0 && differing == 0;
}

// Two candidates show the same content on a tile: thumbnails close and row means
// differing on only a few rows (missing features count as agreement).
bool framesAgreeOnTile(const AnalysisResult& a, int i, int j, int minPosition, int top, int bottom)
{
    if (a.thumbnails.size() == a.frames.size()
        && thumbnailDisagreement(a, i, j, top, bottom, minPosition) > kMajorityDiffThreshold) {
        return false;
    }
    int compared = 0, differing = 0;
    compareRowMeans(a, i, j, minPosition, top, bottom, &compared, &differing);
    const int allowed = std::max(kMinDeviatingRows, (bottom - top) / kRowMeanOutlierRowsDivisor);
    return differing <= allowed;
}

} // namespace

std::vector<TileAssignment> LongshotRenderer::assignTiles(const AnalysisResult& a, const LongshotOptions& options,
                                                          int outputHeight, int minPosition)
{
    std::vector<TileAssignment> tiles;
    const int tileRows = std::max(1, options.tileRows);
    const int frameCount = int(a.frames.size());
    // Rows above the first valid row of any frame are never covered (excluded
    // header band); the first tile only has to cover from there.
    int coveredTop = outputHeight;
    for (int i = 0; i < frameCount; ++i) {
        int fTop = 0, fBottom = 0;
        if (frameCoverage(a, i, minPosition, &fTop, &fBottom)) coveredTop = std::min(coveredTop, fTop);
    }
    for (int top = 0; top < outputHeight; top += tileRows) {
        TileAssignment tile;
        tile.outputTop = top;
        tile.outputBottom = std::min(outputHeight, top + tileRows);
        std::vector<std::pair<double, int>> candidates;
        for (int i = 0; i < frameCount; ++i) {
            int fTop = 0, fBottom = 0;
            if (!frameCoverage(a, i, minPosition, &fTop, &fBottom)) continue;
            if (fTop > std::max(tile.outputTop, coveredTop) || fBottom < tile.outputBottom) continue;
            const double frameCentre = (fTop + fBottom) / 2.0;
            const double tileCentre = (tile.outputTop + tile.outputBottom) / 2.0;
            const double centrality = 1.0 - std::min(1.0, std::abs(tileCentre - frameCentre) / std::max(1, fBottom - fTop));
            double score = centrality + (a.frames[i].stationary ? kStationaryBonus : 0.0)
                           + (a.frames[i].keyFrame ? kKeyFrameBonus : 0.0) + kLaterTieBreak * i / std::max(1, frameCount);
            candidates.push_back({score, i});
        }
        if (!candidates.empty()) {
            // Later observations win: keep the candidates that agree with the latest
            // frame covering the tile (the group containing it), then score.
            const int latest = std::max_element(candidates.begin(), candidates.end(), [](const auto& l, const auto& r) { return l.second < r.second; })->second;
            std::vector<std::pair<double, int>> group;
            for (const auto& c : candidates) {
                if (c.second == latest || framesAgreeOnTile(a, latest, c.second, minPosition, tile.outputTop, tile.outputBottom)) group.push_back(c);
            }
            candidates = group;
            std::sort(candidates.begin(), candidates.end(), [](const auto& l, const auto& r) { return l.first > r.first; });
            tile.frameIndex = candidates.front().second;
        }
        tiles.push_back(tile);
    }
    return tiles;
}

void LongshotRenderer::placeSeams(std::vector<TileAssignment>& tiles, const AnalysisResult& a, int minPosition)
{
    for (size_t k = 0; k + 1 < tiles.size(); ++k) {
        TileAssignment& above = tiles[k];
        TileAssignment& below = tiles[k + 1];
        if (above.frameIndex < 0 || below.frameIndex < 0 || above.frameIndex == below.frameIndex) continue;
        const FrameFeatures& f = a.frames[above.frameIndex];
        const int pos = *a.solve.positions[above.frameIndex] - minPosition;
        int fTop = 0, fBottom = 0;
        if (!frameCoverage(a, above.frameIndex, minPosition, &fTop, &fBottom)) continue;
        int bTop = 0, bBottom = 0;
        if (!frameCoverage(a, below.frameIndex, minPosition, &bTop, &bBottom)) continue;
        const int boundary = above.outputBottom;
        // The seam may move only where both frames still cover the rows.
        const int lo = std::max({boundary - kSeamSearchRows, above.outputTop + 1, bTop});
        const int hi = std::min({boundary + kSeamSearchRows, below.outputBottom - 1, fBottom});
        int best = boundary;
        float bestGradient = std::numeric_limits<float>::max();
        for (int y = lo; y <= hi; ++y) {
            const int row = y - pos;
            if (row < 0 || row >= int(f.rowGradient.size())) continue;
            if (!framesAgreeOnRows(a, above.frameIndex, below.frameIndex, minPosition, std::min(boundary, y), std::max(boundary, y) + 1)) continue;
            if (f.rowGradient[row] < bestGradient) { bestGradient = f.rowGradient[row]; best = y; }
        }
        above.outputBottom = best;
        below.outputTop = best;
    }
}

RenderResult LongshotRenderer::render(LongshotFrameSource& source, const QString& path, qint64 startMs, qint64 endMs,
                                      const QRect& crop, const AnalysisResult& a, const LongshotOptions& options,
                                      const ProgressFn& progress)
{
    RenderResult result;
    if (a.error != LongshotError::None) { result.error = a.error; return result; }
    const int frameCount = int(a.frames.size());
    if (frameCount == 0 || a.solve.positions.size() != a.frames.size()) { result.error = LongshotError::NoReliableContent; return result; }

    // Common column span and vertical extent over placed frames.
    int minPosition = std::numeric_limits<int>::max();
    QRect columns(0, 0, a.frameSize.width(), 1);
    int firstPlaced = -1;
    for (int i = 0; i < frameCount; ++i) {
        if (!a.solve.positions[i].has_value() || !a.frames[i].validContentRect.isValid()) continue;
        if (firstPlaced < 0) firstPlaced = i;
        minPosition = std::min(minPosition, *a.solve.positions[i]);
        const QRect v = a.frames[i].validContentRect;
        columns.setLeft(std::max(columns.left(), v.left()));
        columns.setRight(std::min(columns.right(), v.right()));
    }
    if (firstPlaced < 0 || columns.width() <= 0) { result.error = LongshotError::NoReliableContent; return result; }
    result.autoCroppedLeft = columns.left();
    result.autoCroppedRight = a.frameSize.width() - 1 - columns.right();

    // Sticky header: the top band of the first placed frame that has one, once, when requested.
    int headerFrame = -1;
    if (options.includeStickyHeader) {
        for (int i = 0; i < frameCount; ++i) {
            if (a.solve.positions[i].has_value() && a.frames[i].validContentRect.isValid() && a.frames[i].excludedBands.top > 0) { headerFrame = i; break; }
        }
    }
    const int headerRows = headerFrame >= 0 ? a.frames[headerFrame].excludedBands.top : 0;
    result.stickyHeaderIncluded = headerRows > 0;

    // Rows between the top-most valid row and the bottom-most valid row.
    int contentTop = std::numeric_limits<int>::max();
    int contentBottom = std::numeric_limits<int>::min();
    for (int i = 0; i < frameCount; ++i) {
        int t = 0, b = 0;
        if (frameCoverage(a, i, minPosition, &t, &b)) { contentTop = std::min(contentTop, t); contentBottom = std::max(contentBottom, b); }
    }
    const int bodyHeight = contentBottom - contentTop;
    std::vector<TileAssignment> tiles = assignTiles(a, options, contentBottom, minPosition);
    // Drop tiles above the first valid row (header area excluded everywhere).
    tiles.erase(std::remove_if(tiles.begin(), tiles.end(), [&](const TileAssignment& t) { return t.outputBottom <= contentTop; }), tiles.end());
    if (!tiles.empty() && tiles.front().outputTop < contentTop) tiles.front().outputTop = contentTop;
    placeSeams(tiles, a, minPosition);

    result.fullHeightPx = headerRows + bodyHeight;
    const int outputHeight = result.fullHeightPx;
    const int maxHeight = std::max(options.maxHeightPx, std::max(1, options.tileRows));
    int renderHeight = outputHeight;
    if (!options.splitOversize && outputHeight > maxHeight) {
        renderHeight = maxHeight;
        result.heightCapped = true;
    }
    QImage canvas(columns.width(), renderHeight, QImage::Format_RGB32);
    if (canvas.isNull()) { result.error = LongshotError::OutOfMemory; return result; }
    canvas.fill(Qt::white);

    // Which frames paint which output row ranges.
    std::map<int, std::vector<std::pair<int, int>>> ranges; // frame -> [(outTop, outBottom)]
    for (const TileAssignment& t : tiles) {
        if (t.outputBottom <= t.outputTop) continue;
        if (t.frameIndex < 0) {
            for (int y = t.outputTop; y < t.outputBottom; ++y) {
                const int row = headerRows + y - contentTop;
                if (row < renderHeight) result.breakRows.push_back(row);
            }
            continue;
        }
        ranges[t.frameIndex].push_back({t.outputTop, t.outputBottom});
    }
    if (headerRows > 0) ranges[headerFrame].push_back({-headerRows, 0}); // sentinel: header band

    // Low-confidence rows: frames placed only through weak edges.
    std::vector<double> bestEdgeConfidence(frameCount, 0.0);
    for (const PairShift& e : a.edges) {
        bestEdgeConfidence[e.to] = std::max(bestEdgeConfidence[e.to], e.confidence);
        bestEdgeConfidence[e.from] = std::max(bestEdgeConfidence[e.from], e.confidence);
    }

    if (!source.open(path, startMs, endMs, crop)) { result.error = LongshotError::SourceUnavailable; return result; }
    QPainter painter(&canvas);
    int index = 0;
    qint64 tMs = 0;
    const int expected = std::max(1, source.expectedFrameCount());
    while (auto frame = source.next(&tMs)) {
        const auto it = ranges.find(index);
        if (it != ranges.end()) {
            const int pos = *a.solve.positions[index] - minPosition;
            for (const auto& [outTop, outBottom] : it->second) {
                if (outTop < 0) {
                    // Header band from the frame's excluded top rows.
                    painter.drawImage(QPoint(0, 0), *frame, QRect(columns.left(), 0, columns.width(), headerRows));
                    continue;
                }
                const int srcTop = outTop - pos;
                const int destTop = headerRows + outTop - contentTop;
                const int height = outBottom - outTop;
                if (destTop >= renderHeight) continue;
                const int clippedHeight = std::min(height, renderHeight - destTop);
                painter.drawImage(QPoint(0, destTop), *frame, QRect(columns.left(), srcTop, columns.width(), clippedHeight));
                if (bestEdgeConfidence[index] < kLowConfidence && index != firstPlaced) {
                    for (int y = destTop; y < destTop + clippedHeight; ++y) result.lowConfidenceRows.push_back(y);
                }
            }
        }
        ++index;
        if (progress && !progress(std::min(99, int(qint64(index) * 100 / expected)))) { result.error = LongshotError::Cancelled; return result; }
    }
    painter.end();
    if (index < frameCount || !source.lastError().isEmpty()) {
        // A partial canvas must not be presented as complete.
        qWarning() << "LongshotRenderer: decode stopped at frame" << index << "of" << frameCount << source.lastError();
        result.error = LongshotError::SourceUnavailable;
        return result;
    }

    if (options.splitOversize && outputHeight > maxHeight) {
        for (int top = 0; top < outputHeight; top += maxHeight) {
            result.parts.push_back(canvas.copy(0, top, canvas.width(), std::min(maxHeight, outputHeight - top)));
        }
    } else {
        result.parts.push_back(canvas);
    }
    std::sort(result.breakRows.begin(), result.breakRows.end());
    if (progress) progress(100);
    qDebug() << "LongshotRenderer: height" << result.fullHeightPx << "parts" << result.parts.size() << "capped" << result.heightCapped
             << "side crop" << result.autoCroppedLeft << result.autoCroppedRight << "break rows" << result.breakRows.size();
    return result;
}

} // namespace SnapTray::Longshot
