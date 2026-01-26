#include "scrolling/ImageCombiner.h"
#include <QPainter>
#include <QDebug>
#include <cstring>
#include <QtGlobal>
#include <opencv2/imgproc.hpp>

namespace {
constexpr int kMinEdgeLines = 4;
constexpr int kSampleStep = 6;
constexpr int kPixelDiffThreshold = 25;
constexpr double kMaxMismatchRatio = 0.10;
}

ImageCombiner::ImageCombiner()
{
}

void ImageCombiner::setOptions(int minMatchLines, bool autoIgnoreBottom)
{
    m_minMatchLines = minMatchLines;
    m_autoIgnoreBottom = autoIgnoreBottom;
}

void ImageCombiner::reset()
{
    m_result = QImage();
    m_frameCount = 0;
    m_lastMatchCount = 0;
    m_lastMatchIndex = -1;
    m_lastIgnoreBottomOffset = 0;
    m_lastIgnoreTopOffset = 0;
}

void ImageCombiner::setFirstFrame(const QImage& frame)
{
    if (frame.isNull()) {
        m_result = QImage();
        m_frameCount = 0;
        return;
    }

    QImage normalized = frame;
    if (normalized.format() != QImage::Format_ARGB32) {
        normalized = normalized.convertToFormat(QImage::Format_ARGB32);
    }

    m_result = normalized;
    m_frameCount = 1;
    m_lastMatchCount = 0;
    m_lastMatchIndex = -1;
    m_lastIgnoreBottomOffset = 0;
    m_lastIgnoreTopOffset = 0;
}

