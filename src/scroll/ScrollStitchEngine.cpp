#include "scroll/ScrollStitchEngine.h"

#include <QPainter>

#include <QtGlobal>
#include <algorithm>
#include <cstring>

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
{
    if (m_options.partialThreshold > m_options.goodThreshold) {
        m_options.partialThreshold = m_options.goodThreshold;
    }
    m_options.goodThreshold = qBound(0.0, m_options.goodThreshold, 1.0);
    m_options.partialThreshold = qBound(0.0, m_options.partialThreshold, 1.0);
    m_options.minScrollAmount = qMax(1, m_options.minScrollAmount);
    m_options.maxFrames = qMax(1, m_options.maxFrames);
    m_options.maxOutputPixels = qMax(1024, m_options.maxOutputPixels);
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
    const int rowStep = height > 180 ? 2 : 1;
    const int colStep = sampleWidth > 280 ? 2 : 1;

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

    for (int dy = m_options.minScrollAmount; dy <= effectiveHeight - minOverlap; ++dy) {
        const int overlap = effectiveHeight - dy;
        int compareStartPrev = dy + m_stickyHeaderPx;
        int compareStartCurr = m_stickyHeaderPx;
        int compareHeight = overlap - m_stickyHeaderPx - m_stickyFooterPx;
        if (compareHeight < minOverlap / 2) {
            continue;
        }

        if (compareStartPrev + compareHeight > prevGray.height() ||
            compareStartCurr + compareHeight > currGray.height()) {
            continue;
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

    for (int dy = m_options.minScrollAmount; dy <= effectiveHeight - minOverlap; ++dy) {
        const int overlap = effectiveHeight - dy;
        const int compareStartPrev = dy + m_stickyHeaderPx;
        const int compareStartCurr = m_stickyHeaderPx;
        const int compareHeight = overlap - m_stickyHeaderPx - m_stickyFooterPx;
        if (compareHeight < minOverlap / 2) {
            continue;
        }

        if (compareStartPrev + compareHeight > prevGray.height() ||
            compareStartCurr + compareHeight > currGray.height()) {
            continue;
        }

        const int rowStep = compareHeight > 180 ? 2 : 1;
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
            continue;
        }

        const double robustDiff = robustScoreFromSamples(columnDiffs);
        const double confidence = 1.0 - robustDiff;
        if (confidence > bestConfidence ||
            (qAbs(confidence - bestConfidence) < 1e-6 && compareHeight > bestMatchedRows)) {
            bestConfidence = confidence;
            bestDy = dy;
            bestMatchedRows = compareHeight;
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

    const qint64 maxHeightAllowed =
        static_cast<qint64>(m_options.maxOutputPixels) / qMax<qint64>(1, width);
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
    m_backgroundColor = estimateBackgroundColor(m_lastFrame);

    StitchSegment initialSegment;
    initialSegment.image = m_lastFrame;
    initialSegment.appendHeight = m_lastFrame.height();
    initialSegment.trimTop = 0;
    initialSegment.trimBottom = 0;
    initialSegment.confidence = 1.0;
    initialSegment.strategy = StitchMatchStrategy::RowDiff;
    m_segments.push_back(initialSegment);
    m_totalHeight = m_lastFrame.height();
    m_frameCount = 1;
    m_started = true;
    return true;
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
        return finalizeResult(result);
    }

    if (m_frameCount >= m_options.maxFrames) {
        result.quality = StitchQuality::NoChange;
        return finalizeResult(result);
    }

    QImage current = normalizeOpaqueFrame(frame);
    if (current.size() != m_lastFrame.size()) {
        result.quality = StitchQuality::Bad;
        return finalizeResult(result);
    }

    const QImage currentGray = toGray(current);

    const int edgeCrop = qMin(currentGray.width() / 12, qMax(0, currentGray.width() / 2 - 1));
    const double fullFrameDiff = regionDifference(m_lastGray, 0,
                                                  currentGray, 0,
                                                  currentGray.width(), currentGray.height(),
                                                  edgeCrop, edgeCrop);
    const double centerDiff = centerActivityRatio(m_lastGray, currentGray);
    if (fullFrameDiff <= kNoChangeFullFrameThreshold || centerDiff <= kNoChangeCenterThreshold) {
        result.quality = StitchQuality::NoChange;
        result.confidence = qBound(0.0, 1.0 - qMax(fullFrameDiff, centerDiff), 1.0);
        result.stickyHeaderPx = m_stickyHeaderPx;
        result.stickyFooterPx = m_stickyFooterPx;
        return finalizeResult(result);
    }

    updateStickyBands(m_lastGray, currentGray);
    const int bottomIgnore = calculateBottomIgnore(m_lastGray, currentGray);
    const MatchResult rowMatch = findBestVerticalMatch(m_lastGray, currentGray, bottomIgnore);
    MatchResult match = rowMatch;

    const int gutterWidth = qBound(16, currentGray.width() / 10, currentGray.width() / 4);
    const double leftGutterDiff = regionDifference(m_lastGray, 0,
                                                   currentGray, 0,
                                                   currentGray.width(), currentGray.height(),
                                                   0, currentGray.width() - gutterWidth);
    const double rightGutterDiff = regionDifference(m_lastGray, 0,
                                                    currentGray, 0,
                                                    currentGray.width(), currentGray.height(),
                                                    currentGray.width() - gutterWidth, 0);
    const bool sideGutterLikelyStatic =
        centerDiff > 0.012 && qMax(leftGutterDiff, rightGutterDiff) < (centerDiff * 0.55);
    const bool stickyBandsDetected = (m_stickyHeaderPx > 0 || m_stickyFooterPx > 0);
    const bool lowRowConfidence =
        rowMatch.valid &&
        rowMatch.confidence < m_options.goodThreshold &&
        rowMatch.confidence >= (m_options.partialThreshold * 0.75);

    const bool shouldTryColumnFallback =
        !rowMatch.valid || lowRowConfidence || stickyBandsDetected || sideGutterLikelyStatic;
    if (shouldTryColumnFallback) {
        const MatchResult columnMatch = findColumnSampleMatch(m_lastGray, currentGray, bottomIgnore);
        if (columnMatch.valid &&
            (!rowMatch.valid || columnMatch.confidence > (rowMatch.confidence + kColumnFallbackMinGain))) {
            match = columnMatch;
        }
    }

    result.confidence = match.confidence;
    result.overlapY = match.overlap;
    result.stickyHeaderPx = m_stickyHeaderPx;
    result.stickyFooterPx = m_stickyFooterPx;
    result.bottomIgnorePx = clampPositive(match.bottomIgnore);
    result.strategy = match.strategy;

    if (!match.valid) {
        const bool allowBestGuess =
            m_hasBestMatch &&
            m_dyHistoryCount >= kBestGuessMinDyHistory &&
            centerDiff >= kBestGuessMinCenterActivity;
        if (!allowBestGuess) {
            result.quality = StitchQuality::NoChange;
            result.confidence = qBound(0.0, 1.0 - centerDiff, 1.0);
            return finalizeResult(result);
        }

        match.valid = true;
        match.bestGuess = true;
        match.dy = m_bestMatchDy;
        match.overlap = current.height() - match.dy;
        match.bottomIgnore = m_bestBottomIgnore;
        match.matchedRows = m_bestMatchedRows;
        match.confidence = m_options.partialThreshold;
        match.strategy = StitchMatchStrategy::BestGuess;
        result.confidence = match.confidence;
        result.overlapY = match.overlap;
        result.bottomIgnorePx = clampPositive(match.bottomIgnore);
        result.strategy = match.strategy;
    }

    if (match.dy < m_options.minScrollAmount) {
        result.quality = StitchQuality::NoChange;
        return finalizeResult(result);
    }

    const int minOverlap = qMax(24, current.height() / 5);
    const int effectiveHeight = current.height() - clampPositive(match.bottomIgnore);
    const int maxSearchDy = qMax(m_options.minScrollAmount, effectiveHeight - minOverlap);
    if (m_hasDyHistory && m_dyHistoryCount >= 1) {
        const bool nearSearchCeiling =
            match.dy >= (maxSearchDy - qMax(6, current.height() / 24));
        const bool largeAbsoluteJump = match.dy >= (current.height() / 2);
        const int allowedDy = qMax(static_cast<int>(qRound(m_averageDy * 2.2)),
                                   m_options.minScrollAmount * 3);
        if ((nearSearchCeiling || largeAbsoluteJump) && match.dy > allowedDy) {
            result.quality = StitchQuality::NoChange;
            result.confidence = qMin(result.confidence, 0.25);
            return finalizeResult(result);
        }
    }

    if (match.bestGuess) {
        result.quality = StitchQuality::PartiallyGood;
    } else if (match.confidence >= m_options.goodThreshold) {
        result.quality = StitchQuality::Good;
    } else if (match.confidence >= m_options.partialThreshold) {
        result.quality = StitchQuality::PartiallyGood;
    } else {
        result.quality = StitchQuality::Bad;
        return finalizeResult(result);
    }

    // match.overlap is already in full-frame coordinates (h - dy).
    const int appendStart = qMin(current.height(), clampPositive(match.overlap));
    const int appendEnd = current.height() - clampPositive(m_stickyFooterPx);
    int appendHeight = appendEnd - appendStart;
    if (appendHeight < m_options.minScrollAmount) {
        result.quality = StitchQuality::NoChange;
        return finalizeResult(result);
    }

    const qint64 width = static_cast<qint64>(current.width());
    const qint64 nextPixels = width * (static_cast<qint64>(m_totalHeight) + static_cast<qint64>(appendHeight));
    if (nextPixels > m_options.maxOutputPixels) {
        const int maxHeightAllowed = static_cast<int>(m_options.maxOutputPixels / qMax<qint64>(1, width));
        appendHeight = qMax(0, maxHeightAllowed - m_totalHeight);
    }

    if (appendHeight <= 0) {
        result.quality = StitchQuality::NoChange;
        return finalizeResult(result);
    }

    QImage segment = current.copy(0, appendStart, current.width(), appendHeight);
    if (segment.isNull()) {
        result.quality = StitchQuality::Bad;
        return finalizeResult(result);
    }

    UndoState undoState;
    undoState.segments = m_segments;
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
    undoState.hasLastResult = m_hasLastResult;
    undoState.lastResult = m_lastResult;
    m_undoStack.push_back(std::move(undoState));

    if (!match.bestGuess) {
        updateBottomIgnoreStability(match.bottomIgnore);
    }

    int pendingTrim = 0;
    const int trimBottom = clampPositive(m_committedBottomIgnore);
    if (trimBottom > 0 && !m_segments.isEmpty()) {
        pendingTrim = qMin(trimBottom, qMax(0, m_segments.last().image.height() - 1));
    }

    if (pendingTrim > 0 && !m_segments.isEmpty()) {
        StitchSegment &lastSegment = m_segments.last();
        QImage trimmed = lastSegment.image.copy(0, 0,
                                                lastSegment.image.width(),
                                                lastSegment.image.height() - pendingTrim);
        if (!trimmed.isNull()) {
            m_totalHeight -= pendingTrim;
            lastSegment.image = trimmed;
            lastSegment.trimBottom += pendingTrim;
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
    m_totalHeight += appendHeight;
    ++m_frameCount;
    result.appendHeight = appendHeight;
    invalidatePreviewCache();

    if (!match.bestGuess && match.matchedRows > m_bestMatchedRows) {
        m_hasBestMatch = true;
        m_bestMatchDy = match.dy;
        m_bestBottomIgnore = match.bottomIgnore;
        m_bestMatchedRows = match.matchedRows;
    }

    if (!match.bestGuess) {
        if (!m_hasDyHistory) {
            m_hasDyHistory = true;
            m_averageDy = static_cast<double>(match.dy);
            m_dyHistoryCount = 1;
        } else {
            const int sampleCount = qMin(20, m_dyHistoryCount);
            m_averageDy = ((m_averageDy * sampleCount) + static_cast<double>(match.dy)) /
                          static_cast<double>(sampleCount + 1);
            m_dyHistoryCount = qMin(20, m_dyHistoryCount + 1);
        }
    }

    m_lastFrame = current;
    m_lastGray = currentGray;
    return finalizeResult(result);
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
    m_segments = state.segments;
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
    m_hasLastResult = state.hasLastResult;
    m_lastResult = state.lastResult;
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

    if (maxWidth < 220 || maxHeight < 120) {
        m_cachedPreview = composed.scaled(maxWidth, maxHeight,
                                          Qt::KeepAspectRatio,
                                          Qt::SmoothTransformation);
        m_cachedPreviewSize = QSize(maxWidth, maxHeight);
        m_previewCacheValid = !m_cachedPreview.isNull();
        return m_cachedPreview;
    }

    const int gap = 8;
    const int miniWidth = qBound(80, maxWidth / 5, 140);
    const int mainWidth = maxWidth - miniWidth - gap;
    if (mainWidth < 80) {
        m_cachedPreview = composed.scaled(maxWidth, maxHeight,
                                          Qt::KeepAspectRatio,
                                          Qt::SmoothTransformation);
        m_cachedPreviewSize = QSize(maxWidth, maxHeight);
        m_previewCacheValid = !m_cachedPreview.isNull();
        return m_cachedPreview;
    }

    QImage canvas(maxWidth, maxHeight, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(QColor(15, 23, 42, 255));

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QRect mainRect(0, 0, mainWidth, maxHeight);
    const int sourceHeightByAspect = qRound(static_cast<double>(composed.width()) *
                                            static_cast<double>(mainRect.height()) /
                                            qMax(1, mainRect.width()));
    const int sourceHeight = qBound(1, sourceHeightByAspect, composed.height());
    const int sourceY = qMax(0, composed.height() - sourceHeight);
    const QRect sourceRect(0, sourceY, composed.width(), sourceHeight);
    painter.drawImage(mainRect, composed, sourceRect);
    painter.setPen(QPen(QColor(148, 163, 184, 180), 1));
    painter.drawRect(mainRect.adjusted(0, 0, -1, -1));

    const QRect miniSlot(mainWidth + gap, 0, miniWidth, maxHeight);
    painter.fillRect(miniSlot, QColor(30, 41, 59, 210));

    const double scale = qMin(static_cast<double>(miniSlot.width()) / qMax(1, composed.width()),
                              static_cast<double>(miniSlot.height()) / qMax(1, composed.height()));
    const int miniImageWidth = qMax(1, qRound(composed.width() * scale));
    const int miniImageHeight = qMax(1, qRound(composed.height() * scale));
    const QRect miniRect(miniSlot.center().x() - miniImageWidth / 2,
                         miniSlot.center().y() - miniImageHeight / 2,
                         miniImageWidth,
                         miniImageHeight);
    painter.drawImage(miniRect, composed);
    painter.setPen(QPen(QColor(148, 163, 184, 180), 1));
    painter.drawRect(miniRect.adjusted(0, 0, -1, -1));

    const int viewportTop = miniRect.top() +
        qRound(static_cast<double>(sourceY) / qMax(1, composed.height()) * miniRect.height());
    const int viewportBottom = miniRect.top() +
        qRound(static_cast<double>(sourceY + sourceHeight) / qMax(1, composed.height()) * miniRect.height());
    const QRect viewportRect(miniRect.left() + 1,
                             qBound(miniRect.top(), viewportTop, miniRect.bottom()),
                             qMax(1, miniRect.width() - 2),
                             qMax(2, viewportBottom - viewportTop));
    painter.setPen(QPen(QColor(242, 201, 76, 230), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(viewportRect.adjusted(0, 0, -1, -1));

    painter.end();

    m_cachedPreview = canvas;
    m_cachedPreviewSize = QSize(maxWidth, maxHeight);
    m_previewCacheValid = !m_cachedPreview.isNull();
    return m_cachedPreview;
}
