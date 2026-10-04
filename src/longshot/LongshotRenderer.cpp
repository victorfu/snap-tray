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
    // Strict: a transient covering exactly `allowed` rows is a disagreement.
    return differing < allowed;
}

} // namespace

std::vector<TileAssignment> LongshotRenderer::assignTiles(const AnalysisResult& a, const LongshotOptions& options,
                                                          int outputHeight, int minPosition, const QRect& columns)
{
    std::vector<TileAssignment> tiles;
    const int tileRows = std::max(1, options.tileRows);
    const int frameCount = int(a.frames.size());
    auto coversColumns = [&](int index) {
        const QRect valid = a.frames[index].validContentRect;
        return columns.isEmpty() || (valid.left() <= columns.left() && valid.right() >= columns.right());
    };
    auto assignRange = [&](int top, int bottom) {
        TileAssignment tile;
        tile.outputTop = top;
        tile.outputBottom = bottom;
        std::vector<std::pair<double, int>> candidates;
        for (int i = 0; i < frameCount; ++i) {
            int fTop = 0, fBottom = 0;
            if (!frameCoverage(a, i, minPosition, &fTop, &fBottom)) continue;
            if (!coversColumns(i) || fTop > tile.outputTop || fBottom < tile.outputBottom) continue;
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
        return tile;
    };
    for (int top = 0; top < outputHeight; top += tileRows) {
        const int bottom = std::min(outputHeight, top + tileRows);
        const TileAssignment tile = assignRange(top, bottom);
        if (tile.frameIndex >= 0) {
            tiles.push_back(tile);
            continue;
        }
        // A union of overlapping short frames can cover the entire tile even
        // when no single frame does. Only split these tiles, keeping ordinary
        // tiles (and their existing scoring) unchanged.
        std::vector<int> boundaries{top, bottom};
        for (int i = 0; i < frameCount; ++i) {
            int fTop = 0, fBottom = 0;
            if (!frameCoverage(a, i, minPosition, &fTop, &fBottom)) continue;
            if (fTop > top && fTop < bottom) boundaries.push_back(fTop);
            if (fBottom > top && fBottom < bottom) boundaries.push_back(fBottom);
        }
        std::sort(boundaries.begin(), boundaries.end());
        boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
        for (size_t i = 1; i < boundaries.size(); ++i) {
            tiles.push_back(assignRange(boundaries[i - 1], boundaries[i]));
        }
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
        if (lo > hi) continue;
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

    // Output columns: the union of the placed frames' moving spans. A column is
    // cropped only when it is static in EVERY placed frame (no placed frame
    // moves there), so one quiet pair cannot narrow the whole page.
    int minPosition = std::numeric_limits<int>::max();
    QRect columns;
    int firstPlaced = -1;
    for (int i = 0; i < frameCount; ++i) {
        if (!a.solve.positions[i].has_value() || !a.frames[i].validContentRect.isValid()) continue;
        const QRect v = a.frames[i].validContentRect;
        if (firstPlaced < 0) {
            firstPlaced = i;
            columns = QRect(v.left(), 0, v.width(), 1);
        }
        minPosition = std::min(minPosition, *a.solve.positions[i]);
        columns.setLeft(std::min(columns.left(), v.left()));
        columns.setRight(std::max(columns.right(), v.right()));
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
    // Split only where a frame's horizontal mask changes. Each strip then uses
    // the existing tile/seam policy, restricted to frames covering that strip.
    // This preserves the union width without sampling another frame's sidebar.
    struct PaintTile { QRect rect; int frameIndex; };
    std::vector<PaintTile> paintTiles;
    std::vector<int> columnEdges{columns.left(), columns.right() + 1};
    for (int i = 0; i < frameCount; ++i) {
        if (!a.solve.positions[i] || !a.frames[i].validContentRect.isValid()) continue;
        const QRect valid = a.frames[i].validContentRect;
        columnEdges.push_back(valid.left());
        columnEdges.push_back(valid.right() + 1);
    }
    std::sort(columnEdges.begin(), columnEdges.end());
    columnEdges.erase(std::unique(columnEdges.begin(), columnEdges.end()), columnEdges.end());
    for (size_t i = 1; i < columnEdges.size(); ++i) {
        const QRect strip(columnEdges[i - 1], 0, columnEdges[i] - columnEdges[i - 1], 1);
        auto tiles = assignTiles(a, options, contentBottom, minPosition, strip);
        tiles.erase(std::remove_if(tiles.begin(), tiles.end(), [&](const TileAssignment& t) {
            return t.outputBottom <= contentTop;
        }), tiles.end());
        if (!tiles.empty()) tiles.front().outputTop = std::max(tiles.front().outputTop, contentTop);
        placeSeams(tiles, a, minPosition);
        for (const TileAssignment& tile : tiles) {
            paintTiles.push_back({QRect(strip.left(), tile.outputTop, strip.width(),
                                       tile.outputBottom - tile.outputTop), tile.frameIndex});
        }
    }

    result.fullHeightPx = headerRows + bodyHeight;
    const int outputHeight = result.fullHeightPx;
    const int maxHeight = std::max(options.maxHeightPx, std::max(1, options.tileRows));
    int renderHeight = outputHeight;
    if (!options.splitOversize && outputHeight > maxHeight) {
        renderHeight = maxHeight;
        result.heightCapped = true;
    }
    // Output parts: one (possibly capped) image, or consecutive maxHeight
    // slices when splitting. Each part is its own allocation; no full-height
    // canvas exists when splitting.
    const int partHeight = options.splitOversize && outputHeight > maxHeight ? maxHeight : renderHeight;
    QList<QImage> parts;
    for (int top = 0; top < renderHeight; top += partHeight) {
        QImage part(columns.width(), std::min(partHeight, renderHeight - top), QImage::Format_RGB32);
        if (part.isNull()) { result.error = LongshotError::OutOfMemory; return result; }
        part.fill(Qt::white);
        parts.push_back(part);
    }
    if (parts.isEmpty()) { result.error = LongshotError::NoReliableContent; return result; }
    // Paint a valid source rectangle at output row destTop, into every part
    // it overlaps. Horizontal placement preserves the union output columns.
    auto paintRows = [&](const QImage& frame, const QRect& sourceRect, int destTop) {
        const int height = sourceRect.height();
        const int first = std::max(0, destTop / partHeight);
        for (int p = first; p < parts.size(); ++p) {
            const int partTop = p * partHeight;
            const int from = std::max(destTop, partTop);
            const int to = std::min(destTop + height, partTop + int(parts[p].height()));
            if (from >= destTop + height) break;
            if (to <= from) continue;
            QPainter painter(&parts[p]);
            painter.drawImage(QPoint(sourceRect.left() - columns.left(), from - partTop), frame,
                              QRect(sourceRect.left(), sourceRect.top() + (from - destTop), sourceRect.width(), to - from));
        }
    };

    // Which frames paint which rectangles. Partial horizontal gaps use the
    // same white background and break-row reporting as missing vertical rows.
    std::map<int, std::vector<QRect>> ranges;
    for (const PaintTile& tile : paintTiles) {
        if (tile.rect.isEmpty()) continue;
        if (tile.frameIndex < 0) {
            for (int y = tile.rect.top(); y <= tile.rect.bottom(); ++y) {
                const int row = headerRows + y - contentTop;
                if (row >= 0 && row < renderHeight) result.breakRows.push_back(row);
            }
        } else {
            ranges[tile.frameIndex].push_back(tile.rect);
        }
    }
    if (headerRows > 0) {
        const QRect valid = a.frames[headerFrame].validContentRect;
        const int left = std::max(columns.left(), valid.left());
        const int right = std::min(columns.right(), valid.right());
        ranges[headerFrame].push_back(QRect(left, -headerRows, right - left + 1, headerRows));
        if (left > columns.left() || right < columns.right()) {
            for (int y = 0; y < std::min(headerRows, renderHeight); ++y) result.breakRows.push_back(y);
        }
    }

    // Low-confidence rows: frames placed only through weak edges.
    std::vector<double> bestEdgeConfidence(frameCount, 0.0);
    for (const PairShift& e : a.edges) {
        bestEdgeConfidence[e.to] = std::max(bestEdgeConfidence[e.to], e.confidence);
        bestEdgeConfidence[e.from] = std::max(bestEdgeConfidence[e.from], e.confidence);
    }

    if (!source.open(path, startMs, endMs, crop)) { result.error = LongshotError::SourceUnavailable; return result; }
    if (source.frameSize() != a.frameSize) {
        qWarning() << "LongshotRenderer: source frame size" << source.frameSize() << "does not match the analysis" << a.frameSize;
        result.error = LongshotError::SourceUnavailable;
        return result;
    }
    int index = 0;
    qint64 tMs = 0;
    const int expected = std::max(1, source.expectedFrameCount());
    while (auto frame = source.next(&tMs)) {
        const auto it = ranges.find(index);
        if (it != ranges.end()) {
            const int pos = *a.solve.positions[index] - minPosition;
            for (const QRect& range : it->second) {
                const int outTop = range.top();
                const int outBottom = range.bottom() + 1;
                if (outTop < 0) {
                    // Header band from the frame's excluded top rows.
                    paintRows(*frame, QRect(range.left(), 0, range.width(), std::min(headerRows, renderHeight)), 0);
                    continue;
                }
                const int srcTop = outTop - pos;
                const int destTop = headerRows + outTop - contentTop;
                const int height = outBottom - outTop;
                if (destTop >= renderHeight) continue;
                const int clippedHeight = std::min(height, renderHeight - destTop);
                paintRows(*frame, QRect(range.left(), srcTop, range.width(), clippedHeight), destTop);
                if (bestEdgeConfidence[index] < kLowConfidence && index != firstPlaced) {
                    for (int y = destTop; y < destTop + clippedHeight; ++y) result.lowConfidenceRows.push_back(y);
                }
            }
        }
        ++index;
        if (progress && !progress(std::min(99, int(qint64(index) * 100 / expected)))) { result.error = LongshotError::Cancelled; return result; }
    }
    if (index < frameCount || !source.lastError().isEmpty()) {
        // A partial canvas must not be presented as complete.
        qWarning() << "LongshotRenderer: decode stopped at frame" << index << "of" << frameCount << source.lastError();
        result.error = LongshotError::SourceUnavailable;
        return result;
    }

    result.parts = parts;
    std::sort(result.breakRows.begin(), result.breakRows.end());
    result.breakRows.erase(std::unique(result.breakRows.begin(), result.breakRows.end()), result.breakRows.end());
    std::sort(result.lowConfidenceRows.begin(), result.lowConfidenceRows.end());
    result.lowConfidenceRows.erase(std::unique(result.lowConfidenceRows.begin(), result.lowConfidenceRows.end()),
                                   result.lowConfidenceRows.end());
    if (progress) progress(100);
    qDebug() << "LongshotRenderer: height" << result.fullHeightPx << "parts" << result.parts.size() << "capped" << result.heightCapped
             << "side crop" << result.autoCroppedLeft << result.autoCroppedRight << "break rows" << result.breakRows.size();
    return result;
}

} // namespace SnapTray::Longshot
