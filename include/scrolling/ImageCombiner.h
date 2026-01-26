#ifndef IMAGECOMBINER_H
#define IMAGECOMBINER_H

#include <QImage>
#include "scrolling/ScrollingCaptureOptions.h"

/**
 * @brief Result of combining two images
 */
struct CombineResult {
    CaptureStatus status;   // Success/partial/failed
    int matchIndex;         // Position in result where match was found
    int matchCount;         // Number of matching lines
    int ignoreBottom;       // Rows ignored due to sticky footer
};

/**
 * @brief Image stitching algorithm for scrolling capture
 *
 * Uses memcmp-based line comparison (like ShareX) to find overlapping
 * regions between consecutive frames and stitch them together.
 *
 * Algorithm overview:
 * 1. From result image bottom, search upward for lines matching current frame top
 * 2. When match found, append non-overlapping portion of current frame
 * 3. Detect sticky footers by comparing bottom lines between frames
 * 4. Use fallback matching if no reliable match found
 */
class ImageCombiner
{
public:
    ImageCombiner();

    /**
     * @brief Configure matching options
     * @param minMatchLines Minimum lines for valid match (default: 10)
     * @param autoIgnoreBottom Enable sticky footer detection
     */
    void setOptions(int minMatchLines, bool autoIgnoreBottom);

    /**
     * @brief Reset combiner state for new capture session
     */
    void reset();

    /**
     * @brief Add first frame (no combining needed)
     * @param frame First captured frame
     */
    void setFirstFrame(const QImage& frame);

    /**
     * @brief Combine current frame into accumulated result
     *
     * Searches for overlap between result bottom and current top,
     * then appends non-overlapping portion.
     *
     * @param current New frame to combine
     * @return Combine result with match details
     */
    CombineResult addFrame(const QImage& current);

    /**
     * @brief Get the final combined image
     * @return Accumulated result image
     */
    QImage resultImage() const { return m_result; }

    /**
     * @brief Get total height of combined image
     */
    int totalHeight() const { return m_result.height(); }

    /**
     * @brief Get number of frames combined
     */
    int frameCount() const { return m_frameCount; }

private:
    /**
     * @brief Compare two scan lines using memcmp
     *
     * @param img1 First image
     * @param y1 Line index in first image
     * @param img2 Second image
     * @param y2 Line index in second image
     * @param xOffset Horizontal offset (side margin)
     * @param width Width to compare
     * @return true if lines match exactly
     */
    bool compareLines(const QImage& img1, int y1,
                      const QImage& img2, int y2,
                      int xOffset, int width) const;

    /**
     * @brief Detect sticky footer by comparing bottom lines
     *
     * Sticky footers are fixed elements at the bottom that don't scroll.
     * They appear identical in consecutive frames.
     *
     * @param result Accumulated result image
     * @param resultMatchY Match position in result
     * @param current Current frame
     * @param sideMargin Side margin for comparison
     * @param compareWidth Width to compare
     * @return Number of rows to ignore at bottom
     */
    int detectBottomIgnoreOffset(const QImage& result, int resultMatchY,
                                 const QImage& current,
                                 int sideMargin, int compareWidth) const;

    /**
     * @brief Detect sticky header by comparing top lines
     *
     * Sticky headers are fixed elements at the top that don't scroll.
     * They appear identical in consecutive frames.
     *
     * @param result Accumulated result image
     * @param current Current frame
     * @param sideMargin Side margin for comparison
     * @param compareWidth Width to compare
     * @return Number of rows to ignore at top
     */
    int detectTopIgnoreOffset(const QImage& result,
                              const QImage& current,
                              int sideMargin, int compareWidth) const;

    /**
     * @brief Append new content from current frame to result
     *
     * @param current Current frame
     * @param newContentStart Row in current where new content starts
     * @param ignoreBottomOffset Rows to ignore at bottom (sticky footer)
     */
    void appendToResult(const QImage& current,
                        int newContentStart, int ignoreBottomOffset);

    // Settings
    int m_minMatchLines = 10;
    bool m_autoIgnoreBottom = true;

    // Fallback state (for PartiallySuccessful recovery)
    int m_lastMatchCount = 0;
    int m_lastMatchIndex = -1;
    int m_lastIgnoreBottomOffset = 0;
    int m_lastIgnoreTopOffset = 0;

    // Result tracking
    QImage m_result;
    int m_frameCount = 0;
};

#endif // IMAGECOMBINER_H
