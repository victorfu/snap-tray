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

constexpr int kPeakExclusionRows = 2;   // neighbours of a chosen peak are not a second peak
constexpr float kInkMeanDeviation = 8.0f; // luma: a row this far from the median row is solid ink, not background
constexpr int kMinContentRows = 32;     // fewer usable rows than this: give up on the pair
constexpr int kMinTemplateRows = 24;
constexpr double kStationaryMeanDiff = 1.5; // whole-frame mean |diff| below this = no motion
constexpr int kMinMovingColumns = 32;
constexpr double kMinTemplateStdDev = 3.0; // luma: a flatter template (or match window) scores 1.0 in OpenCV for any content
constexpr int kMinTemplateInkedRows = 6;   // rows with ink the NCC template band must contain
constexpr int kTemplateSlideStep = 8;      // rows between candidate template positions

cv::Mat gray(const QImage& image)
{
    return MatConverter::toGray(image.format() == QImage::Format_RGB32 ? image : image.convertToFormat(QImage::Format_RGB32));
}

// A row carries information when it has vertical edges (text line borders) or
// its mean luma departs from the frame's typical (background) row, which
// catches the solid interior rows of text lines, blocks and headers.
std::vector<char> inkedRows(const FrameFeatures& features, const AnalyzerParams& params)
{
    const size_t n = features.rowMean.size();
    std::vector<char> inked(n, 0);
    if (n == 0) return inked;
    std::vector<float> sorted(features.rowMean);
    std::nth_element(sorted.begin(), sorted.begin() + n / 2, sorted.end());
    const float median = sorted[n / 2];
    for (size_t y = 0; y < n; ++y) {
        inked[y] = features.rowGradient[y] >= params.inkGradientThreshold
                   || std::abs(features.rowMean[y] - median) >= kInkMeanDeviation;
    }
    return inked;
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

// Highest `count` responses at least `exclusion` rows apart, as signed
// shifts. The response has length 2 * profileRows, so bins map to shifts in
// [-profileRows, profileRows); only |shift| <= maxAbsShift is considered.
// Peaks closer than `exclusion` rows to a chosen one are skipped. Callers pass
// 2 * refineRadius + 1: NCC refinement searches +-refineRadius around each
// candidate, so candidates nearer than that would collapse onto the same dy
// and hide the true runner-up.
std::vector<int> topShifts(const std::vector<double>& response, int count, int maxAbsShift, int exclusion)
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
        if (std::any_of(chosen.begin(), chosen.end(), [&](int c) { return std::abs(c - shift) < exclusion; })) continue;
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
                                   const QRect& contentTo, const QRect& contentFrom, int templateRows,
                                   const std::vector<char>& inkedTo)
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
    // Slide the template over the overlap and keep the position holding the
    // most inked rows (ties: closest to the centre): a band that falls inside
    // a whitespace gap would otherwise match anywhere.
    const int centreTop = overlapTop + (overlapRows - tplHeight) / 2;
    int tplTop = centreTop;
    int bestInked = -1;
    auto consider = [&](int top) {
        int inked = 0;
        for (int y = top; y < top + tplHeight; ++y) inked += inkedTo[y] ? 1 : 0;
        if (inked > bestInked || (inked == bestInked && std::abs(top - centreTop) < std::abs(tplTop - centreTop))) {
            bestInked = inked;
            tplTop = top;
        }
    };
    for (int top = overlapTop; top + tplHeight <= overlapBottom; top += kTemplateSlideStep) consider(top);
    consider(overlapBottom - tplHeight);
    if (bestInked < kMinTemplateInkedRows) return {0.0, candidate};
    const cv::Rect tplRect(contentTo.left(), tplTop, contentTo.width(), tplHeight);
    if (tplRect.width <= 0 || tplRect.height <= 0) return {0.0, candidate};
    // Search window in `from`: rows [tplTop + candidate - radius, ... + tplHeight + radius).
    const int searchTop = tplTop + candidate - radius;
    const cv::Rect searchRect(contentTo.left(), searchTop, contentTo.width(), tplHeight + 2 * radius);
    cv::Mat result;
    cv::matchTemplate(from(searchRect), to(tplRect), result, cv::TM_CCOEFF_NORMED);
    double minVal = 0.0, maxVal = 0.0;
    cv::Point minLoc, maxLoc;
    cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);
    const int matchedRowInFrom = searchTop + maxLoc.y;
    cv::Scalar meanValue, stdTemplate, stdWindow;
    cv::meanStdDev(to(tplRect), meanValue, stdTemplate);
    cv::meanStdDev(from(cv::Rect(contentTo.left(), matchedRowInFrom, contentTo.width(), tplHeight)), meanValue, stdWindow);
    if (stdTemplate[0] < kMinTemplateStdDev || stdWindow[0] < kMinTemplateStdDev) return {0.0, candidate};
    return {std::isfinite(maxVal) ? maxVal : 0.0, matchedRowInFrom - tplTop};
}

} // namespace

