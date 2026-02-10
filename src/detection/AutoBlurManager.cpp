#include "detection/AutoBlurManager.h"
#include "detection/FaceDetector.h"
#include "settings/AutoBlurSettingsManager.h"

#include <QDebug>
#include <QPainter>
#include <QtGlobal>

#include <opencv2/imgproc.hpp>

AutoBlurManager::AutoBlurManager(QObject* parent)
    : QObject(parent)
    , m_faceDetector(std::make_unique<FaceDetector>())
{
    m_options = AutoBlurSettingsManager::instance().load();
}

AutoBlurManager::~AutoBlurManager() = default;

bool AutoBlurManager::initialize()
{
    if (m_initialized) {
        return true;
    }

    m_initialized = m_faceDetector->initialize();

    if (m_initialized) {
        qDebug() << "AutoBlurManager: Initialized";
    } else {
        qWarning() << "AutoBlurManager: Failed to initialize face detector";
    }

    return m_initialized;
}

bool AutoBlurManager::isInitialized() const
{
    return m_initialized;
}

AutoBlurManager::DetectionResult AutoBlurManager::detect(const QImage& image)
{
    DetectionResult result;

    if (!m_initialized) {
        result.errorMessage = "AutoBlurManager not initialized";
        return result;
    }

    if (image.isNull()) {
        result.errorMessage = "Invalid image";
        return result;
    }

    emit detectionStarted();
    emit detectionProgress(0);

    // Detect faces if enabled
    if (m_options.detectFaces && m_faceDetector->isInitialized()) {
        result.faceRegions = m_faceDetector->detect(image);
    }

    emit detectionProgress(100);
    result.success = true;
    emit detectionFinished(result);

    return result;
}

void AutoBlurManager::applyBlur(QImage& image, const QVector<QRect>& regions,
                                 int intensity, BlurType type)
{
    for (const QRect& region : regions) {
        // Clamp region to image bounds
        QRect clampedRegion = region.intersected(image.rect());
        if (clampedRegion.isEmpty()) {
            continue;
        }

        switch (type) {
        case BlurType::Gaussian:
            applyGaussianBlur(image, clampedRegion, intensity);
            break;
        case BlurType::Pixelate:
            applyPixelate(image, clampedRegion, intensity);
            break;
        }
    }
}

AutoBlurManager::DetectionResult AutoBlurManager::detectAndBlur(QImage& image)
{
    DetectionResult result = detect(image);

    if (!result.success) {
        return result;
    }

    if (!result.faceRegions.isEmpty()) {
        applyBlur(image, result.faceRegions, m_options.blurIntensity, m_options.blurType);
    }

    return result;
}

void AutoBlurManager::applyGaussianBlur(QImage& image, const QRect& region, int intensity)
{
    // Convert intensity (1-100) to sigma (1-50)
    double sigma = static_cast<double>(intensity) / 2.0;
    if (sigma < 1.0) {
        sigma = 1.0;
    }

    // Extract and convert region to RGB32 for OpenCV processing
    QImage regionImage = image.copy(region);
    QImage rgb = regionImage.convertToFormat(QImage::Format_RGB32);

    // Wrap QImage data in cv::Mat and apply blur in-place
    cv::Mat mat(rgb.height(), rgb.width(), CV_8UC4,
                const_cast<uchar*>(rgb.bits()),
                static_cast<size_t>(rgb.bytesPerLine()));
    cv::GaussianBlur(mat, mat, cv::Size(0, 0), sigma);

    // Paint blurred region back to original image
    QPainter painter(&image);
    painter.drawImage(region.topLeft(), rgb);
}

void AutoBlurManager::applyPixelate(QImage& image, const QRect& region, int intensity)
{
    // Convert intensity (1-100) to block size (2-32)
    int blockSize = 2 + (intensity * 30) / 100;

    // Extract and convert region to RGB32 for OpenCV processing
    QImage regionImage = image.copy(region);
    QImage rgb = regionImage.convertToFormat(QImage::Format_RGB32);

    // Wrap QImage data in cv::Mat
    cv::Mat mat(rgb.height(), rgb.width(), CV_8UC4,
                const_cast<uchar*>(rgb.bits()),
                static_cast<size_t>(rgb.bytesPerLine()));

    // Pixelate: downscale then upscale with nearest neighbor
    int smallWidth = std::max(1, mat.cols / blockSize);
    int smallHeight = std::max(1, mat.rows / blockSize);

    cv::Mat small;
    cv::resize(mat, small, cv::Size(smallWidth, smallHeight), 0, 0, cv::INTER_LINEAR);
    cv::resize(small, mat, cv::Size(rgb.width(), rgb.height()), 0, 0, cv::INTER_NEAREST);

    // Paint pixelated region back to original image
    QPainter painter(&image);
    painter.drawImage(region.topLeft(), rgb);
}

int AutoBlurManager::intensityToBlockSize(int intensity)
{
    // Clamp to valid range
    int clamped = qBound(1, intensity, 100);
    // Map intensity 1-100 -> blockSize 4-24
    // Higher intensity = larger blocks = stronger blur
    // Default intensity 50 -> blockSize 14 (close to legacy hardcoded 12)
    return 4 + (clamped * 20) / 100;
}
