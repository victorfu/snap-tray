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
constexpr double kStationaryMaxPixelDiff = 1.5; // tolerate at most one luma level of rounding per pixel
constexpr int kMinMovingColumns = 32;
// One or two unchanged edge rows can be codec ringing after a small scroll.
// Do not turn that isolated evidence into a mask covering the entire edge band.
constexpr int kMinStaticEvidenceRows = 3;
constexpr double kColumnMotionAdvantage = 0.25; // ignore sub-luma codec/rounding noise
constexpr double kColumnAlignmentRatio = 0.5;
// A static edge run is a side panel only with a contiguous group of textured
// columns; blank page margins and isolated codec fringes are not panels.
constexpr double kStaticColumnInkStdDev = 4.0;
constexpr int kMinStaticInkColumns = 8; // a codec fringe is not a textured panel
constexpr double kMinTemplateStdDev = 3.0; // luma: a flatter template (or match window) scores 1.0 in OpenCV for any content
constexpr int kMinTemplateInkedRows = 6;   // rows with ink the NCC template band must contain
constexpr int kTemplateSlideStep = 8;      // rows between candidate template positions
// Zoom check: a template scaled by these factors must not fit the other frame
// better than the unscaled template by more than kScaleMargin (NCC units).
constexpr double kScaleFactors[] = {0.9, 1.1};
constexpr double kScaleMargin = 0.05;
// The zoom check crops the (scaled) template to this fraction of the moving
// span so it can slide horizontally, and searches this fraction of the frame
// and template height (plus the refine radius) above and below the match.
constexpr double kScaleTemplateWidthFraction = 0.8;
constexpr double kScaleSearchFraction = 0.1;

cv::Mat gray(const QImage& image)
{
    return MatConverter::toGray(image.format() == QImage::Format_RGB32 ? image : image.convertToFormat(QImage::Format_RGB32));
}

// A row carries information when it has vertical edges (text line borders) or
// its mean luma departs from the frame's typical (background) row, which
// catches the solid interior rows of text lines, blocks and headers. Horizontal
// contrast relative to the typical row also retains narrow text in wide crops;
// a constant vertical stripe alone provides no evidence of vertical position.
std::vector<char> inkedRows(const FrameFeatures& features, const AnalyzerParams& params)
{
    const size_t n = features.rowMean.size();
    std::vector<char> inked(n, 0);
    if (n == 0) return inked;
    std::vector<float> sorted(features.rowMean);
    std::nth_element(sorted.begin(), sorted.begin() + n / 2, sorted.end());
    const float median = sorted[n / 2];
    float medianTexture = 0.0f;
    if (features.rowTexture.size() == n) {
        auto texture = features.rowTexture;
        std::nth_element(texture.begin(), texture.begin() + n / 2, texture.end());
        medianTexture = texture[n / 2];
    }
    for (size_t y = 0; y < n; ++y) {
        inked[y] = features.rowGradient[y] >= params.inkGradientThreshold
                   || std::abs(features.rowMean[y] - median) >= kInkMeanDeviation
                   || (features.rowTexture.size() == n && features.rowTexture[y] - medianTexture >= kMinTemplateStdDev);
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
        if (std::any_of(chosen.begin(), chosen.end(), [shift = shift, exclusion](int c) { return std::abs(c - shift) < exclusion; })) continue;
        chosen.push_back(shift);
        if (int(chosen.size()) == count) break;
    }
    return chosen;
}

struct NccMatch {
    double score = 0.0;
    int dy = 0;
    cv::Rect templateRect; // in `to`; empty when no template could be placed
};