std::vector<char> LongshotAnalyzer::rowInkFlags(const FrameFeatures& features, const AnalyzerParams& params)
{
    return inkedRows(features, params);
}

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

namespace {

StaticBands staticBandsImpl(const cv::Mat& from, const cv::Mat& to, const FrameFeatures& toFeatures, int dy,
                            const AnalyzerParams& params)
{
    StaticBands bands;
    if (dy == 0) return bands;
    if (from.size() != to.size()) return bands;
    const std::vector<double> unchanged = rowAbsDiff(from, to);
    const std::vector<char> inkedTo = inkedRows(toFeatures, params);
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
            if (inkedTo[y]) {
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

QRect movingSpanImpl(const cv::Mat& from, const cv::Mat& to, const AnalyzerParams& params)
{
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

} // namespace

StaticBands LongshotAnalyzer::detectStaticBands(const QImage& fromImage, const QImage& toImage, int dy,
                                                const AnalyzerParams& params)
{
    if (dy == 0 || fromImage.size() != toImage.size()) return StaticBands{};
    return staticBandsImpl(gray(fromImage), gray(toImage), computeFeatures(toImage, 0), dy, params);
}

QRect LongshotAnalyzer::movingColumnSpan(const QImage& fromImage, const QImage& toImage, int dy,
                                         const AnalyzerParams& params)
{
    Q_UNUSED(dy);
    return movingSpanImpl(gray(fromImage), gray(toImage), params);
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
    if (fromFeatures.rowMean.size() != size_t(height) || toFeatures.rowMean.size() != size_t(height)
        || fromFeatures.rowGradient.size() != size_t(height) || toFeatures.rowGradient.size() != size_t(height)) {
        qDebug() << "LongshotAnalyzer: feature size does not match the frame height";
        return std::nullopt;
    }

    ShiftObservation obs;
    obs.shift.from = fromIndex;
    obs.shift.to = toIndex;
    obs.movingSpanFrom = QRect(0, 0, width, height);
    obs.movingSpanTo = obs.movingSpanFrom;

    // Frames without ink carry no information: two blank frames look
    // stationary but could be anywhere inside a blank region. Refuse them.
    const std::vector<char> inkedFrom = inkedRows(fromFeatures, params);
    const std::vector<char> inkedTo = inkedRows(toFeatures, params);
    int inkedAnywhere = 0;
    for (int y = 0; y < height; ++y) {
        if (inkedFrom[y] || inkedTo[y]) ++inkedAnywhere;
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

    // Shifts are bounded by the search fraction and by the overlap a match needs.
    const int minOverlap = height / std::max(1, params.minOverlapFraction);
    const int maxAbsShift = std::min(height / std::max(1, params.maxShiftFraction), height - minOverlap);

    // Coarse pass 1: full-profile phase correlation gives a first dy.
    // Fetch two spare peaks so the pool is not starved by the exclusion window.
    const int coarsePool = params.coarseCandidates + 2;
    const int coarseExclusion = 2 * params.refineRadius + 1;
    std::vector<int> candidates = topShifts(phaseCorrelation(fromFeatures.rowMean, toFeatures.rowMean),
                                            coarsePool, maxAbsShift, coarseExclusion);
    if (candidates.empty()) return std::nullopt;

    // Static bands from the leading candidate, then redo the coarse pass on
    // content rows only so a header cannot pin the correlation at zero.
    // A zero candidate would report no bands at all, so prefer the first
    // non-zero one for the pre-pass.
    const auto firstNonZero = std::find_if(candidates.begin(), candidates.end(), [](int c) { return c != 0; });
    const int bandProbe = firstNonZero != candidates.end() ? *firstNonZero : candidates.front();
    const StaticBands bands = staticBandsImpl(from, to, toFeatures, bandProbe, params);
    const QRect span = movingSpanImpl(from, to, params);
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
    int inkedContentRows = 0;
    for (int y = contentTop; y < contentBottom; ++y) {
        if (inkedFrom[y] || inkedTo[y]) ++inkedContentRows;
    }
    if (inkedContentRows < kMinContentRows) {
        qDebug() << "LongshotAnalyzer: content area has no ink; ambiguous";
        return std::nullopt;
    }
    candidates = topShifts(phaseCorrelation(fromProfile, toProfile), coarsePool,
                           std::min(maxAbsShift, (contentBottom - contentTop) / std::max(1, params.maxShiftFraction)),
                           coarseExclusion);
    if (candidates.empty()) return std::nullopt;

    // Fine pass: NCC around each candidate; best + margin = confidence.
    const QRect contentTo(span.left(), contentTop, span.width(), contentBottom - contentTop);
    const QRect contentFrom = contentTo;
    std::vector<std::pair<double, int>> refined;
    for (int candidate : candidates) {
        if (int(refined.size()) == params.coarseCandidates) break;
        const auto result = refineByNcc(from, to, candidate, params.refineRadius, contentTo, contentFrom, params.templateRows, inkedTo);
        // Refine up to coarseCandidates *distinct* shifts.
        const bool duplicate = std::any_of(refined.begin(), refined.end(),
                                           [&](const auto& r) { return std::abs(r.second - result.second) <= kPeakExclusionRows; });
        if (!duplicate) refined.push_back(result);
    }
    std::sort(refined.begin(), refined.end(), [](const auto& l, const auto& r) { return l.first > r.first; });
    const double best = refined.front().first;
    double runnerUp = 0.0;
    for (size_t i = 1; i < refined.size(); ++i) {
        if (std::abs(refined[i].second - refined.front().second) > kPeakExclusionRows) { runnerUp = refined[i].first; break; }
    }
    const double confidence = std::clamp(best + (best - runnerUp), 0.0, 1.0);
    // Periodic content scores nearly as well at several shifts: reject on the
    // margin over the runner-up (confidence is always >= best, so it cannot).
    if (best < params.minPeakScore || best - runnerUp < params.minMargin) {
        qDebug() << "LongshotAnalyzer: ambiguous pair" << fromIndex << "->" << toIndex << "best" << best
                 << "runner-up" << runnerUp;
        return std::nullopt;
    }
    if (height - std::abs(refined.front().second) < minOverlap) {
        qDebug() << "LongshotAnalyzer: pair" << fromIndex << "->" << toIndex << "overlaps too little at dy" << refined.front().second;
        return std::nullopt;
    }
    obs.shift.dy = refined.front().second;
    obs.shift.confidence = confidence;
    // Static bands are rows with ink that stayed put while the content moved.
    // Such rows exist in both frames of the pair by construction (an overlay
    // present in only one frame is indistinguishable from newly revealed
    // content within a single pair), so the band is attributed to both; the
    // pipeline resolves per frame across all of a frame's pairs.
    const StaticBands finalBands = staticBandsImpl(from, to, toFeatures, obs.shift.dy, params);
    obs.bandsTo = finalBands;
    obs.bandsFrom = finalBands;
    obs.movingSpanFrom = span;
    obs.movingSpanTo = span;
    return obs;
}

} // namespace SnapTray::Longshot
