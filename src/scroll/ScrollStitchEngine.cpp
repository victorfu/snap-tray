#include "scroll/ScrollStitchEngine.h"

#include "detection/ScrollTemporalVarianceMask.h"
#include "utils/MatConverter.h"

#include <QPainter>

#include <QtGlobal>
#include <algorithm>
#include <cstring>

#include <opencv2/imgproc.hpp>

namespace {

constexpr double kStableRowDiffThreshold = 0.010;
constexpr int kMinStableRows = 6;
constexpr int kStickyLockFrames = 3;
constexpr double kNoChangeFullFrameThreshold = 0.008;
constexpr double kNoChangeCenterThreshold = 0.006;
constexpr double kBestGuessMinCenterActivity = 0.02;
constexpr int kBestGuessMinDyHistory = 3;
constexpr int kOpaqueAlphaThreshold = 250;
constexpr int kBottomIgnoreStableFrames = 2;
constexpr int kBottomIgnoreTolerancePx = 2;
constexpr int kColumnSampleCount = 12;
constexpr double kColumnFallbackMinGain = 0.02;
constexpr double kDuplicateTailDiffThreshold = 0.0060;
constexpr double kDuplicateTailDiffLooseThreshold = 0.0100;
constexpr int kFingerprintHistory = 8;
constexpr int kMaxUndoHistory = 8;

int clampPositive(int value)
{
    return value > 0 ? value : 0;
}

int sideIgnoreOffset(int width)
{
    if (width <= 0) {
        return 0;
    }

    const int offset = qMax(50, width / 20);
    return qMin(offset, width / 3);
}

double centerActivityRatio(const QImage &a, const QImage &b)
{
    if (a.isNull() || b.isNull() || a.size() != b.size()) {
        return 1.0;
    }

    const int width = a.width();
    const int height = a.height();
    if (width <= 0 || height <= 0) {
        return 1.0;
    }

    const int roiWidth = qBound(32, width * 2 / 3, width);
    const int roiHeight = qBound(32, height * 2 / 3, height);
    const int xStart = (width - roiWidth) / 2;
    const int yStart = (height - roiHeight) / 2;
    const int rowStep = roiHeight > 180 ? 2 : 1;
    const int colStep = roiWidth > 280 ? 2 : 1;

    qint64 accum = 0;
    int samples = 0;
    for (int y = 0; y < roiHeight; y += rowStep) {
        const uchar *lineA = a.constScanLine(yStart + y);
        const uchar *lineB = b.constScanLine(yStart + y);
        if (!lineA || !lineB) {
            continue;
        }

        for (int x = 0; x < roiWidth; x += colStep) {
            const int px = xStart + x;
            accum += qAbs(static_cast<int>(lineA[px]) - static_cast<int>(lineB[px]));
            ++samples;
        }
    }

    if (samples <= 0) {
        return 1.0;
    }

    return static_cast<double>(accum) / (static_cast<double>(samples) * 255.0);
}

QColor estimateBackgroundColor(const QImage &image)
{
    if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
        return QColor(255, 255, 255, 255);
    }

    const int w = image.width();
    const int h = image.height();
    const QVector<QPoint> samples{
        QPoint(0, 0),
        QPoint(w - 1, 0),
        QPoint(0, h - 1),
        QPoint(w - 1, h - 1),
        QPoint(w / 2, 0),
        QPoint(w / 2, h - 1),
        QPoint(0, h / 2),
        QPoint(w - 1, h / 2)
    };

    int r = 0;
    int g = 0;
    int b = 0;
    for (const QPoint &pt : samples) {
        const QColor c = image.pixelColor(pt);
        r += c.red();
        g += c.green();
        b += c.blue();
    }

    const int count = qMax(1, samples.size());
    return QColor(r / count, g / count, b / count, 255);
}

double robustScoreFromSamples(const QVector<double> &samples)
{
    if (samples.isEmpty()) {
        return 0.0;
    }

    QVector<double> sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    int trim = 0;
    if (sorted.size() >= 6) {
        trim = qMax(1, sorted.size() / 6);
    }

    const int start = trim;
    const int end = sorted.size() - trim;
    if (start >= end) {
        trim = 0;
    }

    double accum = 0.0;
    int count = 0;
    for (int i = trim; i < sorted.size() - trim; ++i) {
        accum += sorted.at(i);
        ++count;
    }

    if (count <= 0) {
        return 0.0;
    }
    return accum / static_cast<double>(count);
}

StitchMatchStrategy toStitchStrategy(ScrollAlignmentStrategy strategy)
{
    switch (strategy) {
    case ScrollAlignmentStrategy::TemplateNcc1D:
        return StitchMatchStrategy::TemplateNcc1D;
    }

    return StitchMatchStrategy::TemplateNcc1D;
}

QImage normalizeOpaqueFrame(const QImage &frame)
{
    if (frame.isNull()) {
        return QImage();
    }

    QImage normalized = frame.convertToFormat(QImage::Format_ARGB32);
    if (normalized.isNull()) {
        return QImage();
    }

    const int width = normalized.width();
    const int height = normalized.height();
    if (width <= 0 || height <= 0) {
        return QImage();
    }

    bool hasTransparentPixels = false;
    QVector<bool> rowHasOpaque(height, false);
    for (int y = 0; y < height; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(normalized.constScanLine(y));
        for (int x = 0; x < width; ++x) {
            if (qAlpha(line[x]) >= kOpaqueAlphaThreshold) {
                rowHasOpaque[y] = true;
                break;
            }
        }
        if (!rowHasOpaque[y]) {
            hasTransparentPixels = true;
            continue;
        }

        const QRgb *verifyLine = reinterpret_cast<const QRgb *>(normalized.constScanLine(y));
        for (int x = 0; x < width; ++x) {
            if (qAlpha(verifyLine[x]) < kOpaqueAlphaThreshold) {
                hasTransparentPixels = true;
                break;
            }
        }
    }

    if (!hasTransparentPixels) {
        return normalized.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }

    for (int y = 0; y < height; ++y) {
        if (!rowHasOpaque[y]) {
            continue;
        }

        QRgb *line = reinterpret_cast<QRgb *>(normalized.scanLine(y));
        int firstOpaque = -1;
        int lastOpaque = -1;
        for (int x = 0; x < width; ++x) {
            if (qAlpha(line[x]) >= kOpaqueAlphaThreshold) {
                if (firstOpaque < 0) {
                    firstOpaque = x;
                }
                lastOpaque = x;
            }
        }

        if (firstOpaque < 0 || lastOpaque < 0) {
            continue;
        }

        const QRgb leftColor = qRgba(qRed(line[firstOpaque]),
                                     qGreen(line[firstOpaque]),
                                     qBlue(line[firstOpaque]),
                                     255);
        const QRgb rightColor = qRgba(qRed(line[lastOpaque]),
                                      qGreen(line[lastOpaque]),
                                      qBlue(line[lastOpaque]),
                                      255);
        for (int x = 0; x < firstOpaque; ++x) {
            line[x] = leftColor;
        }
        for (int x = lastOpaque + 1; x < width; ++x) {
            line[x] = rightColor;
        }

        QRgb fillColor = leftColor;
        for (int x = firstOpaque; x <= lastOpaque; ++x) {
            if (qAlpha(line[x]) >= kOpaqueAlphaThreshold) {
                fillColor = qRgba(qRed(line[x]), qGreen(line[x]), qBlue(line[x]), 255);
            } else {
                line[x] = fillColor;
            }
        }

        fillColor = rightColor;
        for (int x = lastOpaque; x >= firstOpaque; --x) {
            if (qAlpha(line[x]) >= kOpaqueAlphaThreshold) {
                fillColor = qRgba(qRed(line[x]), qGreen(line[x]), qBlue(line[x]), 255);
            } else {
                line[x] = fillColor;
            }
        }
    }

    int previousOpaqueRow = -1;
    for (int y = 0; y < height; ++y) {
        if (rowHasOpaque[y]) {
            previousOpaqueRow = y;
            continue;
        }

        int nextOpaqueRow = -1;
        for (int probe = y + 1; probe < height; ++probe) {
            if (rowHasOpaque[probe]) {
                nextOpaqueRow = probe;
                break;
            }
        }

        if (previousOpaqueRow >= 0) {
            memcpy(normalized.scanLine(y), normalized.constScanLine(previousOpaqueRow),
                   static_cast<size_t>(normalized.bytesPerLine()));
        } else if (nextOpaqueRow >= 0) {
            memcpy(normalized.scanLine(y), normalized.constScanLine(nextOpaqueRow),
                   static_cast<size_t>(normalized.bytesPerLine()));
        }
    }

    for (int y = 0; y < height; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(normalized.scanLine(y));
        for (int x = 0; x < width; ++x) {
            line[x] = qRgba(qRed(line[x]), qGreen(line[x]), qBlue(line[x]), 255);
        }
    }

    return normalized.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

} // namespace