CombineResult ImageCombiner::addFrame(const QImage& current)
{
    if (current.isNull() || current.width() <= 0 || current.height() <= 0) {
        return {CaptureStatus::Failed, -1, 0, 0};
    }

    QImage currentFrame = current;
    if (currentFrame.format() != QImage::Format_ARGB32) {
        currentFrame = currentFrame.convertToFormat(QImage::Format_ARGB32);
    }

    // First frame case
    if (m_result.isNull()) {
        setFirstFrame(currentFrame);
        return {CaptureStatus::Successful, 0, currentFrame.height(), 0};
    }

    if (m_result.format() != QImage::Format_ARGB32) {
        m_result = m_result.convertToFormat(QImage::Format_ARGB32);
    }

    if (m_result.width() != currentFrame.width()) {
        return {CaptureStatus::Failed, -1, 0, 0};
    }

    const int width = currentFrame.width();
    const int resultHeight = m_result.height();
    const int currentHeight = currentFrame.height();

    qDebug() << "ImageCombiner::addFrame - result size:" << m_result.size()
             << "current size:" << currentFrame.size()
             << "result format:" << m_result.format()
             << "current format:" << currentFrame.format();

    // Compute side margin (ignore scrollbar area)
    // ShareX: Max(50, Width/20), capped at Width/3
    int sideMargin = ScrollingCaptureOptions::computeSideMargin(width);
    int maxSideMargin = qMax(0, (width - 1) / 2);
    sideMargin = qBound(0, sideMargin, maxSideMargin);
    int compareWidth = width - (sideMargin * 2);

    if (compareWidth <= 0) {
        return {CaptureStatus::Failed, -1, 0, 0};
    }

    int ignoreTopOffset = detectTopIgnoreOffset(m_result, currentFrame, sideMargin, compareWidth);
    if (ignoreTopOffset == 0 && m_lastIgnoreTopOffset > 0) {
        ignoreTopOffset = m_lastIgnoreTopOffset;
    }

    int ignoreBottomOffset = 0;
    if (m_autoIgnoreBottom) {
        ignoreBottomOffset = detectBottomIgnoreOffset(
            m_result, resultHeight, currentFrame, sideMargin, compareWidth);
    }
    if (ignoreBottomOffset == 0 && m_lastIgnoreBottomOffset > 0) {
        ignoreBottomOffset = m_lastIgnoreBottomOffset;
    }

    int effectiveResultHeight = resultHeight - ignoreBottomOffset;
    int currentContentHeight = currentHeight - ignoreTopOffset - ignoreBottomOffset;
    if (effectiveResultHeight <= 0 || currentContentHeight <= 0) {
        return {CaptureStatus::Failed, -1, 0, 0};
    }

    qDebug() << "ImageCombiner: sideMargin:" << sideMargin
             << "compareWidth:" << compareWidth
             << "ignoreTop:" << ignoreTopOffset
             << "ignoreBottom:" << ignoreBottomOffset;

    // Try template matching first (more robust for apps with similar content)
    TemplateMatchResult tmResult = findOverlapWithTemplate(
        m_result, currentFrame, ignoreTopOffset, ignoreBottomOffset, sideMargin);

    if (tmResult.valid) {
        int newContentStart = ignoreTopOffset + tmResult.overlapLines;
        appendToResult(currentFrame, newContentStart, ignoreBottomOffset);

        // Save for fallback
        m_lastMatchCount = tmResult.overlapLines;
        m_lastMatchIndex = -1;  // Not applicable for template matching
        m_lastIgnoreBottomOffset = ignoreBottomOffset;
        m_lastIgnoreTopOffset = ignoreTopOffset;
        m_frameCount++;

        return {CaptureStatus::Successful, -1, tmResult.overlapLines, ignoreBottomOffset};
    }

    qDebug() << "Template matching failed, falling back to line comparison";

    // Fall back to line-by-line comparison
    // Search range: half of smaller content height
    int matchLimit = ScrollingCaptureOptions::computeMatchLimit(effectiveResultHeight, currentContentHeight);

    // Best match tracking
    int bestMatchCount = 0;
    int bestMatchIndex = -1;
    int bestIgnoreBottomOffset = ignoreBottomOffset;

    // Search for overlap: where does current frame's content top match result's bottom?
    // If overlap is N lines: result[overlapStart : overlapStart+N-1] == current[contentTop : contentTop+N-1]
    int contentTop = ignoreTopOffset;
    for (int overlapStart = effectiveResultHeight - matchLimit;
         overlapStart < effectiveResultHeight; overlapStart++) {
        if (overlapStart < 0) {
            continue;
        }

        int matchCount = 0;
        int maxCheck = qMin(effectiveResultHeight - overlapStart, currentContentHeight);
        for (int y = 0; y < maxCheck; y++) {
            if (compareLines(m_result, overlapStart + y,
                             currentFrame, contentTop + y,
                             sideMargin, compareWidth)) {
                matchCount++;
            } else {
                break;  // Consecutive match broken
            }
        }

        if (matchCount > bestMatchCount) {
            bestMatchCount = matchCount;
            bestMatchIndex = overlapStart;
        }
    }

    qDebug() << "ImageCombiner: bestMatchCount:" << bestMatchCount
             << "bestMatchIndex:" << bestMatchIndex
             << "minMatchLines:" << m_minMatchLines
             << "matchLimit:" << matchLimit
             << "contentHeight:" << currentContentHeight;

    // Check if we found a valid match
    if (bestMatchCount >= m_minMatchLines) {
        // New content starts after the matched region
        int newContentStart = contentTop + bestMatchCount;
        appendToResult(currentFrame, newContentStart, bestIgnoreBottomOffset);

        // Save for fallback
        m_lastMatchCount = bestMatchCount;
        m_lastMatchIndex = bestMatchIndex;
        m_lastIgnoreBottomOffset = bestIgnoreBottomOffset;
        m_lastIgnoreTopOffset = ignoreTopOffset;
        m_frameCount++;

        return {CaptureStatus::Successful, bestMatchIndex, bestMatchCount, bestIgnoreBottomOffset};
    }

    // Fallback: use last successful parameters
    if (m_lastMatchCount > 0) {
        int fallbackTop = ignoreTopOffset > 0 ? ignoreTopOffset : m_lastIgnoreTopOffset;
        int fallbackBottom = ignoreBottomOffset > 0 ? ignoreBottomOffset : m_lastIgnoreBottomOffset;
        appendToResult(currentFrame, fallbackTop + m_lastMatchCount, fallbackBottom);
        m_frameCount++;

        m_lastIgnoreBottomOffset = fallbackBottom;
        m_lastIgnoreTopOffset = fallbackTop;

        return {CaptureStatus::PartiallySuccessful, m_lastMatchIndex, 0, fallbackBottom};
    }

    // Last resort fallback: estimate overlap based on typical scroll behavior
    // Assume ~80% overlap (20% new content per scroll)
    int fallbackTop = ignoreTopOffset > 0 ? ignoreTopOffset : m_lastIgnoreTopOffset;
    int fallbackBottom = ignoreBottomOffset > 0 ? ignoreBottomOffset : m_lastIgnoreBottomOffset;
    int fallbackContentHeight = currentHeight - fallbackTop - fallbackBottom;
    if (fallbackContentHeight <= 0) {
        return {CaptureStatus::Failed, -1, 0, 0};
    }

    int estimatedOverlap = fallbackContentHeight * 80 / 100;
    if (estimatedOverlap >= m_minMatchLines) {
        qDebug() << "ImageCombiner: No match found, using estimated overlap:" << estimatedOverlap;
        appendToResult(currentFrame, fallbackTop + estimatedOverlap, fallbackBottom);
        m_frameCount++;

        // Save for future fallbacks
        m_lastMatchCount = estimatedOverlap;
        m_lastMatchIndex = effectiveResultHeight - estimatedOverlap;
        m_lastIgnoreBottomOffset = fallbackBottom;
        m_lastIgnoreTopOffset = fallbackTop;

        return {CaptureStatus::PartiallySuccessful, m_lastMatchIndex, 0, fallbackBottom};
    }

    // Complete failure - no valid match found
    return {CaptureStatus::Failed, -1, 0, 0};
}

