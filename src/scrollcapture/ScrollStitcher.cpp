#include "scrollcapture/ScrollStitcher.h"

#include <QPainter>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace {
struct PreparedFrame {
    cv::Mat edgeMat;
    double scale = 1.0;
    int trimmedX = 0;
    int trimmedWidth = 0;
};

PreparedFrame prepareFrame(const QImage& frame,
                           double trimLeftRatio,
                           double trimRightRatio)
{
    PreparedFrame prepared;
    if (frame.isNull()) {
        return prepared;
    }

    const int width = frame.width();
    const int height = frame.height();
    if (width < 10 || height < 10) {
        return prepared;
    }

    const int x0 = std::clamp(static_cast<int>(std::round(width * trimLeftRatio)), 0, width - 1);
    const int x1 = std::clamp(static_cast<int>(std::round(width * trimRightRatio)), x0 + 1, width);
    const int trimWidth = x1 - x0;
    if (trimWidth < 10) {
        return prepared;
    }

    QImage cropped = frame.copy(x0, 0, trimWidth, height).convertToFormat(QImage::Format_Grayscale8);
    cv::Mat gray(cropped.height(), cropped.width(), CV_8UC1,
                 const_cast<uchar*>(cropped.constBits()), cropped.bytesPerLine());

    constexpr int kTargetWidth = 520;
    if (gray.cols > kTargetWidth) {
        prepared.scale = static_cast<double>(kTargetWidth) / static_cast<double>(gray.cols);
    } else {
        prepared.scale = 1.0;
    }

    cv::Mat resized;
    cv::resize(gray, resized, cv::Size(), prepared.scale, prepared.scale,
               prepared.scale < 1.0 ? cv::INTER_AREA : cv::INTER_LINEAR);

    cv::Mat blurred;
    cv::GaussianBlur(resized, blurred, cv::Size(3, 3), 0.0);

    cv::Mat edges;
    cv::Canny(blurred, edges, 50, 150);

    prepared.edgeMat = edges;
    prepared.trimmedX = x0;
    prepared.trimmedWidth = trimWidth;
    return prepared;
}

struct Candidate {
    bool valid = false;
    double score = 0.0;
    int deltaDownscaled = 0;
    QString message;
};

Candidate findBestDelta(const cv::Mat& previous,
                        const cv::Mat& current,
                        ScrollDirection direction,
                        bool multiTemplate,
                        double matchThreshold,
                        int minDeltaDownscaled)
{
    Candidate best;
    if (previous.empty() || current.empty() || previous.cols != current.cols) {
        best.message = QStringLiteral("Invalid frame size for matching.");
        return best;
    }

    const int h = current.rows;
    const int minTemplate = std::min(std::max(24, h / 8), h - 2);
    const int maxTemplate = std::max(minTemplate + 1, std::min(180, h - 1));
    const int th = std::clamp(static_cast<int>(std::round(h * 0.18)), minTemplate, maxTemplate);

    std::array<double, 3> downRatios{0.08, 0.25, 0.45};
    std::array<double, 3> upRatios{0.55, 0.72, 0.88};
    const auto& ratios = (direction == ScrollDirection::Down) ? downRatios : upRatios;
    const int candidateCount = multiTemplate ? static_cast<int>(ratios.size()) : 1;

    for (int i = 0; i < candidateCount; ++i) {
        const int templateY = std::clamp(static_cast<int>(std::round(h * ratios[i])), 0, h - th);
        const cv::Mat templ = current.rowRange(templateY, templateY + th);

        cv::Mat result;
        cv::matchTemplate(previous, templ, result, cv::TM_CCOEFF_NORMED);

        double maxVal = 0.0;
        cv::Point maxLoc;
        cv::minMaxLoc(result, nullptr, &maxVal, nullptr, &maxLoc);

        const int bestY = maxLoc.y;
        const int delta = (direction == ScrollDirection::Down)
            ? (bestY - templateY)
            : (templateY - bestY);
        const bool legalDelta = delta >= minDeltaDownscaled && delta <= (h - th);

        Candidate candidate;
        candidate.valid = (maxVal >= matchThreshold) && legalDelta;
        candidate.score = maxVal;
        candidate.deltaDownscaled = delta;
        candidate.message = QStringLiteral("score=%1 delta=%2")
            .arg(maxVal, 0, 'f', 3)
            .arg(delta);

        if (candidate.valid && (!best.valid || candidate.score > best.score)) {
            best = candidate;
        } else if (!best.valid && candidate.score > best.score) {
            best = candidate;
        }
    }

    if (!best.valid && best.message.isEmpty()) {
        best.message = QStringLiteral("No valid template match.");
    }
    return best;
}

