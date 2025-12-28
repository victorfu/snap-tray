#ifndef TEXTDETECTOR_H
#define TEXTDETECTOR_H

#include "IDetector.h"

/**
 * @brief Detects text regions using MSER algorithm.
 *
 * MSER (Maximally Stable Extremal Regions) is built into OpenCV's
 * features2d module and works well for detecting text-like regions
 * without requiring external models.
 */
class TextDetector : public IDetector
{
public:
    /**
     * @brief Configuration parameters for text detection.
     */
    struct Config {
        int delta = 5;                  ///< Delta for MSER stability
        int minArea = 60;               ///< Minimum region area
        int maxArea = 14400;            ///< Maximum region area
        double maxVariation = 0.25;     ///< Maximum variation threshold
        bool mergeOverlapping = true;   ///< Merge overlapping regions
        int mergePadding = 5;           ///< Padding when merging
    };

    TextDetector();
    ~TextDetector() override;

    // IDetector interface
    bool initialize() override;
    bool isInitialized() const override;
    QVector<QRect> detect(const QImage& image) override;

    /**
     * @brief Set detection configuration.
     */
    void setConfig(const Config& config);

    /**
     * @brief Get current configuration.
     */
    Config config() const;

private:
    bool m_initialized = false;
    Config m_config;

    /**
     * @brief Merge overlapping or adjacent rectangles.
     * @param rects Input rectangles
     * @param padding Padding to add when checking overlap
     * @return Merged rectangles
     */
    QVector<QRect> mergeRectangles(const QVector<QRect>& rects, int padding);

    /**
     * @brief Filter regions that look like text (aspect ratio, size).
     */
    bool isTextLikeRegion(const QRect& rect) const;
};

#endif // TEXTDETECTOR_H