// NCC of a template band taken from `to` (content rows only) against `from`
// shifted by candidate dy, searched within +-radius. Returns the best score,
// the refined dy and the template used. With dy = pageOffset(to) -
// pageOffset(from): a row y of `to` shows page row offsetTo + y, which in
// `from` is row y + dy.
NccMatch refineByNcc(const cv::Mat& from, const cv::Mat& to, int candidate, int radius,
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
    if (overlapRows < kMinTemplateRows) return {0.0, candidate, {}};
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
    if (bestInked < kMinTemplateInkedRows) return {0.0, candidate, {}};
    const cv::Rect tplRect(contentTo.left(), tplTop, contentTo.width(), tplHeight);
    if (tplRect.width <= 0 || tplRect.height <= 0) return {0.0, candidate, {}};
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
    if (stdTemplate[0] < kMinTemplateStdDev || stdWindow[0] < kMinTemplateStdDev) return {0.0, candidate, {}};
    return {std::isfinite(maxVal) ? maxVal : 0.0, matchedRowInFrom - tplTop, tplRect};
}

// A noisy pause can look like two enormous static bands surrounding a few
// changed codec rows. Verify zero motion locally before trusting that mask:
// sparse moving text must veto a stationary header, and repeated patterns must
// still provide a unique zero-shift match rather than mere visual similarity.
bool verifiedNoisyPause(const cv::Mat& from, const cv::Mat& to, const std::vector<char>& inked,
                        int maxShift, const AnalyzerParams& params)
{
    constexpr int kPatchSide = 64;
    constexpr double kStationaryNcc = 0.98;
    constexpr double kScoreRoundingTolerance = 0.001;
    // Leave room to compare nonzero shifts even for the smallest supported crop.
    const int patchRows = std::min(kPatchSide, to.rows / 2);
    if (patchRows < kMinTemplateRows) return false;
    std::vector<char> supported(to.rows, 0);
    for (int top = 0; top < to.rows; top += patchRows) {
        const int y = std::min(top, to.rows - patchRows);
        const int height = patchRows;
        for (int left = 0; left < to.cols; left += kPatchSide) {
            const cv::Rect tile(left, y, std::min(kPatchSide, to.cols - left), height);
            cv::Scalar mean, fromStd, toStd;
            cv::meanStdDev(from(tile), mean, fromStd);
            cv::meanStdDev(to(tile), mean, toStd);
            if (std::max(fromStd[0], toStd[0]) < kMinTemplateStdDev) continue;
            if (std::min(fromStd[0], toStd[0]) < kMinTemplateStdDev) return false;
            const int searchTop = std::max(0, y - maxShift);
            const int searchBottom = std::min(from.rows, y + height + maxShift);
            cv::Mat scores;
            cv::matchTemplate(from(cv::Rect(left, searchTop, tile.width, searchBottom - searchTop)),
                              to(tile), scores, cv::TM_CCOEFF_NORMED);
            const double zero = scores.at<float>(y - searchTop, 0);
            if (!std::isfinite(zero) || zero < std::max(kStationaryNcc, params.minPeakScore)) return false;
            double runner = 0.0;
            for (int row = 0; row < scores.rows; ++row) {
                const int shift = searchTop + row - y;
                if (shift == 0) continue;
                const double score = scores.at<float>(row, 0);
                if (!std::isfinite(score)) continue;
                const bool betterShift = score > zero + kScoreRoundingTolerance;
                const bool nextRunner = std::abs(shift) > kPeakExclusionRows && score > runner;
                if (!betterShift && !nextRunner) continue;
                cv::Scalar candidateStd;
                cv::meanStdDev(from(cv::Rect(left, searchTop + row, tile.width, height)), mean, candidateStd);
                if (candidateStd[0] < kMinTemplateStdDev) continue;
                if (betterShift) return false;
                if (nextRunner) runner = score;
            }
            if (zero - runner >= params.minMargin) {
                for (int row = y; row < y + height; ++row) {
                    if (inked[size_t(row)]) supported[size_t(row)] = 1;
                }
            }
        }
    }
    return std::count(supported.begin(), supported.end(), char(1)) >= kMinContentRows;
}

// Column-mean profile of rows [top, bottom) over the columns of `span`.
std::vector<float> columnMeans(const cv::Mat& image, int top, int bottom, const QRect& span)
{
    cv::Mat means;
    cv::reduce(image(cv::Rect(span.left(), top, span.width(), bottom - top)), means, 0, cv::REDUCE_AVG, CV_32F);
    return std::vector<float>(means.begin<float>(), means.end<float>());
}