Candidate estimateDeltaFromRowCorrelation(const cv::Mat& previous,
                                          const cv::Mat& current,
                                          ScrollDirection direction,
                                          int minDeltaDownscaled,
                                          int maxDeltaDownscaled)
{
    Candidate best;
    if (previous.empty() || current.empty() || previous.size() != current.size()) {
        best.message = QStringLiteral("Invalid frame size for row-correlation fallback.");
        return best;
    }

    const int h = current.rows;
    if (h < 40 || minDeltaDownscaled > maxDeltaDownscaled) {
        best.message = QStringLiteral("Insufficient rows for row-correlation fallback.");
        return best;
    }

    std::vector<double> prevProfile(h, 0.0);
    std::vector<double> currProfile(h, 0.0);
    for (int y = 0; y < h; ++y) {
        prevProfile[y] = cv::mean(previous.row(y))[0];
        currProfile[y] = cv::mean(current.row(y))[0];
    }

    double bestCorr = -std::numeric_limits<double>::infinity();
    double secondBestCorr = -std::numeric_limits<double>::infinity();
    int bestDelta = 0;

    for (int delta = minDeltaDownscaled; delta <= maxDeltaDownscaled; ++delta) {
        const int overlap = h - delta;
        if (overlap < 36) {
            break;
        }

        double sumA = 0.0;
        double sumB = 0.0;
        double sumAA = 0.0;
        double sumBB = 0.0;
        double sumAB = 0.0;

        for (int i = 0; i < overlap; ++i) {
            const int prevIndex = (direction == ScrollDirection::Down) ? (i + delta) : i;
            const int currIndex = (direction == ScrollDirection::Down) ? i : (i + delta);

            const double a = prevProfile[prevIndex];
            const double b = currProfile[currIndex];

            sumA += a;
            sumB += b;
            sumAA += a * a;
            sumBB += b * b;
            sumAB += a * b;
        }

        const double n = static_cast<double>(overlap);
        const double cov = sumAB - (sumA * sumB / n);
        const double varA = sumAA - (sumA * sumA / n);
        const double varB = sumBB - (sumB * sumB / n);
        if (varA <= 1e-6 || varB <= 1e-6) {
            continue;
        }

        const double corr = cov / std::sqrt(varA * varB);
        if (corr > bestCorr) {
            secondBestCorr = bestCorr;
            bestCorr = corr;
            bestDelta = delta;
        } else if (corr > secondBestCorr) {
            secondBestCorr = corr;
        }
    }

    if (bestDelta <= 0 || !std::isfinite(bestCorr)) {
        best.message = QStringLiteral("Row-correlation fallback found no valid delta.");
        return best;
    }

    const double margin = std::isfinite(secondBestCorr) ? (bestCorr - secondBestCorr) : bestCorr;
    const bool confident = (bestCorr >= 0.30 && margin >= 0.005) || bestCorr >= 0.45;

    best.valid = confident;
    best.score = std::clamp(bestCorr, -1.0, 1.0);
    best.deltaDownscaled = bestDelta;
    best.message = QStringLiteral("row_corr=%1 margin=%2 delta=%3")
        .arg(bestCorr, 0, 'f', 3)
        .arg(margin, 0, 'f', 3)
        .arg(bestDelta);
    return best;
}
} // namespace

void ScrollStitcher::reset()
{
    m_segments.clear();
    m_width = 0;
    m_totalHeight = 0;
    m_frameCount = 0;
    m_initialized = false;
}

bool ScrollStitcher::initialize(const QImage& firstContentFrame)
{
    reset();
    if (firstContentFrame.isNull()) {
        return false;
    }

    m_width = firstContentFrame.width();
    m_totalHeight = firstContentFrame.height();
    m_frameCount = 1;
    m_segments.push_back(firstContentFrame);
    m_initialized = true;
    return true;
}

void ScrollStitcher::setOptions(const MatchOptions& options)
{
    m_options = options;
}