ScrollStitchEngine::ScrollStitchEngine(const ScrollStitchOptions &options)
    : m_options(options)
    , m_alignmentEngine()
    , m_seamBlender()
{
    if (m_options.partialThreshold > m_options.goodThreshold) {
        m_options.partialThreshold = m_options.goodThreshold;
    }
    m_options.goodThreshold = qBound(0.0, m_options.goodThreshold, 1.0);
    m_options.partialThreshold = qBound(0.0, m_options.partialThreshold, 1.0);
    m_options.minScrollAmount = qMax(1, m_options.minScrollAmount);
    m_options.maxFrames = qMax(1, m_options.maxFrames);
    m_options.maxOutputPixels = qMax(1024, m_options.maxOutputPixels);
    m_options.maxOutputHeightPx = qBound(1, m_options.maxOutputHeightPx, 200000);
    m_options.seamWidthPx = qBound(8, m_options.seamWidthPx, 128);
    m_options.seamFeatherSigma = qBound(1.0, m_options.seamFeatherSigma, 32.0);
    m_options.dynamicTvThreshold = qBound(0.001, m_options.dynamicTvThreshold, 0.25);
    m_options.dynamicWeight = qBound(0.0, m_options.dynamicWeight, 1.0);

    ScrollAlignmentOptions alignmentOptions;
    alignmentOptions.templateTopStartRatio = 0.20;
    alignmentOptions.templateTopEndRatio = 0.30;
    alignmentOptions.searchBottomStartRatio = 0.50;
    alignmentOptions.xTolerancePx = 2;
    alignmentOptions.confidenceThreshold = 0.92;
    alignmentOptions.consensusMinBands = 2;
    alignmentOptions.historyDySoftWindowRatio = 0.35;
    alignmentOptions.ambiguityRejectGap = 0.008;
    alignmentOptions.minMovementPx = m_options.minScrollAmount;
    alignmentOptions.minScrollPx = m_options.minScrollAmount;
    m_alignmentEngine = ScrollAlignmentEngine(alignmentOptions);

    ScrollSeamBlendOptions seamBlendOptions;
    seamBlendOptions.seamSearchWindowPx = 64;
    seamBlendOptions.seamWidth = m_options.seamWidthPx;
    seamBlendOptions.featherSigma = m_options.seamFeatherSigma;
    seamBlendOptions.usePoissonByDefault = m_options.usePoissonSeam;
    m_seamBlender = ScrollSeamBlender(seamBlendOptions);
}

void ScrollStitchEngine::invalidatePreviewCache()
{
    m_composedCacheValid = false;
    m_cachedComposed = QImage();
    m_previewCacheValid = false;
    m_cachedPreview = QImage();
    m_cachedPreviewSize = QSize();
}

QImage ScrollStitchEngine::toGray(const QImage &image)
{
    if (image.isNull()) {
        return QImage();
    }
    return image.convertToFormat(QImage::Format_Grayscale8);
}

quint64 ScrollStitchEngine::segmentFingerprint(const QImage &segmentGray)
{
    if (segmentGray.isNull() || segmentGray.width() <= 0 || segmentGray.height() <= 0) {
        return 0;
    }

    // Compact rolling hash sampled from a grid, robust enough for duplicate-loop rejection.
    constexpr quint64 kFnvOffset = 1469598103934665603ULL;
    constexpr quint64 kFnvPrime = 1099511628211ULL;

    quint64 hash = kFnvOffset;
    const int rowStep = qMax(1, segmentGray.height() / 48);
    const int colStep = qMax(1, segmentGray.width() / 48);
    for (int y = 0; y < segmentGray.height(); y += rowStep) {
        const uchar *line = segmentGray.constScanLine(y);
        if (!line) {
            continue;
        }
        for (int x = 0; x < segmentGray.width(); x += colStep) {
            hash ^= static_cast<quint64>(line[x]);
            hash *= kFnvPrime;
        }
    }
    return hash;
}

bool ScrollStitchEngine::detectFingerprintLoop(quint64 fingerprint) const
{
    if (fingerprint == 0 || m_recentFingerprints.isEmpty()) {
        return false;
    }

    const int n = m_recentFingerprints.size();
    const quint64 last = m_recentFingerprints.at(n - 1);
    if (fingerprint == last) {
        return true;
    }

    // ABAB / short-cycle detection.
    if (n >= 3) {
        const quint64 a = m_recentFingerprints.at(n - 3);
        const quint64 b = m_recentFingerprints.at(n - 2);
        if (fingerprint == a && last == b) {
            return true;
        }
    }

    // Frequent repetition in a short horizon.
    int repeats = 0;
    for (const quint64 v : m_recentFingerprints) {
        if (v == fingerprint) {
            ++repeats;
        }
    }
    return repeats >= 2;
}

double ScrollStitchEngine::rowDifference(const uchar *a, const uchar *b, int width)
{
    if (!a || !b || width <= 0) {
        return 1.0;
    }

    qint64 accum = 0;
    for (int x = 0; x < width; ++x) {
        accum += qAbs(static_cast<int>(a[x]) - static_cast<int>(b[x]));
    }
    const double denom = static_cast<double>(width) * 255.0;
    return denom > 0.0 ? (static_cast<double>(accum) / denom) : 1.0;
}