// Best TM_CCOEFF_NORMED score of `tpl` resized by `scale`, cropped to
// `cropWidth` centred columns, anywhere inside `window` of `from`.
double scaledTemplateFit(const cv::Mat& from, const cv::Mat& tpl, double scale, const cv::Rect& window, int cropWidth)
{
    cv::Mat scaled;
    if (scale == 1.0) scaled = tpl;
    else cv::resize(tpl, scaled, cv::Size(), scale, scale, scale < 1.0 ? cv::INTER_AREA : cv::INTER_LINEAR);
    const int width = std::min(cropWidth, scaled.cols);
    if (width <= 0 || scaled.rows <= 0) return 0.0;
    const cv::Mat cropped = scaled(cv::Rect((scaled.cols - width) / 2, 0, width, scaled.rows));
    if (cropped.cols > window.width || cropped.rows > window.height) return 0.0;
    cv::Mat result;
    cv::matchTemplate(from(window), cropped, result, cv::TM_CCOEFF_NORMED);
    double maxVal = 0.0;
    cv::minMaxLoc(result, nullptr, &maxVal);
    return std::isfinite(maxVal) ? maxVal : 0.0;
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
    f.rowTexture.resize(g.rows);
    for (int y = 0; y < g.rows; ++y) {
        cv::Scalar mean, deviation;
        cv::meanStdDev(g.row(y), mean, deviation);
        f.rowMean[y] = float(mean[0]);
        f.rowTexture[y] = float(deviation[0]);
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
        int evidenceRows = 0;
        bool supported = false;
        int y = start;
        while (y >= 0 && y < to.rows) {
            if (inkedTo[y]) {
                if (unchanged[y] > params.staticRowDiffThreshold) break;
                // Unchanged only because the scroll moved it onto an identical
                // row (inside a text line after a small shift) is no evidence
                // of an overlay: skip it like a blank row.
                if (explainedByScroll(y)) { evidenceRows = 0; y += step; continue; }
                ++evidenceRows;
                supported = supported || evidenceRows >= kMinStaticEvidenceRows;
                band = step > 0 ? y + 1 : to.rows - y;
            } else {
                evidenceRows = 0;
            }
            y += step;
        }
        return supported ? band : 0;
    };
    bands.top = scan(0, 1);
    bands.bottom = scan(to.rows - 1, -1);
    // A whole-frame "band" means the frame did not move at all: that is
    // stationary, not a header, and the caller handles it before calling here.
    if (bands.top + bands.bottom >= to.rows) return StaticBands{};
    return bands;
}

