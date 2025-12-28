#ifndef AUTOBLURMANAGER_H
#define AUTOBLURMANAGER_H

#include <QImage>
#include <QObject>
#include <QRect>
#include <QVector>
#include <memory>

class FaceDetector;
class TextDetector;

/**
 * @brief Manages auto-detection and blurring of sensitive content.
 *
 * Orchestrates face and text detection, applies blur effects,
 * and manages settings persistence via QSettings.
 */
class AutoBlurManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Blur effect type.
     */
    enum class BlurType {
        Gaussian,
        Pixelate
    };

    /**
     * @brief Detection options loaded from settings.
     */
    struct Options {
        bool enabled = true;
        bool detectFaces = true;
        bool detectText = true;
        int blurIntensity = 50;         ///< 1-100
        BlurType blurType = BlurType::Pixelate;
    };

    /**
     * @brief Detection result.
     */
    struct DetectionResult {
        QVector<QRect> faceRegions;
        QVector<QRect> textRegions;
        bool success = false;
        QString errorMessage;
    };

    explicit AutoBlurManager(QObject* parent = nullptr);
    ~AutoBlurManager() override;

    /**
     * @brief Initialize both detectors.
     * @return true if at least one detector initialized successfully
     */
    bool initialize();

    /**
     * @brief Check if manager is ready for detection.
     */
    bool isInitialized() const;

    /**
     * @brief Detect faces and/or text in the image.
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

    // Settings management
    static Options loadSettings();
    static void saveSettings(const Options& options);

    /**
     * @brief Get current options.
     */
    Options options() const { return m_options; }

    /**
     * @brief Set options.
     */
    void setOptions(const Options& options) { m_options = options; }

signals:
    void detectionStarted();
    void detectionProgress(int percent);
    void detectionFinished(const DetectionResult& result);

private:
    std::unique_ptr<FaceDetector> m_faceDetector;
    std::unique_ptr<TextDetector> m_textDetector;
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
