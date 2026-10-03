#include "longshot/LongshotAnalyzer.h"

#include "utils/MatConverter.h"

#include <QDebug>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace SnapTray::Longshot {

namespace {

constexpr float kLumaMax = 255.0f;
constexpr int kPeakExclusionRows = 2;   // neighbours of a chosen peak are not a second peak
constexpr int kMinContentRows = 32;     // fewer usable rows than this: give up on the pair
constexpr int kMinTemplateRows = 24;
constexpr double kStationaryMeanDiff = 1.5; // whole-frame mean |diff| below this = no motion
constexpr int kMinMovingColumns = 32;

cv::Mat gray(const QImage& image)
{
    return MatConverter::toGray(image.format() == QImage::Format_RGB32 ? image : image.convertToFormat(QImage::Format_RGB32));
}

// Mean |a - b| per row, both the same size.
std::vector<double> rowAbsDiff(const cv::Mat& a, const cv::Mat& b)
{
    cv::Mat diff;
    cv::absdiff(a, b, diff);
    std::vector<double> rows(diff.rows);
    for (int y = 0; y < diff.rows; ++y) rows[y] = cv::mean(diff.row(y))[0];
    return rows;
}

std::vector<double> columnAbsDiff(const cv::Mat& a, const cv::Mat& b)
{
    cv::Mat diff;
    cv::absdiff(a, b, diff);
    std::vector<double> cols(diff.cols);
    for (int x = 0; x < diff.cols; ++x) cols[x] = cv::mean(diff.col(x))[0];
    return cols;
}

// 1-D phase correlation of two equal-length profiles; returns the zero-padded
// (linear) correlation response of length 2n where bin k (k < n) means
// "to row y shows from row y + k" and bin 2n - k means the same for -k.
std::vector<double> phaseCorrelation(const std::vector<float>& from, const std::vector<float>& to)
{
    const int n = int(from.size());
    // Zero-pad to 2n so the correlation is linear, not circular: every shift
    // in (-n, n) maps to a distinct bin and large shifts keep their full
    // overlap weight (a Hann window would suppress them).
    const int size = 2 * n;
    cv::Mat a = cv::Mat::zeros(1, size, CV_32F);
    cv::Mat b = cv::Mat::zeros(1, size, CV_32F);
    const double meanA = std::accumulate(from.begin(), from.end(), 0.0) / n;
    const double meanB = std::accumulate(to.begin(), to.end(), 0.0) / n;
    for (int i = 0; i < n; ++i) {
        a.at<float>(0, i) = float(from[i] - meanA);
        b.at<float>(0, i) = float(to[i] - meanB);
    }
    cv::Mat fa, fb, cross, response;
    cv::dft(a, fa, cv::DFT_COMPLEX_OUTPUT);
    cv::dft(b, fb, cv::DFT_COMPLEX_OUTPUT);
    // Correlation theorem: IDFT(F_from * conj(F_to))[k] = sum_y from[y + k] * to[y],
    // which peaks at k = dy because to[y] shows the page row from[y + dy] shows.
    cv::mulSpectrums(fa, fb, cross, 0, true);
    // Normalize magnitudes -> phase only.
    for (int i = 0; i < cross.cols; ++i) {
        cv::Vec2f& c = cross.at<cv::Vec2f>(0, i);
        const float mag = std::hypot(c[0], c[1]);
        if (mag > 1e-6f) { c[0] /= mag; c[1] /= mag; } else { c[0] = c[1] = 0.0f; }
    }
    cv::dft(cross, response, cv::DFT_INVERSE | cv::DFT_REAL_OUTPUT | cv::DFT_SCALE);
    std::vector<double> out(size);
    for (int i = 0; i < size; ++i) out[i] = response.at<float>(0, i);
    return out;
}

// Top-k local maxima of the response as signed shifts in [-n/2, n/2).
std::vector<int> topShifts(const std::vector<double>& response, int count, int maxAbsShift)
{
    const int n = int(response.size());
    std::vector<std::pair<double, int>> peaks;
    for (int k = 0; k < n; ++k) {
        const int shift = k < n / 2 ? k : k - n;
        if (std::abs(shift) > maxAbsShift) continue;
        peaks.push_back({response[k], shift});
    }
    std::sort(peaks.begin(), peaks.end(), [](const auto& l, const auto& r) { return l.first > r.first; });
    std::vector<int> chosen;
    for (const auto& [score, shift] : peaks) {
        if (std::any_of(chosen.begin(), chosen.end(), [&](int c) { return std::abs(c - shift) <= kPeakExclusionRows; })) continue;
        chosen.push_back(shift);
        if (int(chosen.size()) == count) break;
    }
    return chosen;
}

// NCC of a template band taken from `to` (content rows only) against `from`
// shifted by candidate dy, searched within +-radius. Returns the best score
// and the refined dy. With dy = pageOffset(to) - pageOffset(from): a row y of
// `to` shows page row offsetTo + y, which in `from` is row y + dy.
std::pair<double, int> refineByNcc(const cv::Mat& from, const cv::Mat& to, int candidate, int radius,
                                   const QRect& contentTo, const QRect& contentFrom, int templateRows)
{
    // Template: a band of `to` that has a counterpart in `from` for every
    // shift within +-radius of the candidate. `to` row y corresponds to `from`
    // row y + dy, so only part of `to` overlaps `from` when the shift is
    // large: centre the template inside that overlap, not inside the frame.
    const int fromBottom = contentFrom.top() + contentFrom.height();
    const int toBottom = contentTo.top() + contentTo.height();
    const int overlapTop = std::max(contentTo.top(), contentFrom.top() - candidate + radius);
    const int overlapBottom = std::min(toBottom, fromBottom - candidate - radius); // exclusive
    const int overlapRows = overlapBottom - overlapTop;
    if (overlapRows < kMinTemplateRows) return {0.0, candidate};
    const int tplHeight = std::clamp(templateRows, kMinTemplateRows,
                                     std::min(overlapRows, std::max(kMinTemplateRows, contentTo.height() / 2)));
    const int tplTop = overlapTop + (overlapRows - tplHeight) / 2;
    const cv::Rect tplRect(contentTo.left(), tplTop, contentTo.width(), tplHeight);
    if (tplRect.width <= 0 || tplRect.height <= 0) return {0.0, candidate};
    // Search window in `from`: rows [tplTop + candidate - radius, ... + tplHeight + radius).
    const int clampedTop = tplTop + candidate - radius;
    const int clampedBottom = tplTop + candidate + tplHeight + radius; // exclusive
    const cv::Rect searchRect(contentTo.left(), clampedTop, contentTo.width(), clampedBottom - clampedTop);
    if (searchRect.x + searchRect.width > from.cols || searchRect.y + searchRect.height > from.rows) return {0.0, candidate};
    cv::Mat result;
    cv::matchTemplate(from(searchRect), to(tplRect), result, cv::TM_CCOEFF_NORMED);
    double minVal = 0.0, maxVal = 0.0;
    cv::Point minLoc, maxLoc;
    cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);
    const int matchedRowInFrom = clampedTop + maxLoc.y;
    return {std::isfinite(maxVal) ? maxVal : 0.0, matchedRowInFrom - tplTop};
}

} // namespace

