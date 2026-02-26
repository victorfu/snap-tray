#include "detection/ScrollAlignmentEngine.h"

#include "utils/MatConverter.h"

#include <QtGlobal>

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <limits>
#include <algorithm>
#include <QSet>
#include <QVector>

namespace {

constexpr int kMinTemplateHeight = 16;
constexpr int kMinCompareWidth = 32;
constexpr int kTopKPeaksPerBand = 3;
constexpr int kPeakSeparationDy = 6;
constexpr int kDyClusterRadius = 5;
constexpr double kLowTextureStddev = 6.0;

cv::Mat toGrayMat(const QImage &image)
{
    if (image.isNull()) {
        return cv::Mat();
    }
    return MatConverter::toGray(image);
}

struct BandSpec {
    int startY = 0;
    int height = 0;
    double texture = 0.0;
};

struct NccSample {
    int bandIndex = -1;
    int templateStartY = 0;
    int templateHeight = 0;
    int dy = 0;
    int dx = 0;
    int overlap = 0;
    int matchY = 0;
    int prevX = 0;
    int currX = 0;
    int compareWidth = 0;
    double score = -1.0;
    double mad = 1.0;
};

struct DyCluster {
    QVector<NccSample> items;
    int representativeDy = 0;
    int supportBands = 0;
    double spread = 0.0;
    double meanScore = 0.0;
    double maxScore = 0.0;
    double consensusScore = 0.0;
    double penalty = 0.0;
};

double normalizedScore(double value)
{
    if (!std::isfinite(value)) {
        return 0.0;
    }
    return qBound(0.0, value, 1.0);
}

double bandTextureScore(const cv::Mat &gray, int startY, int height)
{
    if (gray.empty() || height <= 0 || startY < 0 || startY + height > gray.rows) {
        return 0.0;
    }

    const cv::Rect roiRect(0, startY, gray.cols, height);
    const cv::Mat roi(gray, roiRect);

    cv::Scalar mean;
    cv::Scalar stddev;
    cv::meanStdDev(roi, mean, stddev);

    cv::Mat gradX;
    cv::Sobel(roi, gradX, CV_16S, 1, 0, 3);
    cv::Mat absGradX;
    cv::convertScaleAbs(gradX, absGradX);
    const double edge = cv::mean(absGradX)[0] / 255.0;

    // Texture prefers high variance and visible horizontal details.
    return stddev[0] + edge * 32.0;
}

QVector<BandSpec> selectTemplateBands(const cv::Mat &gray,
                                      const ScrollAlignmentOptions &options)
{
    QVector<BandSpec> candidates;
    const int height = gray.rows;
    if (height <= 0) {
        return candidates;
    }

    auto addBand = [&](double startRatio, double endRatio) {
        const double clampedStart = qBound(0.01, startRatio, 0.88);
        const double clampedEnd = qBound(clampedStart + 0.02, endRatio, 0.96);
        const int startY = qBound(0, qRound(static_cast<double>(height) * clampedStart), height - 1);
        const int endY = qBound(startY + 1, qRound(static_cast<double>(height) * clampedEnd), height);
        const int bandHeight = qBound(kMinTemplateHeight,
                                      endY - startY,
                                      qMax(kMinTemplateHeight, height - startY));
        if (startY + bandHeight > height) {
            return;
        }

        BandSpec band;
        band.startY = startY;
        band.height = bandHeight;
        band.texture = bandTextureScore(gray, startY, bandHeight);
        candidates.push_back(band);
    };

    addBand(options.templateTopStartRatio, options.templateTopEndRatio);
    addBand(0.30, 0.40);
    addBand(0.40, 0.50);

    // Content-driven candidates: sample the upper-to-middle area where overlap is most stable.
    const double scanBandHeightRatio = qBound(0.06,
                                              options.templateTopEndRatio - options.templateTopStartRatio,
                                              0.20);
    for (double start = 0.14; start <= 0.58; start += 0.06) {
        addBand(start, start + scanBandHeightRatio);
    }

    std::sort(candidates.begin(), candidates.end(), [](const BandSpec &a, const BandSpec &b) {
        if (a.texture == b.texture) {
            return a.startY < b.startY;
        }
        return a.texture > b.texture;
    });

    QVector<BandSpec> selected;
    for (const BandSpec &candidate : candidates) {
        bool overlaps = false;
        for (const BandSpec &existing : selected) {
            const int centerDelta = qAbs((existing.startY + existing.height / 2) -
                                         (candidate.startY + candidate.height / 2));
            if (centerDelta < qMax(existing.height, candidate.height) / 2) {
                overlaps = true;
                break;
            }
        }
        if (overlaps) {
            continue;
        }
        selected.push_back(candidate);
        if (selected.size() >= 3) {
            break;
        }
    }

    return selected;
}

void insertPeakWithSeparation(QVector<NccSample> *peaks, const NccSample &candidate)
{
    if (!peaks) {
        return;
    }

    for (int i = 0; i < peaks->size(); ++i) {
        NccSample &existing = (*peaks)[i];
        if (qAbs(existing.dy - candidate.dy) < kPeakSeparationDy) {
            if (candidate.score > existing.score) {
                existing = candidate;
            }
            return;
        }
    }

    peaks->push_back(candidate);
    std::sort(peaks->begin(), peaks->end(), [](const NccSample &a, const NccSample &b) {
        return a.score > b.score;
    });
    if (peaks->size() > kTopKPeaksPerBand) {
        peaks->resize(kTopKPeaksPerBand);
    }
}

double computePatchMad(const cv::Mat &previousGray,
                       const cv::Mat &currentGray,
                       const NccSample &sample)
{
    if (sample.compareWidth < 8 || sample.templateHeight < 8) {
        return 1.0;
    }

    if (sample.currX < 0 || sample.prevX < 0 || sample.matchY < 0 ||
        sample.currX + sample.compareWidth > currentGray.cols ||
        sample.prevX + sample.compareWidth > previousGray.cols ||
        sample.templateStartY + sample.templateHeight > currentGray.rows ||
        sample.matchY + sample.templateHeight > previousGray.rows) {
        return 1.0;
    }

    const cv::Rect templRect(sample.currX,
                             sample.templateStartY,
                             sample.compareWidth,
                             sample.templateHeight);
    const cv::Rect matchRect(sample.prevX,
                             sample.matchY,
                             sample.compareWidth,
                             sample.templateHeight);

    cv::Mat absDiff;
    cv::absdiff(currentGray(templRect), previousGray(matchRect), absDiff);
    return qBound(0.0, cv::mean(absDiff)[0] / 255.0, 1.0);
}

QVector<DyCluster> clusterCandidates(const QVector<NccSample> &samples,
                                     const ScrollAlignmentOptions &options,
                                     int targetDy)
{
    QVector<DyCluster> clusters;
    if (samples.isEmpty()) {
        return clusters;
    }

    QVector<NccSample> ordered = samples;
    std::sort(ordered.begin(), ordered.end(), [](const NccSample &a, const NccSample &b) {
        if (a.dy == b.dy) {
            return a.score > b.score;
        }
        return a.dy < b.dy;
    });

    for (const NccSample &sample : ordered) {
        int clusterIndex = -1;
        int bestDistance = std::numeric_limits<int>::max();
        for (int i = 0; i < clusters.size(); ++i) {
            const int distance = qAbs(clusters[i].representativeDy - sample.dy);
            if (distance <= kDyClusterRadius && distance < bestDistance) {
                clusterIndex = i;
                bestDistance = distance;
            }
        }

        if (clusterIndex < 0) {
            DyCluster cluster;
            cluster.representativeDy = sample.dy;
            cluster.items.push_back(sample);
            clusters.push_back(cluster);
        } else {
            clusters[clusterIndex].items.push_back(sample);
        }
    }

    for (DyCluster &cluster : clusters) {
        std::sort(cluster.items.begin(), cluster.items.end(), [](const NccSample &a, const NccSample &b) {
            return a.score > b.score;
        });

        QSet<int> bandSet;
        double weightedDy = 0.0;
        double weightSum = 0.0;
        double scoreSum = 0.0;
        const int scoreCount = qMin(3, cluster.items.size());

        for (int i = 0; i < cluster.items.size(); ++i) {
            const NccSample &item = cluster.items.at(i);
            bandSet.insert(item.bandIndex);
            const double w = qMax(0.001, item.score);
            weightedDy += static_cast<double>(item.dy) * w;
            weightSum += w;
            if (i < scoreCount) {
                scoreSum += item.score;
            }
        }

        cluster.supportBands = bandSet.size();
        cluster.representativeDy = weightSum > 0.0
            ? qRound(weightedDy / weightSum)
            : cluster.items.first().dy;
        cluster.maxScore = cluster.items.first().score;
        cluster.meanScore = scoreCount > 0 ? scoreSum / static_cast<double>(scoreCount) : 0.0;

        double spreadAccum = 0.0;
        for (const NccSample &item : cluster.items) {
            const double delta = static_cast<double>(item.dy - cluster.representativeDy);
            spreadAccum += delta * delta;
        }
        cluster.spread = cluster.items.isEmpty()
            ? 0.0
            : std::sqrt(spreadAccum / static_cast<double>(cluster.items.size()));

        const NccSample &best = cluster.items.first();
        const bool boundaryDx = options.xTolerancePx > 0 && (qAbs(best.dx) == options.xTolerancePx);

        const double supportBonus = qBound(0.0,
                                           static_cast<double>(cluster.supportBands - 1) * 0.018,
                                           0.06);
        const double boundaryPenalty = boundaryDx ? 0.028 : 0.0;
        const double patchPenalty = qBound(0.0, (best.mad - 0.05) / 0.30, 1.0) * 0.06;
        const double spreadPenalty = qBound(0.0, (cluster.spread - 2.0) / 8.0, 1.0) * 0.03;

        const int softWindow = qMax(18,
                                    qRound(static_cast<double>(qMax(options.minScrollPx, targetDy)) *
                                           options.historyDySoftWindowRatio));
        const int historyDistance = qAbs(cluster.representativeDy - targetDy);
        const double historyPenalty = historyDistance > softWindow
            ? qBound(0.0,
                     (static_cast<double>(historyDistance - softWindow) /
                      static_cast<double>(qMax(softWindow, 1))) * 0.06,
                     0.09)
            : 0.0;

        cluster.penalty = boundaryPenalty + patchPenalty + spreadPenalty + historyPenalty;
        cluster.consensusScore = normalizedScore(cluster.meanScore + supportBonus - cluster.penalty);
    }

    std::sort(clusters.begin(), clusters.end(), [](const DyCluster &a, const DyCluster &b) {
        if (a.consensusScore == b.consensusScore) {
            if (a.supportBands == b.supportBands) {
                return a.maxScore > b.maxScore;
            }
            return a.supportBands > b.supportBands;
        }
        return a.consensusScore > b.consensusScore;
    });

    return clusters;
}

} // namespace

