#include "detection/TextDetector.h"

#include <QDebug>
#include <algorithm>

#include <opencv2/imgproc.hpp>
#include <opencv2/features2d.hpp>

TextDetector::TextDetector() = default;

TextDetector::~TextDetector() = default;

bool TextDetector::initialize()
{
    // MSER doesn't need initialization, it's created on-demand
    m_initialized = true;
    qDebug() << "TextDetector: Initialized successfully";
    return true;
}

bool TextDetector::isInitialized() const
{
    return m_initialized;
}

QVector<QRect> TextDetector::detect(const QImage& image)
{
    QVector<QRect> results;

    if (!m_initialized) {
        qWarning() << "TextDetector: Not initialized";
        return results;
    }

    if (image.isNull()) {
        return results;
    }

    // Convert QImage to cv::Mat
    QImage rgb = image.convertToFormat(QImage::Format_RGB32);

    cv::Mat mat(rgb.height(), rgb.width(), CV_8UC4,
                const_cast<uchar*>(rgb.bits()),
                static_cast<size_t>(rgb.bytesPerLine()));

    cv::Mat gray;
    cv::cvtColor(mat, gray, cv::COLOR_RGBA2GRAY);

    // Create MSER detector
    cv::Ptr<cv::MSER> mser = cv::MSER::create(
        m_config.delta,
        m_config.minArea,
        m_config.maxArea,
        m_config.maxVariation
        );

    // Detect MSER regions
    std::vector<std::vector<cv::Point>> regions;
    std::vector<cv::Rect> boundingBoxes;
    mser->detectRegions(gray, regions, boundingBoxes);

    // Convert and filter regions
    QVector<QRect> candidateRects;
    for (const auto& box : boundingBoxes) {
        QRect rect(box.x, box.y, box.width, box.height);
        if (isTextLikeRegion(rect)) {
            candidateRects.append(rect);
        }
    }

    // Merge overlapping regions if enabled
    if (m_config.mergeOverlapping && !candidateRects.isEmpty()) {
        results = mergeRectangles(candidateRects, m_config.mergePadding);
    } else {
        results = candidateRects;
    }

    qDebug() << "TextDetector: Detected" << results.size() << "text regions"
             << "(from" << boundingBoxes.size() << "MSER regions)";
    return results;
}

bool TextDetector::isTextLikeRegion(const QRect& rect) const
{
    // Filter out regions that don't look like text
    // Text typically has certain aspect ratio and size characteristics

    if (rect.width() < 5 || rect.height() < 5) {
        return false;  // Too small
    }

    double aspectRatio = static_cast<double>(rect.width()) / rect.height();

    // Text characters typically have aspect ratio between 0.1 and 10
    if (aspectRatio < 0.1 || aspectRatio > 10.0) {
        return false;
    }

    // Filter very large regions (likely not text)
    if (rect.width() > 500 && rect.height() > 500) {
        return false;
    }

    return true;
}

QVector<QRect> TextDetector::mergeRectangles(const QVector<QRect>& rects, int padding)
{
    if (rects.isEmpty()) {
        return {};
    }

    // Sort rectangles by x position
    QVector<QRect> sorted = rects;
    std::sort(sorted.begin(), sorted.end(), [](const QRect& a, const QRect& b) {
        return a.x() < b.x();
    });

    QVector<QRect> merged;
    QRect current = sorted.first().adjusted(-padding, -padding, padding, padding);

    for (int i = 1; i < sorted.size(); ++i) {
        QRect next = sorted[i].adjusted(-padding, -padding, padding, padding);

        if (current.intersects(next) || current.adjusted(0, 0, padding * 2, 0).intersects(next)) {
            // Merge overlapping or adjacent rectangles
            current = current.united(next);
        } else {
            // Save current and start new group
            merged.append(current.adjusted(padding, padding, -padding, -padding));
            current = next;
        }
    }

    // Don't forget the last rectangle
    merged.append(current.adjusted(padding, padding, -padding, -padding));

    // Second pass: merge vertically adjacent regions
    if (merged.size() > 1) {
        std::sort(merged.begin(), merged.end(), [](const QRect& a, const QRect& b) {
            return a.y() < b.y();
        });

        QVector<QRect> verticalMerged;
        current = merged.first().adjusted(-padding, -padding, padding, padding);

        for (int i = 1; i < merged.size(); ++i) {
            QRect next = merged[i].adjusted(-padding, -padding, padding, padding);

            if (current.intersects(next)) {
                current = current.united(next);
            } else {
                verticalMerged.append(current.adjusted(padding, padding, -padding, -padding));
                current = next;
            }
        }
        verticalMerged.append(current.adjusted(padding, padding, -padding, -padding));

        return verticalMerged;
    }

    return merged;
}

void TextDetector::setConfig(const Config& config)
{
    m_config = config;
}

TextDetector::Config TextDetector::config() const
{
    return m_config;
}