bool ImageCombiner::compareLines(const QImage& img1, int y1,
                                  const QImage& img2, int y2,
                                  int xOffset, int width) const
{
    // Bounds check
    if (y1 < 0 || y1 >= img1.height() ||
        y2 < 0 || y2 >= img2.height()) {
        return false;
    }
    if (xOffset < 0 || width <= 0) {
        return false;
    }
    if (xOffset + width > img1.width() || xOffset + width > img2.width()) {
        return false;
    }
    if (img1.format() != QImage::Format_ARGB32 ||
        img2.format() != QImage::Format_ARGB32) {
        return false;
    }

    // Ensure both images are in the same format for memcmp
    // Assume RGBA format (4 bytes per pixel)
    const int bytesPerPixel = 4;
    const uchar* line1 = img1.constScanLine(y1) + (xOffset * bytesPerPixel);
    const uchar* line2 = img2.constScanLine(y2) + (xOffset * bytesPerPixel);

    if (std::memcmp(line1, line2, width * bytesPerPixel) == 0) {
        return true;
    }

    // Fuzzy compare to tolerate minor changes (cursor blink, subpixel shifts)
    const QRgb* pixels1 = reinterpret_cast<const QRgb*>(line1);
    const QRgb* pixels2 = reinterpret_cast<const QRgb*>(line2);
    const int samples = (width + kSampleStep - 1) / kSampleStep;
    const int maxMismatches = static_cast<int>(samples * kMaxMismatchRatio);
    int mismatches = 0;

    for (int x = 0; x < width; x += kSampleStep) {
        const QRgb p1 = pixels1[x];
        const QRgb p2 = pixels2[x];
        const int diff = qAbs(qRed(p1) - qRed(p2))
                         + qAbs(qGreen(p1) - qGreen(p2))
                         + qAbs(qBlue(p1) - qBlue(p2));
        if (diff > kPixelDiffThreshold) {
            if (++mismatches > maxMismatches) {
                return false;
            }
        }
    }

    return true;
}

int ImageCombiner::detectBottomIgnoreOffset(const QImage& result, int resultMatchY,
                                             const QImage& current,
                                             int sideMargin, int compareWidth) const
{
    Q_UNUSED(resultMatchY);

    // Sticky footer detection:
    // Compare bottom lines of result vs current.
    // If they match, the line is part of a fixed footer.

    int maxIgnore = current.height() / 3;  // Don't ignore more than 1/3
    int ignoreOffset = 0;

    for (int offset = 0; offset < maxIgnore; offset++) {
        int resultY = result.height() - 1 - offset;
        int currentY = current.height() - 1 - offset;

        // Bounds check
        if (resultY < 0 || currentY < 0) break;

        // Compare this bottom line
        if (compareLines(result, resultY, current, currentY,
                        sideMargin, compareWidth)) {
            // Same line = sticky footer
            ignoreOffset++;
        } else {
            // Different = content boundary
            break;
        }
    }

    if (ignoreOffset < kMinEdgeLines) {
        return 0;
    }

    return ignoreOffset;
}

int ImageCombiner::detectTopIgnoreOffset(const QImage& result,
                                          const QImage& current,
                                          int sideMargin, int compareWidth) const
{
    // Sticky header detection:
    // Compare top lines of result vs current.
    // If they match, the line is part of a fixed header.

    int maxIgnore = current.height() / 3;  // Don't ignore more than 1/3
    int ignoreOffset = 0;

    for (int offset = 0; offset < maxIgnore; offset++) {
        int resultY = offset;
        int currentY = offset;

        // Bounds check
        if (resultY >= result.height() || currentY >= current.height()) break;

        if (compareLines(result, resultY, current, currentY,
                         sideMargin, compareWidth)) {
            ignoreOffset++;
        } else {
            break;
        }
    }

    if (ignoreOffset < kMinEdgeLines) {
        return 0;
    }

    return ignoreOffset;
}