FrameFeatures LongshotAnalyzer::computeFeatures(const QImage& frame, qint64 tMs)
{
    FrameFeatures f;
    f.tMs = tMs;
    const cv::Mat g = gray(frame);
    f.rowMean.resize(g.rows);
    f.rowGradient.resize(g.rows);
    for (int y = 0; y < g.rows; ++y) {
        f.rowMean[y] = float(cv::mean(g.row(y))[0]);
        if (y + 1 < g.rows) {
            cv::Mat diff;
            cv::absdiff(g.row(y + 1), g.row(y), diff);
            f.rowGradient[y] = float(cv::mean(diff)[0]);
        } else {
            f.rowGradient[y] = y > 0 ? f.rowGradient[y - 1] : 0.0f;
        }
    }
    f.validContentRect = frame.rect();
    return f;
}

StaticBands LongshotAnalyzer::detectStaticBands(const QImage& fromImage, const QImage& toImage, int dy,
                                                const AnalyzerParams& params)
{
    StaticBands bands;
    if (dy == 0) return bands;
    const cv::Mat from = gray(fromImage);
    const cv::Mat to = gray(toImage);
    if (from.size() != to.size()) return bands;
    const std::vector<double> unchanged = rowAbsDiff(from, to);
    const FrameFeatures toFeatures = computeFeatures(toImage, 0);
    // Rows that have ink and did not move, scanning in from the edges. Rows
    // without ink are skipped (they cannot tell us anything) but end the band
    // once a moving inked row has been seen.
    // Row y of `to` shows the row `from` has at y + dy; where that row falls
    // outside the frame, use the mirrored pairing from[y] <-> to[y - dy].
    auto explainedByScroll = [&](int y) {
        cv::Mat diff;
        if (y + dy >= 0 && y + dy < from.rows) {
            cv::absdiff(to.row(y), from.row(y + dy), diff);
        } else if (y - dy >= 0 && y - dy < to.rows) {
            cv::absdiff(from.row(y), to.row(y - dy), diff);
        } else {
            return false;
        }
        return cv::mean(diff)[0] <= params.staticRowDiffThreshold;
    };
    auto scan = [&](int start, int step) {
        int band = 0;
        int y = start;
        while (y >= 0 && y < to.rows) {
            const bool inked = toFeatures.rowGradient[y] >= params.inkGradientThreshold;
            if (inked) {
                if (unchanged[y] > params.staticRowDiffThreshold) break;
                // Unchanged only because the scroll moved it onto an identical
                // row (inside a text line after a small shift) is no evidence
                // of an overlay: skip it like a blank row.
                if (explainedByScroll(y)) { y += step; continue; }
                band = step > 0 ? y + 1 : to.rows - y;
            }
            y += step;
        }
        return band;
    };
    bands.top = scan(0, 1);
    bands.bottom = scan(to.rows - 1, -1);
    // A whole-frame "band" means the frame did not move at all: that is
    // stationary, not a header, and the caller handles it before calling here.
    if (bands.top + bands.bottom >= to.rows) return StaticBands{};
    return bands;
}