QRect movingSpanImpl(const cv::Mat& from, const cv::Mat& to, int dy, const AnalyzerParams& params)
{
    const QRect full(0, 0, to.cols, to.rows);
    if (from.size() != to.size()) return full;
    const std::vector<double> unchanged = columnAbsDiff(from, to);
    // Like static bands, a static run needs ink: uniform margin columns carry
    // no evidence of an overlay and stay inside the span.
    auto columnStd = [](const cv::Mat& m) {
        cv::Mat f, mean, meanSq;
        m.convertTo(f, CV_32F);
        cv::reduce(f, mean, 0, cv::REDUCE_AVG, CV_32F);
        cv::reduce(f.mul(f), meanSq, 0, cv::REDUCE_AVG, CV_32F);
        std::vector<double> out(m.cols);
        for (int x = 0; x < m.cols; ++x) {
            const double mu = mean.at<float>(0, x);
            out[x] = std::sqrt(std::max(0.0, double(meanSq.at<float>(0, x)) - mu * mu));
        }
        return out;
    };
    const std::vector<double> stdFrom = columnStd(from);
    const std::vector<double> stdTo = columnStd(to);
    // Sparse text can change too few pixels to exceed a whole-column mean
    // threshold. A substantially better match after the known vertical shift
    // is positive evidence of scrolling content, not a fixed side panel.
    std::vector<double> aligned;
    std::vector<double> overlapUnchanged;
    const int overlap = to.rows - std::abs(dy);
    if (dy != 0 && overlap > 0) {
        const int fromTop = std::max(0, dy);
        const int toTop = std::max(0, -dy);
        const cv::Mat target = to.rowRange(toTop, toTop + overlap);
        aligned = columnAbsDiff(from.rowRange(fromTop, fromTop + overlap), target);
        overlapUnchanged = columnAbsDiff(from.rowRange(toTop, toTop + overlap), target);
    }
    auto moving = [&](int x) {
        if (unchanged[x] >= params.movingColumnDiffThreshold) return true;
        return !aligned.empty() && std::min(stdFrom[x], stdTo[x]) >= kStaticColumnInkStdDev
            && aligned[x] + kColumnMotionAdvantage < overlapUnchanged[x]
            && aligned[x] < kColumnAlignmentRatio * overlapUnchanged[x];
    };
    int left = 0;
    while (left < to.cols && !moving(left)) ++left;
    int right = to.cols - 1;
    while (right > left && !moving(right)) --right;
    if (right - left + 1 < kMinMovingColumns) return full;
    // A newly revealed rule or word is not a panel in the other frame.
    // Require texture in both observations, rather than borrowing one frame's ink.
    auto runHasInk = [&](int begin, int end) { // [begin, end)
        int inkColumns = 0;
        for (int x = begin; x < end; ++x) {
            inkColumns = std::min(stdFrom[x], stdTo[x]) >= kStaticColumnInkStdDev ? inkColumns + 1 : 0;
            if (inkColumns >= kMinStaticInkColumns) return true;
        }
        return false;
    };
    if (!runHasInk(0, left)) left = 0;
    if (!runHasInk(right + 1, to.cols)) right = to.cols - 1;
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
    return movingSpanImpl(gray(fromImage), gray(toImage), dy, params);
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

    // Only bypass motion matching when every pixel is unchanged (apart from
    // rounding). A whole-frame mean lets white margins hide slow motion in
    // sparse content and introduces a false zero-shift edge on every frame.
    if (cv::norm(from, to, cv::NORM_INF) < kStationaryMaxPixelDiff) {
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
    const QRect span = movingSpanImpl(from, to, bandProbe, params);
    const int contentTop = bands.top;
    const int contentBottom = height - bands.bottom; // exclusive
    if (contentBottom - contentTop < kMinContentRows) {
        if (candidates.front() == 0 && verifiedNoisyPause(from, to, inkedTo, maxAbsShift, params)) {
            obs.stationary = true;
            obs.shift.dy = 0;
            obs.shift.confidence = 1.0;
            return obs;
        }
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
    std::vector<NccMatch> refined;
    for (int candidate : candidates) {
        if (int(refined.size()) == params.coarseCandidates) break;
        const NccMatch result = refineByNcc(from, to, candidate, params.refineRadius, contentTo, contentFrom, params.templateRows, inkedTo);
        // Refine up to coarseCandidates *distinct* shifts.
        const bool duplicate = std::any_of(refined.begin(), refined.end(),
                                           [&](const NccMatch& r) { return std::abs(r.dy - result.dy) <= kPeakExclusionRows; });
        if (!duplicate) refined.push_back(result);
    }
    std::sort(refined.begin(), refined.end(), [](const NccMatch& l, const NccMatch& r) { return l.score > r.score; });
    const double best = refined.front().score;
    double runnerUp = 0.0;
    for (size_t i = 1; i < refined.size(); ++i) {
        if (std::abs(refined[i].dy - refined.front().dy) > kPeakExclusionRows) { runnerUp = refined[i].score; break; }
    }
    const double confidence = std::clamp(best + (best - runnerUp), 0.0, 1.0);
    // Periodic content scores nearly as well at several shifts: reject on the
    // margin over the runner-up (confidence is always >= best, so it cannot).
    if (best < params.minPeakScore || best - runnerUp < params.minMargin) {
        qDebug() << "LongshotAnalyzer: ambiguous pair" << fromIndex << "->" << toIndex << "best" << best
                 << "runner-up" << runnerUp;
        return std::nullopt;
    }
    const int dy = refined.front().dy;
    if (height - std::abs(dy) < minOverlap) {
        qDebug() << "LongshotAnalyzer: pair" << fromIndex << "->" << toIndex << "overlaps too little at dy" << dy;
        return std::nullopt;
    }

    // The engine is vertical-only: horizontal motion or a zoom between the
    // frames cannot be stitched, so such pairs are refused, not approximated.
    // Horizontal: phase-correlate the column-mean profiles of the rows both
    // frames show (to row y <-> from row y + dy) over the moving span.
    const int overlapTop = std::max(contentTop, contentTop - dy);
    const int overlapBottom = std::min(contentBottom, contentBottom - dy); // exclusive, rows of `to`
    if (overlapBottom > overlapTop) {
        const std::vector<float> fromColumns = columnMeans(from, overlapTop + dy, overlapBottom + dy, span);
        const std::vector<float> toColumns = columnMeans(to, overlapTop, overlapBottom, span);
        const std::vector<int> dxPeak = topShifts(phaseCorrelation(fromColumns, toColumns), 1, span.width() / 2, 1);
        // Bin k means "to column x shows from column x + k": content moved by -k.
        const int dx = dxPeak.empty() ? 0 : -dxPeak.front();
        if (std::abs(dx) > params.maxHorizontalShift) {
            qDebug() << "LongshotAnalyzer: pair" << fromIndex << "->" << toIndex << "moved horizontally by" << dx << "px; refused";
            return std::nullopt;
        }
    }
    // Zoom: the winning template, scaled by 0.9 and 1.1, must not fit `from`
    // clearly better than unscaled. All three use the same centred crop and
    // search window so the comparison is like for like.
    const cv::Rect tpl = refined.front().templateRect;
    if (!tpl.empty()) {
        const int cropWidth = int(span.width() * kScaleTemplateWidthFraction);
        const int margin = int(std::ceil(kScaleSearchFraction * (height + tpl.height))) + params.refineRadius;
        const int windowTop = std::max(0, tpl.y + dy - margin);
        const int windowBottom = std::min(height, tpl.y + dy + tpl.height + margin);
        const cv::Rect window(span.left(), windowTop, span.width(), windowBottom - windowTop);
        const cv::Mat templateBand = to(tpl);
        const double unscaled = scaledTemplateFit(from, templateBand, 1.0, window, cropWidth);
        for (double scale : kScaleFactors) {
            const double fit = scaledTemplateFit(from, templateBand, scale, window, cropWidth);
            if (fit > unscaled + kScaleMargin) {
                qDebug() << "LongshotAnalyzer: pair" << fromIndex << "->" << toIndex << "fits better scaled by" << scale
                         << "(" << fit << "vs" << unscaled << "); refused";
                return std::nullopt;
            }
        }
    }

    obs.shift.dy = dy;
    obs.shift.confidence = confidence;
    // Noisy pauses can reach the matcher instead of the equality shortcut.
    // Preserve their mask propagation once zero motion has been verified.
    obs.stationary = dy == 0;
    // Static bands are rows with ink that stayed put while the content moved.
    // Such rows exist in both frames of the pair by construction (an overlay
    // present in only one frame is indistinguishable from newly revealed
    // content within a single pair), so the band is attributed to both; the
    // pipeline resolves per frame across all of a frame's pairs.
    const StaticBands finalBands = staticBandsImpl(from, to, toFeatures, obs.shift.dy, params);
    obs.bandsTo = finalBands;
    obs.bandsFrom = finalBands;
    obs.movingSpanFrom = movingSpanImpl(from, to, dy, params);
    obs.movingSpanTo = obs.movingSpanFrom;
    return obs;
}

} // namespace SnapTray::Longshot