TemplateMatchResult ImageCombiner::findOverlapWithTemplate(
    const QImage& result, const QImage& current,
    int ignoreTop, int ignoreBottom, int sideMargin) const
{
    TemplateMatchResult res{0, 0.0, false};

    // Template: bottom portion of result (excluding footer)
    // Use ~150 lines or 1/4 of frame height, whichever is smaller
    int resultContentHeight = result.height() - ignoreBottom;
    int templateHeight = qMin(150, resultContentHeight / 4);
    if (templateHeight < 30) {
        qDebug() << "Template too small:" << templateHeight;
        return res;
    }

    int templateTop = result.height() - ignoreBottom - templateHeight;
    if (templateTop < 0) {
        qDebug() << "Template top out of bounds:" << templateTop;
        return res;
    }

    // Search region: top 70% of current frame (excluding header)
    int currentContentHeight = current.height() - ignoreTop - ignoreBottom;
    int searchHeight = currentContentHeight * 70 / 100;
    if (searchHeight < templateHeight) {
        qDebug() << "Search region too small:" << searchHeight << "< template" << templateHeight;
        return res;
    }

    // Crop to exclude side margins (scrollbars)
    int cropX = sideMargin;
    int cropWidth = result.width() - 2 * sideMargin;
    if (cropWidth < 50) {
        qDebug() << "Crop width too small:" << cropWidth;
        return res;
    }

    // Bounds check for cropping
    if (cropX + cropWidth > result.width() || cropX + cropWidth > current.width()) {
        qDebug() << "Crop exceeds image bounds";
        return res;
    }

    // Extract template from bottom of result
    QImage templateRgb = result.copy(cropX, templateTop, cropWidth, templateHeight)
                              .convertToFormat(QImage::Format_RGB32);
    if (templateRgb.isNull()) {
        qDebug() << "Failed to create template image";
        return res;
    }

    // Extract search region from top of current frame
    QImage searchRgb = current.copy(cropX, ignoreTop, cropWidth, searchHeight)
                             .convertToFormat(QImage::Format_RGB32);
    if (searchRgb.isNull()) {
        qDebug() << "Failed to create search image";
        return res;
    }

    // Convert QImage to cv::Mat
    cv::Mat templateMat(templateRgb.height(), templateRgb.width(), CV_8UC4,
                        const_cast<uchar*>(templateRgb.bits()),
                        static_cast<size_t>(templateRgb.bytesPerLine()));
    cv::Mat searchMat(searchRgb.height(), searchRgb.width(), CV_8UC4,
                      const_cast<uchar*>(searchRgb.bits()),
                      static_cast<size_t>(searchRgb.bytesPerLine()));

    // Convert to grayscale for faster matching
    cv::Mat templateGray, searchGray;
    cv::cvtColor(templateMat, templateGray, cv::COLOR_BGRA2GRAY);
    cv::cvtColor(searchMat, searchGray, cv::COLOR_BGRA2GRAY);

    // Template matching using normalized cross-correlation
    cv::Mat matchResult;
    cv::matchTemplate(searchGray, templateGray, matchResult, cv::TM_CCOEFF_NORMED);

    // Find best match
    double minVal, maxVal;
    cv::Point minLoc, maxLoc;
    cv::minMaxLoc(matchResult, &minVal, &maxVal, &minLoc, &maxLoc);

    res.confidence = maxVal;

    qDebug() << "Template match: templateH=" << templateHeight
             << "searchH=" << searchHeight
             << "confidence=" << maxVal
             << "matchY=" << maxLoc.y;

    // Confidence threshold - require high confidence for reliable stitching
    if (maxVal < 0.75) {
        qDebug() << "Template match confidence too low:" << maxVal;
        return res;
    }

    // Calculate overlap:
    // The template (bottom of result) was found at maxLoc.y in the search region.
    // This means the overlap starts at (ignoreTop + maxLoc.y) in the current frame.
    // New content starts after the overlap region: ignoreTop + maxLoc.y + templateHeight
    res.overlapLines = maxLoc.y + templateHeight;
    res.valid = true;

    qDebug() << "Template match successful: overlap=" << res.overlapLines
             << "confidence=" << res.confidence;

    return res;
}

void ImageCombiner::appendToResult(const QImage& current,
                                    int newContentStart, int ignoreBottomOffset)
{
    // Calculate height to append
    int appendHeight = current.height() - newContentStart - ignoreBottomOffset;

    if (appendHeight <= 0) return;  // No new content

    // Create new result image with extended height
    int newHeight = m_result.height() + appendHeight;
    QImage newResult(m_result.width(), newHeight, m_result.format());

    // Copy existing result
    QPainter painter(&newResult);
    painter.drawImage(0, 0, m_result);

    // Append new content from current frame
    // Source rect: from newContentStart row, excluding bottom footer
    QRect sourceRect(0, newContentStart, current.width(), appendHeight);
    QRect targetRect(0, m_result.height(), current.width(), appendHeight);
    painter.drawImage(targetRect, current, sourceRect);

    m_result = std::move(newResult);
}
