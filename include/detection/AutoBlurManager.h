#ifndef AUTOBLURMANAGER_H
#define AUTOBLURMANAGER_H

#include "settings/AutoBlurSettingsManager.h"

#include <QImage>
#include <QObject>
#include <QRect>
#include <QVector>
#include <memory>

class FaceDetector;

/**
 * @brief Manages auto-detection and blurring of sensitive content.
 *
 * Orchestrates face detection and applies blur effects.
 * Settings are managed via AutoBlurSettingsManager.
 */
class AutoBlurManager : public QObject
{
    Q_OBJECT

public:
    using BlurType = AutoBlurSettingsManager::BlurType;
    using Options = AutoBlurSettingsManager::Options;

    /**
     * @brief Detection result.
     */
    struct DetectionResult {
        QVector<QRect> faceRegions;
        bool success = false;
        QString errorMessage;
    };

    explicit AutoBlurManager(QObject* parent = nullptr);
    ~AutoBlurManager() override;

    /**
     * @brief Initialize face detector.
     * @return true if detector initialized successfully
     */
    bool initialize();

    /**
     * @brief Check if manager is ready for detection.
     */
    bool isInitialized() const;

    /**
     * @brief Detect faces in the image.
     * @param image Image to analyze
     * @return Detection result with regions
     */
    DetectionResult detect(const QImage& image);

    /**
     * @brief Apply blur to specified regions of an image.
     * @param image Image to modify (will be modified in-place)
     * @param regions Regions to blur
     * @param intensity Blur intensity (1-100)
     * @param type Blur type (Gaussian or Pixelate)
     */
    void applyBlur(QImage& image, const QVector<QRect>& regions,
                   int intensity, BlurType type);

    /**
     * @brief Detect and blur in one step.
     * @param image Image to process (will be modified in-place)
     * @return Detection result
     */
    DetectionResult detectAndBlur(QImage& image);

    /**
     * @brief Get current options.
     */
    Options options() const { return m_options; }

    /**
     * @brief Set options.
     */
    void setOptions(const Options& options) { m_options = options; }

    /**
     * @brief Convert blur intensity (1-100) to mosaic block size.
     *
     * Maps the abstract intensity setting to a concrete pixel block size
     * for MosaicRectAnnotation. Both RegionSelector and PinWindow should
     * use this to ensure consistent auto-blur appearance.
     *
     * @param intensity Blur intensity from AutoBlurSettingsManager (1-100)
     * @return Block size in pixels (4-24)
     */
    static int intensityToBlockSize(int intensity);

signals:
    void detectionStarted();
    void detectionProgress(int percent);
    void detectionFinished(const DetectionResult& result);

private:
    std::unique_ptr<FaceDetector> m_faceDetector;
    bool m_initialized = false;
    Options m_options;

    /**
     * @brief Apply Gaussian blur to a region.
     */
    void applyGaussianBlur(QImage& image, const QRect& region, int intensity);

    /**
     * @brief Apply pixelation effect to a region.
     */
    void applyPixelate(QImage& image, const QRect& region, int intensity);
};

#endif // AUTOBLURMANAGER_H