double ScrollStitchEngine::regionDifference(const QImage &a, int ay,
                                            const QImage &b, int by,
                                            int width, int height,
                                            int leftCrop, int rightCrop)
{
    if (a.isNull() || b.isNull() || width <= 0 || height <= 0) {
        return 1.0;
    }

    const int xStart = qBound(0, leftCrop, width - 1);
    const int xEnd = qBound(xStart + 1, width - rightCrop, width);
    const int sampleWidth = qMax(1, xEnd - xStart);
    const int rowStep = (height > 700) ? 4 : (height > 280 ? 2 : 1);
    const int colStep = (sampleWidth > 900) ? 4 : (sampleWidth > 420 ? 2 : 1);

    double accum = 0.0;
    int samples = 0;
    for (int row = 0; row < height; row += rowStep) {
        const int aRow = ay + row;
        const int bRow = by + row;
        if (aRow < 0 || aRow >= a.height() || bRow < 0 || bRow >= b.height()) {
            continue;
        }

        const uchar *pa = a.constScanLine(aRow);
        const uchar *pb = b.constScanLine(bRow);
        if (!pa || !pb) {
            continue;
        }

        qint64 rowDiff = 0;
        int rowSamples = 0;
        for (int x = xStart; x < xEnd; x += colStep) {
            rowDiff += qAbs(static_cast<int>(pa[x]) - static_cast<int>(pb[x]));
            ++rowSamples;
        }
        if (rowSamples <= 0) {
            continue;
        }
        const double rowNorm = static_cast<double>(rowDiff) / (static_cast<double>(rowSamples) * 255.0);
        accum += rowNorm;
        ++samples;
    }

    if (samples <= 0) {
        return 1.0;
    }
    return accum / static_cast<double>(samples);
}

int ScrollStitchEngine::contiguousStableRows(const QImage &a, const QImage &b, bool fromTop, int maxScanRows)
{
    if (a.isNull() || b.isNull() || a.size() != b.size()) {
        return 0;
    }

    const int rows = qMin(qMax(0, maxScanRows), a.height());
    int stable = 0;
    for (int i = 0; i < rows; ++i) {
        const int row = fromTop ? i : (a.height() - 1 - i);
        const uchar *pa = a.constScanLine(row);
        const uchar *pb = b.constScanLine(row);
        if (!pa || !pb) {
            break;
        }

        if (rowDifference(pa, pb, a.width()) <= kStableRowDiffThreshold) {
            ++stable;
        } else {
            break;
        }
    }
    return stable;
}

void ScrollStitchEngine::updateStickyBands(const QImage &prevGray, const QImage &currGray)
{
    if (prevGray.isNull() || currGray.isNull() || prevGray.size() != currGray.size()) {
        return;
    }

    const int maxScan = qMax(24, prevGray.height() / 4);
    const int headerCandidate = contiguousStableRows(prevGray, currGray, true, maxScan);
    const int footerCandidate = contiguousStableRows(prevGray, currGray, false, maxScan);

    if (headerCandidate >= kMinStableRows) {
        if (qAbs(headerCandidate - m_lastHeaderCandidate) <= 2) {
            ++m_headerStableCount;
        } else {
            m_headerStableCount = 1;
        }
        m_lastHeaderCandidate = headerCandidate;
        if (m_headerStableCount >= kStickyLockFrames) {
            m_stickyHeaderPx = qMin(headerCandidate, prevGray.height() / 4);
        }
    } else {
        m_headerStableCount = 0;
        m_lastHeaderCandidate = 0;
    }

    if (footerCandidate >= kMinStableRows) {
        if (qAbs(footerCandidate - m_lastFooterCandidate) <= 2) {
            ++m_footerStableCount;
        } else {
            m_footerStableCount = 1;
        }
        m_lastFooterCandidate = footerCandidate;
        if (m_footerStableCount >= kStickyLockFrames) {
            m_stickyFooterPx = qMin(footerCandidate, prevGray.height() / 4);
        }
    } else {
        m_footerStableCount = 0;
        m_lastFooterCandidate = 0;
    }
}

int ScrollStitchEngine::calculateBottomIgnore(const QImage &prevGray, const QImage &currGray) const
{
    if (!m_options.autoIgnoreBottomEdge || prevGray.isNull() || currGray.isNull() || prevGray.size() != currGray.size()) {
        return 0;
    }

    const int h = prevGray.height();
    const int w = prevGray.width();
    if (h <= 0 || w <= 0) {
        return 0;
    }

    const int maxOffset = h / 3;
    if (maxOffset <= 0) {
        return 0;
    }

    const int crop = sideIgnoreOffset(w);
    const int sampleWidth = qMax(1, w - crop * 2);
    const int ignoreBaseline = qMin(maxOffset, qMax(8, h / 10));
    int unstableBandRows = 0;

    for (int i = 0; i <= maxOffset; ++i) {
        const int row = h - 1 - i;
        if (row < 0) {
            break;
        }

        const uchar *prevRow = prevGray.constScanLine(row);
        const uchar *currRow = currGray.constScanLine(row);
        if (!prevRow || !currRow) {
            break;
        }

        const double diff = rowDifference(prevRow + crop, currRow + crop, sampleWidth);
        if (diff > kStableRowDiffThreshold) {
            unstableBandRows = i + 1;
        } else if (unstableBandRows > 0) {
            break;
        } else {
            // Bottom edge starts stable, so avoid ignoring rows just because deeper
            // content moved during the scroll step.
            break;
        }
    }

    if (unstableBandRows <= 0) {
        return 0;
    }

    const int ignoreBottom = qMax(ignoreBaseline, unstableBandRows);
    return qMin(maxOffset, ignoreBottom);
}

void ScrollStitchEngine::updateBottomIgnoreStability(int bottomIgnore)
{
    const int candidate = clampPositive(bottomIgnore);
    if (candidate <= 0) {
        m_bottomIgnoreCandidate = 0;
        m_bottomIgnoreStableCount = 0;
        m_committedBottomIgnore = 0;
        return;
    }

    if (m_bottomIgnoreCandidate > 0 &&
        qAbs(candidate - m_bottomIgnoreCandidate) <= kBottomIgnoreTolerancePx) {
        m_bottomIgnoreCandidate = (m_bottomIgnoreCandidate + candidate) / 2;
        ++m_bottomIgnoreStableCount;
    } else {
        m_bottomIgnoreCandidate = candidate;
        m_bottomIgnoreStableCount = 1;
    }

    if (m_bottomIgnoreStableCount >= kBottomIgnoreStableFrames) {
        m_committedBottomIgnore = m_bottomIgnoreCandidate;
    }
}

