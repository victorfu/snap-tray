#include "detection/AutoBlurManager.h"
#include "detection/FaceDetector.h"
#include "detection/TextDetector.h"
#include "settings/Settings.h"

#include <QDebug>
#include <QPainter>
#include <QSettings>

#include <opencv2/imgproc.hpp>

// QSettings keys
static const char* kSettingsGroup = "detection";
static const char* kAutoBlurEnabled = "autoBlurEnabled";
static const char* kDetectFaces = "detectFaces";
static const char* kDetectText = "detectText";
static const char* kBlurIntensity = "blurIntensity";
static const char* kBlurType = "blurType";

AutoBlurManager::AutoBlurManager(QObject* parent)
    : QObject(parent)
    , m_faceDetector(std::make_unique<FaceDetector>())
    , m_textDetector(std::make_unique<TextDetector>())
{
    m_options = loadSettings();
}

AutoBlurManager::~AutoBlurManager() = default;

bool AutoBlurManager::initialize()
{
    if (m_initialized) {
        return true;
    }

    bool faceOk = m_faceDetector->initialize();
    bool textOk = m_textDetector->initialize();

    m_initialized = faceOk || textOk;

    if (m_initialized) {
        qDebug() << "AutoBlurManager: Initialized (face:" << faceOk << ", text:" << textOk << ")";
    } else {
        qWarning() << "AutoBlurManager: Failed to initialize any detector";
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
        emit detectionProgress(50);
    }

    // Detect text if enabled
    if (m_options.detectText && m_textDetector->isInitialized()) {
        result.textRegions = m_textDetector->detect(image);
        emit detectionProgress(100);
    }

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

    // Combine all regions
    QVector<QRect> allRegions;
    allRegions.append(result.faceRegions);
    allRegions.append(result.textRegions);

    if (!allRegions.isEmpty()) {
        applyBlur(image, allRegions, m_options.blurIntensity, m_options.blurType);
    }

    return result;
}

void AutoBlurManager::applyGaussianBlur(QImage& image, const QRect& region, int intensity)
{
    // Convert intensity (1-100) to sigma (1-50)
    double sigma = static_cast<double>(intensity) / 2.0;
    if (sigma < 1.0) sigma = 1.0;

    // Extract the region
    QImage regionImage = image.copy(region);
    QImage rgb = regionImage.convertToFormat(QImage::Format_RGB32);

    cv::Mat mat(rgb.height(), rgb.width(), CV_8UC4,
                const_cast<uchar*>(rgb.bits()),
                static_cast<size_t>(rgb.bytesPerLine()));

    cv::Mat bgr;
    cv::cvtColor(mat, bgr, cv::COLOR_RGBA2BGR);

    // Apply Gaussian blur
    cv::GaussianBlur(bgr, bgr, cv::Size(0, 0), sigma);

    // Convert back to QImage
    cv::Mat rgba;
    cv::cvtColor(bgr, rgba, cv::COLOR_BGR2RGBA);

    QImage blurred(rgba.data, rgba.cols, rgba.rows,
                   static_cast<int>(rgba.step), QImage::Format_RGBA8888);
    blurred = blurred.copy();  // Deep copy

    // Paint back to original image
    QPainter painter(&image);
    painter.drawImage(region.topLeft(), blurred);
}

void AutoBlurManager::applyPixelate(QImage& image, const QRect& region, int intensity)
{
    // Convert intensity (1-100) to block size (2-32)
    int blockSize = 2 + (intensity * 30) / 100;

    // Extract the region
    QImage regionImage = image.copy(region);
    QImage rgb = regionImage.convertToFormat(QImage::Format_RGB32);

    cv::Mat mat(rgb.height(), rgb.width(), CV_8UC4,
                const_cast<uchar*>(rgb.bits()),
                static_cast<size_t>(rgb.bytesPerLine()));

    cv::Mat bgr;
    cv::cvtColor(mat, bgr, cv::COLOR_RGBA2BGR);

    // Pixelate: downscale then upscale with nearest neighbor
    int smallWidth = std::max(1, bgr.cols / blockSize);
    int smallHeight = std::max(1, bgr.rows / blockSize);

    cv::Mat small;
    cv::resize(bgr, small, cv::Size(smallWidth, smallHeight), 0, 0, cv::INTER_LINEAR);
    cv::resize(small, bgr, cv::Size(rgb.width(), rgb.height()), 0, 0, cv::INTER_NEAREST);

    // Convert back to QImage
    cv::Mat rgba;
    cv::cvtColor(bgr, rgba, cv::COLOR_BGR2RGBA);

    QImage pixelated(rgba.data, rgba.cols, rgba.rows,
                     static_cast<int>(rgba.step), QImage::Format_RGBA8888);
    pixelated = pixelated.copy();  // Deep copy

    // Paint back to original image
    QPainter painter(&image);
    painter.drawImage(region.topLeft(), pixelated);
}

AutoBlurManager::Options AutoBlurManager::loadSettings()
{
    Options options;

    QSettings settings = SnapTray::getSettings();
    settings.beginGroup(kSettingsGroup);

    options.enabled = settings.value(kAutoBlurEnabled, true).toBool();
    options.detectFaces = settings.value(kDetectFaces, true).toBool();
    options.detectText = settings.value(kDetectText, true).toBool();
    options.blurIntensity = settings.value(kBlurIntensity, 50).toInt();

    QString blurTypeStr = settings.value(kBlurType, "pixelate").toString();
    options.blurType = (blurTypeStr == "gaussian")
                           ? BlurType::Gaussian
                           : BlurType::Pixelate;

    settings.endGroup();

    return options;
}

void AutoBlurManager::saveSettings(const Options& options)
{
    QSettings settings = SnapTray::getSettings();
    settings.beginGroup(kSettingsGroup);

    settings.setValue(kAutoBlurEnabled, options.enabled);
    settings.setValue(kDetectFaces, options.detectFaces);
    settings.setValue(kDetectText, options.detectText);
    settings.setValue(kBlurIntensity, options.blurIntensity);
    settings.setValue(kBlurType, options.blurType == BlurType::Gaussian ? "gaussian" : "pixelate");

    settings.endGroup();
}
