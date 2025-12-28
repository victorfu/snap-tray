#ifndef FACEDETECTOR_H
#define FACEDETECTOR_H

#include "IDetector.h"
#include <memory>

namespace cv {
class CascadeClassifier;
}

/**
 * @brief Detects faces in images using Haar Cascades.
 *
 * Uses OpenCV's CascadeClassifier with the pre-trained
 * haarcascade_frontalface_default.xml model for offline face detection.
 */
class FaceDetector : public IDetector
{
public:
    /**
     * @brief Configuration parameters for face detection.
     */
    struct Config {
        double scaleFactor = 1.1;     ///< Image scale factor for multi-scale detection
        int minNeighbors = 5;         ///< Higher values = fewer false positives
        int minFaceSize = 30;         ///< Minimum face size in pixels
        int maxFaceSize = 0;          ///< Maximum face size (0 = unlimited)
    };

    FaceDetector();
    ~FaceDetector() override;

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
    std::unique_ptr<cv::CascadeClassifier> m_classifier;
    bool m_initialized = false;
    Config m_config;
};

#endif // FACEDETECTOR_H