ScrollStitchEngine::MatchResult ScrollStitchEngine::findBestVerticalMatch(const QImage &prevGray,
                                                                          const QImage &currGray,
                                                                          int bottomIgnore) const
{
    MatchResult result;
    if (prevGray.isNull() || currGray.isNull() || prevGray.size() != currGray.size()) {
        return result;
    }

    const int h = prevGray.height();
    const int w = prevGray.width();
    if (h <= 0 || w <= 0) {
        return result;
    }

    const int effectiveHeight = h - bottomIgnore;
    const int minOverlap = qMax(24, h / 5);
    if (effectiveHeight <= minOverlap) {
        return result;
    }

    const int leftCrop = sideIgnoreOffset(w);
    const int rightCrop = leftCrop;

    double bestConfidence = -1.0;
    int bestDy = -1;
    int bestMatchedRows = 0;

    const int dyMin = m_options.minScrollAmount;
    const int dyMax = effectiveHeight - minOverlap;
    if (dyMax < dyMin) {
        return result;
    }

    const int dyStep = (effectiveHeight > 900) ? 4 : (effectiveHeight > 600 ? 3 : (effectiveHeight > 360 ? 2 : 1));
    auto considerDy = [&](int dy) {
        const int overlap = effectiveHeight - dy;
        int compareStartPrev = dy + m_stickyHeaderPx;
        int compareStartCurr = m_stickyHeaderPx;
        int compareHeight = overlap - m_stickyHeaderPx - m_stickyFooterPx;
        if (compareHeight < minOverlap / 2) {
            return;
        }

        if (compareStartPrev + compareHeight > prevGray.height() ||
            compareStartCurr + compareHeight > currGray.height()) {
            return;
        }

        const double diff = regionDifference(prevGray, compareStartPrev,
                                             currGray, compareStartCurr,
                                             w, compareHeight,
                                             leftCrop, rightCrop);
        const double confidence = 1.0 - diff;
        if (confidence > bestConfidence ||
            (qAbs(confidence - bestConfidence) < 1e-6 && compareHeight > bestMatchedRows)) {
            bestConfidence = confidence;
            bestDy = dy;
            bestMatchedRows = compareHeight;
        }
    };

    for (int dy = dyMin; dy <= dyMax; dy += dyStep) {
        considerDy(dy);
    }

    if (bestDy >= 0 && dyStep > 1) {
        const int refineStart = qMax(dyMin, bestDy - dyStep);
        const int refineEnd = qMin(dyMax, bestDy + dyStep);
        for (int dy = refineStart; dy <= refineEnd; ++dy) {
            considerDy(dy);
        }
    }

    if (bestDy < 0) {
        return result;
    }

    result.valid = true;
    result.dy = bestDy;
    // Keep overlap in full-frame coordinates so append() trims duplicated rows correctly
    // even when matching ignores a bottom edge band.
    result.overlap = h - bestDy;
    result.matchedRows = bestMatchedRows;
    result.bottomIgnore = bottomIgnore;
    result.confidence = qBound(0.0, bestConfidence, 1.0);
    result.strategy = StitchMatchStrategy::RowDiff;
    return result;
}

ScrollStitchEngine::MatchResult ScrollStitchEngine::findColumnSampleMatch(const QImage &prevGray,
                                                                          const QImage &currGray,
                                                                          int bottomIgnore) const
{
    MatchResult result;
    if (prevGray.isNull() || currGray.isNull() || prevGray.size() != currGray.size()) {
        return result;
    }

    const int h = prevGray.height();
    const int w = prevGray.width();
    if (h <= 0 || w <= 0) {
        return result;
    }

    const int effectiveHeight = h - bottomIgnore;
    const int minOverlap = qMax(24, h / 5);
    if (effectiveHeight <= minOverlap) {
        return result;
    }

    const int sideCrop = sideIgnoreOffset(w);
    const int left = qBound(0, sideCrop, w - 1);
    const int right = qBound(left + 1, w - sideCrop, w);
    const int usableWidth = right - left;
    if (usableWidth < 8) {
        return result;
    }

    QVector<int> columns;
    columns.reserve(kColumnSampleCount);
    for (int i = 0; i < kColumnSampleCount; ++i) {
        const int col = left + qRound(static_cast<double>(i + 1) *
                                      static_cast<double>(usableWidth) /
                                      static_cast<double>(kColumnSampleCount + 1));
        if (!columns.contains(col)) {
            columns.push_back(col);
        }
    }

    if (columns.isEmpty()) {
        return result;
    }

    double bestConfidence = -1.0;
    int bestDy = -1;
    int bestMatchedRows = 0;

    const int dyMin = m_options.minScrollAmount;
    const int dyMax = effectiveHeight - minOverlap;
    if (dyMax < dyMin) {
        return result;
    }

    const int dyStep = (effectiveHeight > 900) ? 4 : (effectiveHeight > 600 ? 3 : (effectiveHeight > 360 ? 2 : 1));
    auto considerDy = [&](int dy) {
        const int overlap = effectiveHeight - dy;
        const int compareStartPrev = dy + m_stickyHeaderPx;
        const int compareStartCurr = m_stickyHeaderPx;
        const int compareHeight = overlap - m_stickyHeaderPx - m_stickyFooterPx;
        if (compareHeight < minOverlap / 2) {
            return;
        }

        if (compareStartPrev + compareHeight > prevGray.height() ||
            compareStartCurr + compareHeight > currGray.height()) {
            return;
        }

        const int rowStep = (compareHeight > 700) ? 4 : (compareHeight > 280 ? 2 : 1);
        QVector<double> columnDiffs;
        columnDiffs.reserve(columns.size());
        for (const int col : columns) {
            qint64 diffAccum = 0;
            int samples = 0;
            for (int row = 0; row < compareHeight; row += rowStep) {
                const int prevRow = compareStartPrev + row;
                const int currRow = compareStartCurr + row;
                const uchar *prevLine = prevGray.constScanLine(prevRow);
                const uchar *currLine = currGray.constScanLine(currRow);
                if (!prevLine || !currLine) {
                    continue;
                }
                diffAccum += qAbs(static_cast<int>(prevLine[col]) - static_cast<int>(currLine[col]));
                ++samples;
            }

            if (samples > 0) {
                const double normalized =
                    static_cast<double>(diffAccum) / (static_cast<double>(samples) * 255.0);
                columnDiffs.push_back(normalized);
            }
        }

        if (columnDiffs.size() < 3) {
            return;
        }

        const double robustDiff = robustScoreFromSamples(columnDiffs);
        const double confidence = 1.0 - robustDiff;
        if (confidence > bestConfidence ||
            (qAbs(confidence - bestConfidence) < 1e-6 && compareHeight > bestMatchedRows)) {
            bestConfidence = confidence;
            bestDy = dy;
            bestMatchedRows = compareHeight;
        }
    };

    for (int dy = dyMin; dy <= dyMax; dy += dyStep) {
        considerDy(dy);
    }

    if (bestDy >= 0 && dyStep > 1) {
        const int refineStart = qMax(dyMin, bestDy - dyStep);
        const int refineEnd = qMin(dyMax, bestDy + dyStep);
        for (int dy = refineStart; dy <= refineEnd; ++dy) {
            considerDy(dy);
        }
    }

    if (bestDy < 0) {
        return result;
    }

    result.valid = true;
    result.dy = bestDy;
    result.overlap = h - bestDy;
    result.matchedRows = bestMatchedRows;
    result.bottomIgnore = bottomIgnore;
    result.confidence = qBound(0.0, bestConfidence, 1.0);
    result.strategy = StitchMatchStrategy::ColumnSample;
    return result;
}

