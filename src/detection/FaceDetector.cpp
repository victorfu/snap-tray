#include "detection/FaceDetector.h"

#include <QDebug>
#include <QFile>
#include <QTemporaryFile>

#include <opencv2/objdetect.hpp>
#include <opencv2/imgproc.hpp>

FaceDetector::FaceDetector()
    : m_classifier(std::make_unique<cv::CascadeClassifier>())
{
}

FaceDetector::~FaceDetector() = default;

bool FaceDetector::initialize()
{
    if (m_initialized) {
        return true;
    }

    // Load cascade from Qt resources
    QString resourcePath = ":/cascades/cascades/haarcascade_frontalface_default.xml";
    QFile resourceFile(resourcePath);

    if (!resourceFile.open(QIODevice::ReadOnly)) {
        qWarning() << "FaceDetector: Failed to open cascade resource:" << resourcePath;
        return false;
    }

    // OpenCV needs a file path, so we extract to a temp file
    QTemporaryFile tempFile;
    tempFile.setAutoRemove(false);  // Keep until we load it

    if (!tempFile.open()) {
        qWarning() << "FaceDetector: Failed to create temp file for cascade";
        return false;
    }

    tempFile.write(resourceFile.readAll());
    tempFile.flush();
    QString tempPath = tempFile.fileName();
    tempFile.close();

    // Load the cascade classifier
    if (!m_classifier->load(tempPath.toStdString())) {
        qWarning() << "FaceDetector: Failed to load cascade classifier from:" << tempPath;
        QFile::remove(tempPath);
        return false;
    }

    // Clean up temp file
    QFile::remove(tempPath);

    m_initialized = true;
    qDebug() << "FaceDetector: Initialized successfully";
    return true;
}

bool FaceDetector::isInitialized() const
{
    return m_initialized;
}

QVector<QRect> FaceDetector::detect(const QImage& image)
{
    QVector<QRect> results;

    if (!m_initialized) {
        qWarning() << "FaceDetector: Not initialized";
        return results;
    }

    if (image.isNull()) {
        return results;
    }

    // Convert QImage to cv::Mat (grayscale for cascade)
    QImage rgb = image.convertToFormat(QImage::Format_RGB32);

    cv::Mat mat(rgb.height(), rgb.width(), CV_8UC4,
                const_cast<uchar*>(rgb.bits()),
                static_cast<size_t>(rgb.bytesPerLine()));

    cv::Mat gray;
    cv::cvtColor(mat, gray, cv::COLOR_RGBA2GRAY);

    // Enhance contrast for better detection
    cv::equalizeHist(gray, gray);

    // Detect faces
    std::vector<cv::Rect> faces;
    cv::Size minSize(m_config.minFaceSize, m_config.minFaceSize);
    cv::Size maxSize = m_config.maxFaceSize > 0
                           ? cv::Size(m_config.maxFaceSize, m_config.maxFaceSize)
                           : cv::Size();

    m_classifier->detectMultiScale(
        gray,
        faces,
        m_config.scaleFactor,
        m_config.minNeighbors,
        0,  // flags (deprecated)
        minSize,
        maxSize
        );

    // Convert cv::Rect to QRect
    for (const auto& face : faces) {
        results.append(QRect(face.x, face.y, face.width, face.height));
    }

    qDebug() << "FaceDetector: Detected" << results.size() << "faces";
    return results;
}

void FaceDetector::setConfig(const Config& config)
{
    m_config = config;
}

FaceDetector::Config FaceDetector::config() const
{
    return m_config;
}