ScrollAlignmentEngine::ScrollAlignmentEngine(const ScrollAlignmentOptions &options)
    : m_options(options)
{
    m_options.templateTopStartRatio =
        qBound(0.01, m_options.templateTopStartRatio, 0.80);
    m_options.templateTopEndRatio =
        qBound(m_options.templateTopStartRatio + 0.02, m_options.templateTopEndRatio, 0.95);
    m_options.searchBottomStartRatio =
        qBound(0.05, m_options.searchBottomStartRatio, 0.90);
    m_options.xTolerancePx = qBound(0, m_options.xTolerancePx, 12);
    m_options.confidenceThreshold = qBound(0.50, m_options.confidenceThreshold, 0.999);
    m_options.consensusMinBands = qBound(1, m_options.consensusMinBands, 3);
    m_options.historyDySoftWindowRatio = qBound(0.10, m_options.historyDySoftWindowRatio, 1.20);
    m_options.ambiguityRejectGap = qBound(0.002, m_options.ambiguityRejectGap, 0.08);
    m_options.minMovementPx = qBound(1, m_options.minMovementPx, 2000);
    m_options.minScrollPx = qBound(1, m_options.minScrollPx, 2000);
}

ScrollAlignmentResult ScrollAlignmentEngine::align(const QImage &previous,
                                                   const QImage &current,
                                                   int estimatedDyPx) const
{
    ScrollAlignmentResult result;

    const cv::Mat previousGray = toGrayMat(previous);
    const cv::Mat currentGray = toGrayMat(current);
    if (previousGray.empty() || currentGray.empty() || previousGray.size() != currentGray.size()) {
        result.reason = QStringLiteral("Invalid or mismatched frames");
        result.rejectionCode = ScrollAlignmentRejectionCode::InvalidInput;
        return result;
    }

    return runTemplateNcc1D(previousGray, currentGray, estimatedDyPx);
}