bool ScrollStitchEngine::start(const QImage &firstFrame)
{
    if (firstFrame.isNull()) {
        return false;
    }

    invalidatePreviewCache();

    m_segments.clear();
    m_undoStack.clear();
    m_totalHeight = 0;
    m_frameCount = 0;
    m_stickyHeaderPx = 0;
    m_stickyFooterPx = 0;
    m_externalEstimatedDyHint = 0;
    m_headerStableCount = 0;
    m_footerStableCount = 0;
    m_lastHeaderCandidate = 0;
    m_lastFooterCandidate = 0;
    m_hasBestMatch = false;
    m_bestMatchDy = 0;
    m_bestBottomIgnore = 0;
    m_bestMatchedRows = 0;
    m_bottomIgnoreCandidate = 0;
    m_bottomIgnoreStableCount = 0;
    m_committedBottomIgnore = 0;
    m_hasDyHistory = false;
    m_averageDy = 0.0;
    m_dyHistoryCount = 0;
    m_backgroundColor = QColor(255, 255, 255, 255);
    m_hasLastResult = false;
    m_lastResult = StitchFrameResult();
    m_observedPreviousGray = QImage();
    m_observedOlderGray = QImage();

    m_lastFrame = normalizeOpaqueFrame(firstFrame);
    if (m_lastFrame.isNull()) {
        m_started = false;
        return false;
    }

    const int width = m_lastFrame.width();
    if (width <= 0) {
        m_started = false;
        return false;
    }

    const qint64 maxHeightByPixels =
        static_cast<qint64>(m_options.maxOutputPixels) / qMax<qint64>(1, width);
    const qint64 maxHeightAllowed = qMin<qint64>(maxHeightByPixels, m_options.maxOutputHeightPx);
    if (maxHeightAllowed <= 0) {
        m_started = false;
        return false;
    }

    if (m_lastFrame.height() > maxHeightAllowed) {
        m_lastFrame = m_lastFrame.copy(0, 0, m_lastFrame.width(), static_cast<int>(maxHeightAllowed));
    }

    m_lastGray = toGray(m_lastFrame);
    if (m_lastFrame.isNull() || m_lastGray.isNull()) {
        m_started = false;
        return false;
    }
    m_observedPreviousGray = m_lastGray;
    m_backgroundColor = estimateBackgroundColor(m_lastFrame);

    StitchSegment initialSegment;
    initialSegment.image = m_lastFrame;
    initialSegment.appendHeight = m_lastFrame.height();
    initialSegment.trimTop = 0;
    initialSegment.trimBottom = 0;
    initialSegment.confidence = 1.0;
    initialSegment.strategy = StitchMatchStrategy::RowDiff;
    m_segments.push_back(initialSegment);
    m_recentFingerprints.clear();
    const quint64 initialFingerprint = segmentFingerprint(toGray(initialSegment.image));
    if (initialFingerprint != 0) {
        m_recentFingerprints.push_back(initialFingerprint);
    }
    m_totalHeight = m_lastFrame.height();
    m_frameCount = 1;
    m_started = true;
    return true;
}

void ScrollStitchEngine::setExternalEstimatedDyHint(int dyPx)
{
    m_externalEstimatedDyHint = qBound(0, dyPx, 4000);
}

