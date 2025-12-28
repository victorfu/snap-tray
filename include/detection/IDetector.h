#ifndef IDETECTOR_H
#define IDETECTOR_H

#include <QImage>
#include <QRect>
#include <QVector>

/**
 * @brief Interface for detection algorithms.
 *
 * Base interface for face and text detectors that identify
 * regions of interest in images for auto-blur functionality.
 */
class IDetector
{
public:
    virtual ~IDetector() = default;

    /**
     * @brief Initialize the detector.
     * @return true if initialization succeeded
     */
    virtual bool initialize() = 0;

    /**
     * @brief Check if the detector is initialized and ready.
     * @return true if ready for detection
     */
    virtual bool isInitialized() const = 0;

    /**
     * @brief Detect regions of interest in the given image.
     * @param image Input image to analyze
     * @return Vector of rectangles containing detected regions
     */
    virtual QVector<QRect> detect(const QImage& image) = 0;
};

#endif // IDETECTOR_H
