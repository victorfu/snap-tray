#ifndef FACEDETECTOR_H
#define FACEDETECTOR_H

#include <QImage>
#include <QRect>
#include <QVector>

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
class FaceDetector
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
    ~FaceDetector();

    bool initialize();
    bool isInitialized() const;
    QVector<QRect> detect(const QImage& image);

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