StitchFrameResult ScrollStitchEngine::append(const QImage &frame)
{
    StitchFrameResult result;
    auto finalizeResult = [this](const StitchFrameResult &value) {
        m_lastResult = value;
        m_hasLastResult = true;
        return value;
    };

    if (!m_started || frame.isNull() || m_lastFrame.isNull()) {
        result.quality = StitchQuality::Bad;
        result.rejectionCode = StitchRejectionCode::InvalidFrame;
        return finalizeResult(result);
    }

    if (m_frameCount >= m_options.maxFrames) {
        result.quality = StitchQuality::NoChange;
        return finalizeResult(result);
    }

    QImage current = normalizeOpaqueFrame(frame);
    if (current.size() != m_lastFrame.size()) {
        result.quality = StitchQuality::Bad;
        result.rejectionCode = StitchRejectionCode::InvalidFrame;
        return finalizeResult(result);
    }

    const QImage currentGray = toGray(current);
    auto finalizeWithObservedHistory = [&](const StitchFrameResult &value) {
        if (!currentGray.isNull()) {
            m_observedOlderGray = m_observedPreviousGray;
            m_observedPreviousGray = currentGray;
        }
        return finalizeResult(value);
    };

    const int edgeCrop = qMin(currentGray.width() / 12, qMax(0, currentGray.width() / 2 - 1));
    const double fullFrameDiff = regionDifference(m_lastGray, 0,
                                                  currentGray, 0,
                                                  currentGray.width(), currentGray.height(),
                                                  edgeCrop, edgeCrop);
    const double centerDiff = centerActivityRatio(m_lastGray, currentGray);
    if (fullFrameDiff <= kNoChangeFullFrameThreshold && centerDiff <= kNoChangeCenterThreshold) {
        result.quality = StitchQuality::NoChange;
        result.confidence = qBound(0.0, 1.0 - qMax(fullFrameDiff, centerDiff), 1.0);
        result.stickyHeaderPx = m_stickyHeaderPx;
        result.stickyFooterPx = m_stickyFooterPx;
        result.rejectionCode = StitchRejectionCode::None;
        return finalizeWithObservedHistory(result);
    }

    const int historyDy = (m_hasDyHistory && m_dyHistoryCount > 0)
        ? qRound(m_averageDy)
        : 0;
    const int consumedHintDy = m_externalEstimatedDyHint;
    m_externalEstimatedDyHint = 0;
    int estimatedDy = qMax(m_options.minScrollAmount, current.height() / 5);
    if (historyDy > 0) {
        estimatedDy = historyDy;
    }
    if (consumedHintDy > 0) {
        const int clampedHint = qBound(m_options.minScrollAmount, consumedHintDy, current.height() - 8);
        if (historyDy > 0 && qAbs(historyDy - clampedHint) <= qMax(24, current.height() / 3)) {
            estimatedDy = qRound((static_cast<double>(historyDy) * 2.0 + clampedHint) / 3.0);
        } else {
            estimatedDy = clampedHint;
        }
    }

    QImage previousForAlignment = m_lastGray;
    QImage currentForAlignment = currentGray;

    const bool hasThreeFrameWindow =
        !m_observedOlderGray.isNull() &&
        !m_observedPreviousGray.isNull() &&
        m_observedOlderGray.size() == currentGray.size() &&
        m_observedPreviousGray.size() == currentGray.size();

    if (hasThreeFrameWindow) {
        const cv::Mat olderGray = MatConverter::toGray(m_observedOlderGray);
        const cv::Mat previousObservedGray = MatConverter::toGray(m_observedPreviousGray);
        const cv::Mat currentObservedGray = MatConverter::toGray(currentGray);
        const ScrollTemporalMaskResult maskResult = ScrollTemporalVarianceMask::buildAlignmentMask(
            olderGray,
            previousObservedGray,
            currentObservedGray,
            m_options.dynamicTvThreshold,
            20,
            kMinStableRows);

        if (!maskResult.alignmentMask.empty()) {
            cv::Mat prevMasked = ScrollTemporalVarianceMask::applyMask(MatConverter::toGray(m_lastGray),
                                                                       maskResult.alignmentMask);
            cv::Mat currMasked = ScrollTemporalVarianceMask::applyMask(MatConverter::toGray(currentGray),
                                                                       maskResult.alignmentMask);
            const QImage prevMaskedImage = MatConverter::toQImage(prevMasked).convertToFormat(QImage::Format_Grayscale8);
            const QImage currMaskedImage = MatConverter::toQImage(currMasked).convertToFormat(QImage::Format_Grayscale8);
            if (!prevMaskedImage.isNull() && !currMaskedImage.isNull()) {
                previousForAlignment = prevMaskedImage;
                currentForAlignment = currMaskedImage;
            }

            result.dynamicMaskCoverage = ScrollTemporalVarianceMask::maskedCoverage(maskResult.alignmentMask);
        }

        if (maskResult.stickyHeaderPx >= kMinStableRows) {
            if (qAbs(maskResult.stickyHeaderPx - m_lastHeaderCandidate) <= 2) {
                ++m_headerStableCount;
            } else {
                m_headerStableCount = 1;
            }
            m_lastHeaderCandidate = maskResult.stickyHeaderPx;
            if (m_headerStableCount >= kStickyLockFrames) {
                m_stickyHeaderPx = qMin(maskResult.stickyHeaderPx, current.height() / 4);
            }
        } else {
            m_headerStableCount = 0;
            m_lastHeaderCandidate = 0;
            m_stickyHeaderPx = 0;
        }

        if (maskResult.stickyFooterPx >= kMinStableRows) {
            if (qAbs(maskResult.stickyFooterPx - m_lastFooterCandidate) <= 2) {
                ++m_footerStableCount;
            } else {
                m_footerStableCount = 1;
            }
            m_lastFooterCandidate = maskResult.stickyFooterPx;
            if (m_footerStableCount >= kStickyLockFrames) {
                m_stickyFooterPx = qMin(maskResult.stickyFooterPx, current.height() / 4);
            }
        } else {
            m_footerStableCount = 0;
            m_lastFooterCandidate = 0;
            m_stickyFooterPx = 0;
        }
    } else {
        updateStickyBands(m_lastGray, currentGray);
    }

    int bottomIgnorePx = 0;
    if (m_options.autoIgnoreBottomEdge) {
        const int rawBottomIgnoreCandidate = calculateBottomIgnore(m_lastGray, currentGray);
        // Only treat a narrow unstable strip as "bottom-edge instability".
        // Large candidates usually mean global scroll motion rather than a footer artifact.
        const int maxEdgeBandPx = qMax(24, currentGray.height() / 8);
        const int bottomIgnoreCandidate =
            (rawBottomIgnoreCandidate > 0 && rawBottomIgnoreCandidate <= maxEdgeBandPx)
                ? rawBottomIgnoreCandidate
                : 0;
        updateBottomIgnoreStability(bottomIgnoreCandidate);

        // Prefer stable value once available, otherwise use the current candidate.
        bottomIgnorePx = (m_committedBottomIgnore > 0) ? m_committedBottomIgnore
                                                       : bottomIgnoreCandidate;
        const int minRowsForAlignment = qMax(8, m_options.minScrollAmount);
        bottomIgnorePx = qBound(0, bottomIgnorePx, qMax(0, currentGray.height() - minRowsForAlignment));
    } else {
        updateBottomIgnoreStability(0);
    }

    if (bottomIgnorePx > 0) {
        auto maskBottomRows = [bottomIgnorePx](QImage *image) {
            if (!image || image->isNull() || bottomIgnorePx <= 0) {
                return;
            }

            const int startRow = qMax(0, image->height() - bottomIgnorePx);
            for (int y = startRow; y < image->height(); ++y) {
                uchar *line = image->scanLine(y);
                if (!line) {
                    continue;
                }
                std::memset(line, 0, static_cast<size_t>(image->bytesPerLine()));
            }
        };

        previousForAlignment = previousForAlignment.copy();
        currentForAlignment = currentForAlignment.copy();
        maskBottomRows(&previousForAlignment);
        maskBottomRows(&currentForAlignment);
    }

    const ScrollAlignmentResult alignment = m_alignmentEngine.align(previousForAlignment,
                                                                    currentForAlignment,
                                                                    estimatedDy);
    result.confidence = alignment.confidence;
    result.overlapY = alignment.overlapPx;
    result.overlapPx = alignment.overlapPx;
    result.stickyHeaderPx = m_stickyHeaderPx;
    result.stickyFooterPx = m_stickyFooterPx;
    result.bottomIgnorePx = bottomIgnorePx;
    result.strategy = toStitchStrategy(alignment.strategy);
    result.dySigned = alignment.dySigned;
    result.dx = alignment.dx;
    result.scale = alignment.scale;
    result.shear = alignment.shear;
    result.mad = alignment.mad;
    result.rawNccScore = alignment.rawNccScore;
    result.confidencePenalty = alignment.confidencePenalty;
    result.movementVerified = alignment.movementVerified;
    result.rejectedByConfidence = alignment.rejectedByConfidence;
    result.poissonApplied = false;

    if (!alignment.ok) {
        if (alignment.rejectedByConfidence) {
            result.quality = StitchQuality::Bad;
            result.rejectionCode = StitchRejectionCode::LowConfidence;
        } else {
            result.quality = StitchQuality::NoChange;
            result.confidence = qBound(0.0, 1.0 - centerDiff, 1.0);
            result.rejectionCode = StitchRejectionCode::None;
        }
        return finalizeWithObservedHistory(result);
    }

    const int dy = qAbs(alignment.dySigned);
    if (dy < m_options.minScrollAmount) {
        result.quality = StitchQuality::NoChange;
        result.rejectionCode = StitchRejectionCode::None;
        return finalizeWithObservedHistory(result);
    }

    const int overlap = qMax(0, current.height() - dy);
    const int minOverlap = qMax(24, current.height() / 5);
    if (overlap < minOverlap) {
        result.quality = StitchQuality::Bad;
        result.rejectionCode = StitchRejectionCode::OverlapTooSmall;
        return finalizeWithObservedHistory(result);
    }

    int overlapTop = qBound(0, m_stickyHeaderPx, overlap - 1);
    int overlapBottomCrop = qBound(0, m_stickyFooterPx, overlap - overlapTop);
    int overlapHeight = overlap - overlapTop - overlapBottomCrop;
    if (overlapHeight < 8) {
        overlapTop = 0;
        overlapBottomCrop = 0;
        overlapHeight = overlap;
    }

    int appendStart = qBound(0, overlap, current.height());
    if (overlapHeight >= 8 &&
        dy + overlapTop + overlapHeight <= m_lastFrame.height() &&
        overlapTop + overlapHeight <= current.height()) {
        const QImage previousOverlap = m_lastFrame.copy(0, dy + overlapTop, current.width(), overlapHeight);
        const QImage currentOverlap = current.copy(0, overlapTop, current.width(), overlapHeight);
        if (!previousOverlap.isNull() && !currentOverlap.isNull()) {
            const int seamRow = m_seamBlender.findMinimumEnergySeamRow(previousOverlap, currentOverlap);
            appendStart = qBound(0, overlapTop + seamRow, current.height());
        }
    }

    const int appendEnd = current.height() - qBound(0, m_stickyFooterPx, current.height() - 1);
    int appendHeight = appendEnd - appendStart;
    if (appendHeight < m_options.minScrollAmount) {
        result.quality = StitchQuality::NoChange;
        result.rejectionCode = StitchRejectionCode::None;
        return finalizeWithObservedHistory(result);
    }

    const qint64 width = static_cast<qint64>(current.width());
    const qint64 maxHeightByPixels =
        static_cast<qint64>(m_options.maxOutputPixels) / qMax<qint64>(1, width);
    const qint64 maxHeightAllowed = qMin<qint64>(maxHeightByPixels, m_options.maxOutputHeightPx);
    appendHeight = qMin<qint64>(appendHeight,
                                qMax<qint64>(0, maxHeightAllowed - m_totalHeight));

    if (appendHeight <= 0) {
        result.quality = StitchQuality::NoChange;
        result.rejectionCode = StitchRejectionCode::None;
        return finalizeWithObservedHistory(result);
    }

    QImage segment = current.copy(0, appendStart, current.width(), appendHeight);
    if (segment.isNull()) {
        result.quality = StitchQuality::Bad;
        result.rejectionCode = StitchRejectionCode::InvalidFrame;
        return finalizeWithObservedHistory(result);
    }

    const int expectedDy = (m_hasDyHistory && m_dyHistoryCount > 0)
        ? qMax(m_options.minScrollAmount, qRound(m_averageDy))
        : qMax(m_options.minScrollAmount, dy);
    const int dyDeviation = qAbs(appendHeight - expectedDy);
    const double normalizedDeviation =
        static_cast<double>(dyDeviation) / static_cast<double>(qMax(expectedDy, 1));
    result.appendPlausibilityScore = qBound(0.0, 1.0 - normalizedDeviation * 0.65, 1.0);

    if (!m_segments.isEmpty()) {
        const StitchSegment &previousSegment = m_segments.last();
        const int compareHeight = qMin(previousSegment.image.height(), segment.height());
        if (compareHeight >= qMax(72, m_options.minScrollAmount * 4)) {
            const QImage previousTail = previousSegment.image.copy(
                0,
                previousSegment.image.height() - compareHeight,
                previousSegment.image.width(),
                compareHeight);
            const QImage segmentHead = segment.copy(
                0,
                0,
                segment.width(),
                compareHeight);

            const QImage previousTailGray = toGray(previousTail);
            const QImage segmentHeadGray = toGray(segmentHead);
            const double tailDiff = regionDifference(previousTailGray, 0,
                                                     segmentHeadGray, 0,
                                                     segment.width(), compareHeight,
                                                     edgeCrop, edgeCrop);

            const int duplicateDyLimit = qMax(40, m_options.minScrollAmount * 4);
            const int duplicateAppendLimit = qMax(56, m_options.minScrollAmount * 5);
            const bool strictDuplicate =
                tailDiff <= kDuplicateTailDiffThreshold &&
                dy <= duplicateDyLimit &&
                appendHeight <= duplicateAppendLimit &&
                result.confidence <= 0.95;
            const bool looseDuplicate =
                tailDiff <= kDuplicateTailDiffLooseThreshold &&
                result.appendPlausibilityScore < 0.25 &&
                result.confidence < 0.97;
            if (strictDuplicate || looseDuplicate) {
                result.quality = StitchQuality::NoChange;
                result.rejectionCode = StitchRejectionCode::DuplicateTail;
                result.confidence = qMin(result.confidence, 0.90);
                return finalizeWithObservedHistory(result);
            }
        }
    }

    const quint64 incomingFingerprint = segmentFingerprint(toGray(segment));
    if (detectFingerprintLoop(incomingFingerprint)) {
        result.quality = StitchQuality::NoChange;
        result.rejectionCode = StitchRejectionCode::DuplicateLoop;
        result.duplicateLoopDetected = true;
        result.confidence = qMin(result.confidence, 0.90);
        return finalizeWithObservedHistory(result);
    }

    const bool implausibleAppend =
        (result.appendPlausibilityScore < 0.18 || appendHeight > expectedDy * 2) &&
        result.confidence < 0.97;
    if (implausibleAppend) {
        result.quality = StitchQuality::NoChange;
        result.rejectionCode = StitchRejectionCode::ImplausibleAppend;
        result.confidence = qMin(result.confidence, 0.90);
        return finalizeWithObservedHistory(result);
    }

    if (result.confidence >= m_options.goodThreshold) {
        result.quality = StitchQuality::Good;
        result.rejectionCode = StitchRejectionCode::None;
    } else if (result.confidence >= m_options.partialThreshold) {
        result.quality = StitchQuality::PartiallyGood;
        result.rejectionCode = StitchRejectionCode::None;
    } else {
        result.quality = StitchQuality::Bad;
        result.rejectionCode = StitchRejectionCode::LowConfidence;
        return finalizeWithObservedHistory(result);
    }

    UndoState undoState;
    undoState.segmentCount = m_segments.size();
    undoState.totalHeight = m_totalHeight;
    undoState.frameCount = m_frameCount;
    undoState.stickyHeaderPx = m_stickyHeaderPx;
    undoState.stickyFooterPx = m_stickyFooterPx;
    undoState.headerStableCount = m_headerStableCount;
    undoState.footerStableCount = m_footerStableCount;
    undoState.lastHeaderCandidate = m_lastHeaderCandidate;
    undoState.lastFooterCandidate = m_lastFooterCandidate;
    undoState.hasBestMatch = m_hasBestMatch;
    undoState.bestMatchDy = m_bestMatchDy;
    undoState.bestBottomIgnore = m_bestBottomIgnore;
    undoState.bestMatchedRows = m_bestMatchedRows;
    undoState.bottomIgnoreCandidate = m_bottomIgnoreCandidate;
    undoState.bottomIgnoreStableCount = m_bottomIgnoreStableCount;
    undoState.committedBottomIgnore = m_committedBottomIgnore;
    undoState.hasDyHistory = m_hasDyHistory;
    undoState.averageDy = m_averageDy;
    undoState.dyHistoryCount = m_dyHistoryCount;
    undoState.lastFrame = m_lastFrame;
    undoState.lastGray = m_lastGray;
    undoState.observedPreviousGray = m_observedPreviousGray;
    undoState.observedOlderGray = m_observedOlderGray;
    undoState.hasLastResult = m_hasLastResult;
    undoState.lastResult = m_lastResult;
    undoState.recentFingerprints = m_recentFingerprints;
    m_undoStack.push_back(std::move(undoState));
    if (m_undoStack.size() > kMaxUndoHistory) {
        m_undoStack.removeFirst();
    }

    if (!m_segments.isEmpty() && m_options.usePoissonSeam) {
        const StitchSegment &previousSegment = m_segments.last();
        const int seamHeight = qMin(m_options.seamWidthPx,
                                    qMin(previousSegment.image.height(), segment.height()));
        if (seamHeight >= 8) {
            const QImage previousBand = previousSegment.image.copy(
                0,
                previousSegment.image.height() - seamHeight,
                previousSegment.image.width(),
                seamHeight);
            const QImage currentBand = segment.copy(0, 0, segment.width(), seamHeight);
            const ScrollSeamBlendResult seamResult = m_seamBlender.blendTopBand(
                previousBand,
                currentBand,
                cv::Mat());
            if (seamResult.ok && !seamResult.blendedTopBand.isNull()) {
                QPainter seamPainter(&segment);
                seamPainter.drawImage(0, 0, seamResult.blendedTopBand);
                seamPainter.end();
                result.poissonApplied = seamResult.poissonApplied;
            }
        }
    }

    StitchSegment appendedSegment;
    appendedSegment.image = segment;
    appendedSegment.appendHeight = appendHeight;
    appendedSegment.trimTop = appendStart;
    appendedSegment.trimBottom = 0;
    appendedSegment.confidence = result.confidence;
    appendedSegment.strategy = result.strategy;
    m_segments.push_back(appendedSegment);
    if (incomingFingerprint != 0) {
        m_recentFingerprints.push_back(incomingFingerprint);
        while (m_recentFingerprints.size() > kFingerprintHistory) {
            m_recentFingerprints.removeFirst();
        }
    }
    m_totalHeight += appendHeight;
    ++m_frameCount;
    result.appendHeight = appendHeight;
    result.movementVerified =
        (result.quality == StitchQuality::Good || result.quality == StitchQuality::PartiallyGood) &&
        appendHeight >= m_options.minScrollAmount;
    invalidatePreviewCache();

    if (!m_hasDyHistory) {
        m_hasDyHistory = true;
        m_averageDy = static_cast<double>(dy);
        m_dyHistoryCount = 1;
    } else {
        const int sampleCount = qMin(20, m_dyHistoryCount);
        m_averageDy = ((m_averageDy * sampleCount) + static_cast<double>(dy)) /
                      static_cast<double>(sampleCount + 1);
        m_dyHistoryCount = qMin(20, m_dyHistoryCount + 1);
    }

    m_lastFrame = current;
    m_lastGray = currentGray;
    result.rejectionCode = StitchRejectionCode::None;
    return finalizeWithObservedHistory(result);
}