ScrollAlignmentResult ScrollAlignmentEngine::runTemplateNcc1D(const cv::Mat &previousGray,
                                                              const cv::Mat &currentGray,
                                                              int estimatedDyPx) const
{
    ScrollAlignmentResult result;
    result.strategy = ScrollAlignmentStrategy::TemplateNcc1D;

    const int width = previousGray.cols;
    const int height = previousGray.rows;
    if (width <= 0 || height <= 0) {
        result.reason = QStringLiteral("Invalid frame dimensions");
        result.rejectionCode = ScrollAlignmentRejectionCode::InvalidInput;
        return result;
    }

    const int minCandidateDy = m_options.minScrollPx;
    const int defaultEstimatedDy = qMax(minCandidateDy, height / 5);
    const int targetDy = (estimatedDyPx > 0)
        ? qBound(minCandidateDy, estimatedDyPx, qMax(minCandidateDy, height - 4))
        : defaultEstimatedDy;

    const int searchStartBase = qBound(0,
                                       qRound(static_cast<double>(height) * m_options.searchBottomStartRatio),
                                       height - 1);

    const QVector<BandSpec> bands = selectTemplateBands(currentGray, m_options);
    if (bands.isEmpty()) {
        result.reason = QStringLiteral("Template NCC failed to build candidate bands");
        result.rejectionCode = ScrollAlignmentRejectionCode::InvalidInput;
        return result;
    }

    QVector<NccSample> samples;
    int lowTextureBands = 0;
    int sampledBands = 0;

    for (int bandIndex = 0; bandIndex < bands.size(); ++bandIndex) {
        const BandSpec &band = bands.at(bandIndex);
        if (band.height < kMinTemplateHeight || band.startY + band.height > height) {
            continue;
        }

        ++sampledBands;
        if (band.texture < kLowTextureStddev) {
            ++lowTextureBands;
            continue;
        }

        const int predictedMatchY = band.startY + qMax(minCandidateDy, targetDy);
        const int adaptiveStart = qMax(0, predictedMatchY - qMax(24, band.height));
        const int searchStartY = qMin(searchStartBase, adaptiveStart);

        QVector<NccSample> bandPeaks;

        for (int dx = -m_options.xTolerancePx; dx <= m_options.xTolerancePx; ++dx) {
            const int compareWidth = width - qAbs(dx);
            if (compareWidth < kMinCompareWidth) {
                continue;
            }

            const int prevX = qMax(0, dx);
            const int currX = qMax(0, -dx);
            if (currX + compareWidth > currentGray.cols || prevX + compareWidth > previousGray.cols) {
                continue;
            }

            const int searchHeight = height - searchStartY;
            if (searchHeight < band.height) {
                continue;
            }

            const cv::Rect templateRect(currX, band.startY, compareWidth, band.height);
            const cv::Rect searchRect(prevX, searchStartY, compareWidth, searchHeight);

            const cv::Mat templ = currentGray(templateRect);
            const cv::Mat search = previousGray(searchRect);
            if (templ.empty() || search.empty()) {
                continue;
            }

            cv::Mat response;
            cv::matchTemplate(search, templ, response, cv::TM_CCOEFF_NORMED);
            if (response.empty()) {
                continue;
            }

            for (int y = 0; y < response.rows; ++y) {
                const double score = static_cast<double>(response.at<float>(y, 0));
                if (!std::isfinite(score)) {
                    continue;
                }

                const int dy = (searchStartY + y) - band.startY;
                if (dy < minCandidateDy) {
                    continue;
                }

                const int overlap = height - qAbs(dy);
                if (overlap <= 0) {
                    continue;
                }

                NccSample candidate;
                candidate.bandIndex = bandIndex;
                candidate.templateStartY = band.startY;
                candidate.templateHeight = band.height;
                candidate.dy = dy;
                candidate.dx = dx;
                candidate.overlap = overlap;
                candidate.matchY = searchStartY + y;
                candidate.prevX = prevX;
                candidate.currX = currX;
                candidate.compareWidth = compareWidth;
                candidate.score = score;

                insertPeakWithSeparation(&bandPeaks, candidate);
            }
        }

        for (NccSample &sample : bandPeaks) {
            sample.mad = computePatchMad(previousGray, currentGray, sample);
            samples.push_back(sample);
        }
    }

    if (samples.isEmpty()) {
        result.confidence = 0.0;
        result.mad = 1.0;
        result.rejectedByConfidence = true;
        result.rejectionCode = (sampledBands > 0 && lowTextureBands >= sampledBands)
            ? ScrollAlignmentRejectionCode::LowTexture
            : ScrollAlignmentRejectionCode::LowConfidence;
        result.reason = (result.rejectionCode == ScrollAlignmentRejectionCode::LowTexture)
            ? QStringLiteral("Low confidence NCC match: template has low texture")
            : QStringLiteral("Template NCC failed to find a valid candidate");
        return result;
    }

    QVector<DyCluster> clusters = clusterCandidates(samples, m_options, targetDy);
    if (clusters.isEmpty()) {
        result.rejectedByConfidence = true;
        result.rejectionCode = ScrollAlignmentRejectionCode::LowConfidence;
        result.reason = QStringLiteral("Template NCC produced no consensus cluster");
        return result;
    }

    const DyCluster &bestCluster = clusters.first();
    const NccSample &bestSample = bestCluster.items.first();

    const bool hasSecondCluster = clusters.size() > 1;
    const DyCluster *secondCluster = hasSecondCluster ? &clusters.at(1) : nullptr;
    const double scoreGap = hasSecondCluster
        ? (bestCluster.consensusScore - secondCluster->consensusScore)
        : 1.0;
    const bool ambiguousCompetingCluster =
        hasSecondCluster &&
        qAbs(bestCluster.representativeDy - secondCluster->representativeDy) >= kDyClusterRadius &&
        scoreGap < m_options.ambiguityRejectGap;
    const bool strongAlternateCluster =
        hasSecondCluster &&
        qAbs(bestCluster.representativeDy - secondCluster->representativeDy) >= (kDyClusterRadius * 2) &&
        secondCluster->maxScore >= (bestCluster.maxScore - 0.02) &&
        secondCluster->supportBands >= qMax(1, m_options.consensusMinBands - 1) &&
        bestCluster.supportBands <= m_options.consensusMinBands;

    const bool weakConsensus =
        bestCluster.supportBands < m_options.consensusMinBands &&
        bestCluster.consensusScore < (m_options.confidenceThreshold + 0.03);

    result.dySigned = bestCluster.representativeDy;
    result.dx = static_cast<double>(bestSample.dx);
    result.overlapPx = height - qAbs(result.dySigned);
    result.rawNccScore = normalizedScore(bestCluster.meanScore);
    result.confidencePenalty = qBound(0.0,
                                      result.rawNccScore - bestCluster.consensusScore,
                                      1.0);
    result.confidence = normalizedScore(bestCluster.consensusScore);
    result.mad = qBound(0.0, qMax(1.0 - result.confidence, bestSample.mad), 1.0);
    result.consensusSupport = bestCluster.supportBands;
    result.dyClusterSpread = bestCluster.spread;

    if (ambiguousCompetingCluster || strongAlternateCluster) {
        result.rejectedByConfidence = true;
        result.rejectionCode = ScrollAlignmentRejectionCode::AmbiguousCluster;
        result.reason = QStringLiteral("Low confidence NCC match: ambiguous dy clusters (%1 vs %2, gap=%3)")
                            .arg(bestCluster.representativeDy)
                            .arg(secondCluster ? secondCluster->representativeDy : 0)
                            .arg(scoreGap, 0, 'f', 4);
        return result;
    }

    const bool boundaryDx = m_options.xTolerancePx > 0 &&
                            qAbs(bestSample.dx) == m_options.xTolerancePx;
    bool outsideToleranceDominant = false;
    int outsideBestDx = 0;
    double outsideBestScore = -1.0;
    if (bestSample.templateHeight >= 8 && bestSample.compareWidth >= kMinCompareWidth) {
        const int probeLimit = qMin(qMax(m_options.xTolerancePx + 10, 8), 16);
        auto evaluateDx = [&](int dxCandidate) -> double {
            const int compareWidth = width - qAbs(dxCandidate);
            if (compareWidth < kMinCompareWidth) {
                return -1.0;
            }
            const int prevX = qMax(0, dxCandidate);
            const int currX = qMax(0, -dxCandidate);
            if (currX + compareWidth > currentGray.cols || prevX + compareWidth > previousGray.cols) {
                return -1.0;
            }

            const int localPadding = qMax(8, bestSample.templateHeight / 3);
            const int localSearchStart = qBound(0,
                                                bestSample.matchY - localPadding,
                                                height - bestSample.templateHeight);
            const int localSearchEnd = qBound(localSearchStart + bestSample.templateHeight,
                                              bestSample.matchY + bestSample.templateHeight + localPadding,
                                              height);
            const int localSearchHeight = localSearchEnd - localSearchStart;
            if (localSearchHeight < bestSample.templateHeight) {
                return -1.0;
            }

            const cv::Rect templRect(currX,
                                     bestSample.templateStartY,
                                     compareWidth,
                                     bestSample.templateHeight);
            const cv::Rect searchRect(prevX,
                                      localSearchStart,
                                      compareWidth,
                                      localSearchHeight);
            const cv::Mat templ = currentGray(templRect);
            const cv::Mat search = previousGray(searchRect);
            if (templ.empty() || search.empty()) {
                return -1.0;
            }

            cv::Mat response;
            cv::matchTemplate(search, templ, response, cv::TM_CCOEFF_NORMED);
            if (response.empty()) {
                return -1.0;
            }

            double maxScore = -1.0;
            cv::minMaxLoc(response, nullptr, &maxScore);
            return std::isfinite(maxScore) ? maxScore : -1.0;
        };

        double inToleranceBest = -1.0;
        for (int dx = -probeLimit; dx <= probeLimit; ++dx) {
            const double score = evaluateDx(dx);
            if (score < 0.0) {
                continue;
            }
            if (qAbs(dx) <= m_options.xTolerancePx) {
                inToleranceBest = qMax(inToleranceBest, score);
            } else if (score > outsideBestScore) {
                outsideBestScore = score;
                outsideBestDx = dx;
            }
        }

        if (inToleranceBest >= 0.0 && outsideBestScore >= 0.0) {
            outsideToleranceDominant = outsideBestScore > (inToleranceBest + 0.02);
        }
    }

    if (outsideToleranceDominant) {
        result.rejectedByConfidence = true;
        result.rejectionCode = ScrollAlignmentRejectionCode::HorizontalDrift;
        result.reason = QStringLiteral("Low confidence NCC match: likely horizontal drift (outsideDx=%1 score=%2)")
                            .arg(outsideBestDx)
                            .arg(outsideBestScore, 0, 'f', 4);
        return result;
    }

    if (boundaryDx && bestSample.mad > 0.10) {
        result.rejectedByConfidence = true;
        result.rejectionCode = ScrollAlignmentRejectionCode::BoundaryAmbiguity;
        result.reason = QStringLiteral("Low confidence NCC match: boundary-x ambiguity (mad=%1)")
                            .arg(bestSample.mad, 0, 'f', 4);
        return result;
    }

    if (bestSample.mad > 0.22) {
        result.rejectedByConfidence = true;
        result.rejectionCode = ScrollAlignmentRejectionCode::PhotometricMismatch;
        result.reason = QStringLiteral("Low confidence NCC match: photometric mismatch (mad=%1)")
                            .arg(bestSample.mad, 0, 'f', 4);
        return result;
    }

    const int relaxedDyWindow = qMax(12,
                                     qRound(static_cast<double>(qMax(targetDy, minCandidateDy)) * 0.22));
    const bool dyWithinWindow = qAbs(result.dySigned - targetDy) <= relaxedDyWindow;
    const bool dxWithinTolerance = qAbs(bestSample.dx) <= m_options.xTolerancePx;
    const bool relaxedConfidenceAccepted =
        result.confidence >= 0.88 &&
        dxWithinTolerance &&
        dyWithinWindow &&
        result.mad <= 0.10 &&
        result.consensusSupport >= m_options.consensusMinBands;

    // Rescue path: if raw NCC remains extremely strong, allow acceptance even when
    // history-dy penalty drags consensus below threshold.
    const bool highRawRescueAccepted =
        result.rawNccScore >= 0.975 &&
        dxWithinTolerance &&
        dyWithinWindow &&
        result.mad <= 0.10 &&
        result.consensusSupport >= qMax(1, m_options.consensusMinBands - 1);

    const bool confidenceAccepted =
        (result.confidence >= m_options.confidenceThreshold ||
         relaxedConfidenceAccepted ||
         highRawRescueAccepted) &&
        (!weakConsensus || highRawRescueAccepted);

    if (!confidenceAccepted) {
        result.rejectedByConfidence = true;
        result.rejectionCode = ScrollAlignmentRejectionCode::LowConfidence;
        result.reason = QStringLiteral("Low confidence NCC match: %1 < %2 (support=%3 spread=%4 mad=%5)")
                            .arg(result.confidence, 0, 'f', 4)
                            .arg(m_options.confidenceThreshold, 0, 'f', 2)
                            .arg(result.consensusSupport)
                            .arg(result.dyClusterSpread, 0, 'f', 3)
                            .arg(result.mad, 0, 'f', 4);
        return result;
    }

    if (qAbs(result.dySigned) < m_options.minScrollPx) {
        result.reason = QStringLiteral("Detected movement below minimum scroll threshold");
        result.rejectionCode = ScrollAlignmentRejectionCode::LowConfidence;
        return result;
    }

    result.movementVerified =
        qAbs(result.dySigned) >= m_options.minMovementPx &&
        confidenceAccepted;

    result.ok = true;
    result.rejectionCode = ScrollAlignmentRejectionCode::None;
    if (result.confidence < m_options.confidenceThreshold) {
        if (highRawRescueAccepted) {
            result.reason = QStringLiteral("Template NCC aligned (raw-score rescue)");
        } else {
            result.reason = QStringLiteral("Template NCC aligned (relaxed consensus)");
        }
    } else {
        result.reason = QStringLiteral("Template NCC aligned");
    }
    return result;
}