QRect LongshotAnalyzer::movingColumnSpan(const QImage& fromImage, const QImage& toImage, int dy,
                                         const AnalyzerParams& params)
{
    Q_UNUSED(dy);
    const cv::Mat from = gray(fromImage);
    const cv::Mat to = gray(toImage);
    const QRect full(0, 0, to.cols, to.rows);
    if (from.size() != to.size()) return full;
    const std::vector<double> unchanged = columnAbsDiff(from, to);
    int left = 0;
    while (left < to.cols && unchanged[left] < params.movingColumnDiffThreshold) ++left;
    int right = to.cols - 1;
    while (right > left && unchanged[right] < params.movingColumnDiffThreshold) --right;
    if (right - left + 1 < kMinMovingColumns) return full;
    return QRect(left, 0, right - left + 1, to.rows);
}

std::optional<ShiftObservation> LongshotAnalyzer::estimateShift(const QImage& fromImage, const FrameFeatures& fromFeatures,
                                                                const QImage& toImage, const FrameFeatures& toFeatures,
                                                                int fromIndex, int toIndex, const AnalyzerParams& params)
{
    if (fromImage.size() != toImage.size() || fromImage.isNull()) return std::nullopt;
    const cv::Mat from = gray(fromImage);
    const cv::Mat to = gray(toImage);
    const int height = to.rows;
    const int width = to.cols;

    ShiftObservation obs;
    obs.shift.from = fromIndex;
    obs.shift.to = toIndex;
    obs.movingSpanFrom = QRect(0, 0, width, height);
    obs.movingSpanTo = obs.movingSpanFrom;

    // Frames without ink carry no information: two blank frames look
    // stationary but could be anywhere inside a blank region. Refuse them.
    int inkedAnywhere = 0;
    for (int y = 0; y < height; ++y) {
        if (fromFeatures.rowGradient[y] >= params.inkGradientThreshold || toFeatures.rowGradient[y] >= params.inkGradientThreshold) ++inkedAnywhere;
    }
    if (inkedAnywhere < kMinContentRows) {
        qDebug() << "LongshotAnalyzer: pair" << fromIndex << "->" << toIndex << "has no ink; ambiguous";
        return std::nullopt;
    }

    // Stationary pair: nothing changed anywhere.
    const std::vector<double> unchangedRows = rowAbsDiff(from, to);
    const double meanDiff = std::accumulate(unchangedRows.begin(), unchangedRows.end(), 0.0) / height;
    if (meanDiff < kStationaryMeanDiff) {
        obs.stationary = true;
        obs.shift.dy = 0;
        obs.shift.confidence = 1.0;
        return obs;
    }

    const int maxAbsShift = height / std::max(1, params.maxShiftFraction);

    // Coarse pass 1: full-profile phase correlation gives a first dy.
    std::vector<int> candidates = topShifts(phaseCorrelation(fromFeatures.rowMean, toFeatures.rowMean),
                                            params.coarseCandidates, maxAbsShift);
    if (candidates.empty()) return std::nullopt;

    // Static bands from the leading candidate, then redo the coarse pass on
    // content rows only so a header cannot pin the correlation at zero.
    StaticBands bands = detectStaticBands(fromImage, toImage, candidates.front(), params);
    const QRect span = movingColumnSpan(fromImage, toImage, candidates.front(), params);
    const int contentTop = bands.top;
    const int contentBottom = height - bands.bottom; // exclusive
    if (contentBottom - contentTop < kMinContentRows) {
        qDebug() << "LongshotAnalyzer: too little content between bands" << bands.top << bands.bottom;
        return std::nullopt;
    }
    std::vector<float> fromProfile(fromFeatures.rowMean.begin() + contentTop, fromFeatures.rowMean.begin() + contentBottom);
    std::vector<float> toProfile(toFeatures.rowMean.begin() + contentTop, toFeatures.rowMean.begin() + contentBottom);
    // Rows without ink in BOTH frames add nothing but noise; a fully blank
    // content area has no information at all.
    int inkedRows = 0;
    for (int y = contentTop; y < contentBottom; ++y) {
        if (fromFeatures.rowGradient[y] >= params.inkGradientThreshold || toFeatures.rowGradient[y] >= params.inkGradientThreshold) ++inkedRows;
    }
    if (inkedRows < kMinContentRows) {
        qDebug() << "LongshotAnalyzer: content area has no ink; ambiguous";
        return std::nullopt;
    }
    candidates = topShifts(phaseCorrelation(fromProfile, toProfile), params.coarseCandidates,
                           std::min(maxAbsShift, (contentBottom - contentTop) / std::max(1, params.maxShiftFraction)));
    if (candidates.empty()) return std::nullopt;

    // Fine pass: NCC around each candidate; best + margin = confidence.
    const QRect contentTo(span.left(), contentTop, span.width(), contentBottom - contentTop);
    const QRect contentFrom = contentTo;
    std::vector<std::pair<double, int>> refined;
    for (int candidate : candidates) {
        refined.push_back(refineByNcc(from, to, candidate, params.refineRadius, contentTo, contentFrom, params.templateRows));
    }
    std::sort(refined.begin(), refined.end(), [](const auto& l, const auto& r) { return l.first > r.first; });
    const double best = refined.front().first;
    double runnerUp = 0.0;
    for (size_t i = 1; i < refined.size(); ++i) {
        if (std::abs(refined[i].second - refined.front().second) > kPeakExclusionRows) { runnerUp = refined[i].first; break; }
    }
    const double confidence = std::clamp(best + (best - runnerUp), 0.0, 1.0);
    if (best < params.minPeakScore || confidence < params.minConfidence) {
        qDebug() << "LongshotAnalyzer: ambiguous pair" << fromIndex << "->" << toIndex << "best" << best
                 << "runner-up" << runnerUp;
        return std::nullopt;
    }
    obs.shift.dy = refined.front().second;
    obs.shift.confidence = confidence;
    // Static bands are rows with ink that stayed put while the content moved.
    // Such rows exist in both frames of the pair by construction (an overlay
    // present in only one frame is indistinguishable from newly revealed
    // content within a single pair), so the band is attributed to both; the
    // pipeline resolves per frame across all of a frame's pairs.
    const StaticBands finalBands = detectStaticBands(fromImage, toImage, obs.shift.dy, params);
    obs.bandsTo = finalBands;
    obs.bandsFrom = finalBands;
    obs.movingSpanFrom = span;
    obs.movingSpanTo = span;
    return obs;
}

} // namespace SnapTray::Longshot