bool ScrollStitchEngine::canUndoLastSegment() const
{
    return m_started && m_segments.size() > 1 && !m_undoStack.isEmpty();
}

bool ScrollStitchEngine::undoLastSegment()
{
    if (!canUndoLastSegment()) {
        return false;
    }

    const UndoState state = m_undoStack.takeLast();
    if (state.segmentCount <= 0 || state.segmentCount > m_segments.size()) {
        return false;
    }
    m_segments.resize(state.segmentCount);
    m_totalHeight = state.totalHeight;
    m_frameCount = state.frameCount;
    m_stickyHeaderPx = state.stickyHeaderPx;
    m_stickyFooterPx = state.stickyFooterPx;
    m_headerStableCount = state.headerStableCount;
    m_footerStableCount = state.footerStableCount;
    m_lastHeaderCandidate = state.lastHeaderCandidate;
    m_lastFooterCandidate = state.lastFooterCandidate;
    m_hasBestMatch = state.hasBestMatch;
    m_bestMatchDy = state.bestMatchDy;
    m_bestBottomIgnore = state.bestBottomIgnore;
    m_bestMatchedRows = state.bestMatchedRows;
    m_bottomIgnoreCandidate = state.bottomIgnoreCandidate;
    m_bottomIgnoreStableCount = state.bottomIgnoreStableCount;
    m_committedBottomIgnore = state.committedBottomIgnore;
    m_hasDyHistory = state.hasDyHistory;
    m_averageDy = state.averageDy;
    m_dyHistoryCount = state.dyHistoryCount;
    m_lastFrame = state.lastFrame;
    m_lastGray = state.lastGray;
    m_observedPreviousGray = state.observedPreviousGray;
    m_observedOlderGray = state.observedOlderGray;
    m_hasLastResult = state.hasLastResult;
    m_lastResult = state.lastResult;
    m_recentFingerprints = state.recentFingerprints;
    invalidatePreviewCache();
    return true;
}