ScrollStitcher::MatchResult ScrollStitcher::appendFromPair(const QImage& prevContentFrame,
                                                            const QImage& currContentFrame)
{
    MatchResult matchResult;
    if (!m_initialized || prevContentFrame.isNull() || currContentFrame.isNull()) {
        matchResult.message = QStringLiteral("Stitcher not initialized.");
        return matchResult;
    }
    if (currContentFrame.width() < m_width || currContentFrame.height() < 2) {
        matchResult.message = QStringLiteral("Current frame too small.");
        return matchResult;
    }

    PreparedFrame previousPrepared = prepareFrame(prevContentFrame,
                                                  m_options.xTrimLeftRatio,
                                                  m_options.xTrimRightRatio);
    PreparedFrame currentPrepared = prepareFrame(currContentFrame,
                                                 m_options.xTrimLeftRatio,
                                                 m_options.xTrimRightRatio);

    if (previousPrepared.edgeMat.empty() || currentPrepared.edgeMat.empty()) {
        matchResult.message = QStringLiteral("Unable to preprocess frames.");
        return matchResult;
    }

    const int h = currentPrepared.edgeMat.rows;
    const int minTemplate = std::min(std::max(24, h / 8), h - 2);
    const int maxTemplate = std::max(minTemplate + 1, std::min(180, h - 1));
    const int th = std::clamp(static_cast<int>(std::round(h * 0.18)), minTemplate, maxTemplate);
    const int maxLegalDelta = std::max(m_options.minDeltaDownscaled, h - th);

    Candidate candidate = findBestDelta(previousPrepared.edgeMat,
                                        currentPrepared.edgeMat,
                                        m_options.direction,
                                        m_options.useMultiTemplate,
                                        m_options.matchThreshold,
                                        m_options.minDeltaDownscaled);
    bool usedCorrelationFallback = false;
    if (!candidate.valid) {
        Candidate corrCandidate = estimateDeltaFromRowCorrelation(previousPrepared.edgeMat,
                                                                  currentPrepared.edgeMat,
                                                                  m_options.direction,
                                                                  m_options.minDeltaDownscaled,
                                                                  maxLegalDelta);
        if (corrCandidate.valid) {
            candidate = corrCandidate;
            usedCorrelationFallback = true;
        }
    }

    matchResult.score = candidate.score;
    matchResult.weak = !candidate.valid || usedCorrelationFallback;
    matchResult.message = candidate.message;
    if (!candidate.valid) {
        return matchResult;
    }

    const double scale = currentPrepared.scale;
    if (scale <= 0.0) {
        matchResult.message = QStringLiteral("Invalid scale for delta conversion.");
        return matchResult;
    }

    int deltaFull = static_cast<int>(std::round(candidate.deltaDownscaled / scale));
    deltaFull = std::clamp(deltaFull, 0, currContentFrame.height());
    matchResult.deltaFullPx = deltaFull;

    if (deltaFull <= 0) {
        matchResult.message = QStringLiteral("deltaFull is zero.");
        return matchResult;
    }

    matchResult.valid = appendStrip(currContentFrame, deltaFull);
    if (!matchResult.valid) {
        matchResult.message = QStringLiteral("Failed to append strip.");
    }
    return matchResult;
}

bool ScrollStitcher::appendByDelta(const QImage& currContentFrame, int deltaFullPx)
{
    if (!m_initialized || currContentFrame.isNull()) {
        return false;
    }

    const int maxDelta = std::max(1, currContentFrame.height() - 1);
    const int clampedDelta = std::clamp(deltaFullPx, 1, maxDelta);
    return appendStrip(currContentFrame, clampedDelta);
}

bool ScrollStitcher::appendStrip(const QImage& currentFrame, int deltaFullPx)
{
    if (currentFrame.width() < m_width || deltaFullPx <= 0) {
        return false;
    }

    const int stripHeight = std::clamp(deltaFullPx, 1, currentFrame.height());
    QImage strip;

    if (m_options.direction == ScrollDirection::Down) {
        const int startY = currentFrame.height() - stripHeight;
        strip = currentFrame.copy(0, startY, m_width, stripHeight);
        m_segments.push_back(strip);
    } else {
        strip = currentFrame.copy(0, 0, m_width, stripHeight);
        m_segments.push_front(strip);
    }

    m_totalHeight += strip.height();
    ++m_frameCount;
    return true;
}

QPixmap ScrollStitcher::compose() const
{
    if (!m_initialized || m_segments.empty() || m_width <= 0 || m_totalHeight <= 0) {
        return {};
    }

    QImage composed(m_width, m_totalHeight, QImage::Format_ARGB32_Premultiplied);
    composed.fill(Qt::transparent);

    QPainter painter(&composed);
    int y = 0;
    for (const QImage& segment : m_segments) {
        painter.drawImage(0, y, segment);
        y += segment.height();
    }
    painter.end();

    return QPixmap::fromImage(composed);
}