StitchPreviewMeta ScrollStitchEngine::previewMeta() const
{
    StitchPreviewMeta meta;
    meta.frames = m_frameCount;
    meta.height = m_totalHeight;
    meta.stickyHeaderPx = m_stickyHeaderPx;
    meta.stickyFooterPx = m_stickyFooterPx;

    if (m_hasLastResult) {
        meta.hasLastAppend = true;
        meta.lastAppend = m_lastResult.appendHeight;
        meta.lastConfidence = m_lastResult.confidence;
        meta.lastStrategy = m_lastResult.strategy;
        meta.lastQuality = m_lastResult.quality;
        meta.bottomIgnorePx = m_lastResult.bottomIgnorePx;
    }
    return meta;
}

QImage ScrollStitchEngine::composeFinal() const
{
    if (!m_started || m_segments.isEmpty()) {
        m_composedCacheValid = false;
        m_cachedComposed = QImage();
        return QImage();
    }

    if (m_composedCacheValid && !m_cachedComposed.isNull()) {
        return m_cachedComposed;
    }

    const int width = m_segments.first().image.width();
    if (width <= 0 || m_totalHeight <= 0) {
        m_composedCacheValid = false;
        m_cachedComposed = QImage();
        return QImage();
    }

    QImage output(width, m_totalHeight, QImage::Format_ARGB32_Premultiplied);
    output.fill(m_backgroundColor);

    QPainter painter(&output);
    int y = 0;
    for (const StitchSegment &segment : m_segments) {
        if (segment.image.isNull()) {
            continue;
        }
        painter.drawImage(0, y, segment.image);
        y += segment.image.height();
        if (y >= output.height()) {
            break;
        }
    }
    painter.end();

    // Scroll capture output should always be opaque to avoid transparent seam artifacts.
    for (int y = 0; y < output.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(output.scanLine(y));
        for (int x = 0; x < output.width(); ++x) {
            line[x] = qRgba(qRed(line[x]), qGreen(line[x]), qBlue(line[x]), 255);
        }
    }

    m_cachedComposed = output;
    m_composedCacheValid = true;
    return m_cachedComposed;
}

QImage ScrollStitchEngine::preview(int maxWidth, int maxHeight) const
{
    if (m_previewCacheValid &&
        m_cachedPreviewSize == QSize(maxWidth, maxHeight) &&
        !m_cachedPreview.isNull()) {
        return m_cachedPreview;
    }

    QImage composed = composeFinal();
    if (composed.isNull() || maxWidth <= 0 || maxHeight <= 0) {
        m_previewCacheValid = false;
        m_cachedPreview = QImage();
        m_cachedPreviewSize = QSize();
        return composed;
    }

    m_cachedPreview = composed.scaled(maxWidth, maxHeight,
                                      Qt::KeepAspectRatio,
                                      Qt::FastTransformation);
    m_cachedPreviewSize = QSize(maxWidth, maxHeight);
    m_previewCacheValid = !m_cachedPreview.isNull();
    return m_cachedPreview;
}
