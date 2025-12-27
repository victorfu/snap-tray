#ifndef FIXEDELEMENTDETECTOR_H
#define FIXEDELEMENTDETECTOR_H

#include <QObject>
#include <QImage>
#include <QRect>
#include <vector>

class FixedElementDetector : public QObject
{
    Q_OBJECT

public:
    struct DetectionResult {
        bool detected = false;
        int leadingFixed = 0;   // top (header)
        int trailingFixed = 0;  // bottom (footer)
        double confidence = 0.0;
    };

    explicit FixedElementDetector(QObject *parent = nullptr);
    ~FixedElementDetector();

    // Configuration
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    // Reset for new capture session
    void reset();

    // Add a frame for analysis
    void addFrame(const QImage &frame);

    // Get detection results after sufficient frames
    DetectionResult detect();

    // Check if detection has been performed
    bool hasDetectedElements() const;

    // Get crop values
    int leadingCropSize() const { return m_leadingCropSize; }
    int trailingCropSize() const { return m_trailingCropSize; }

    // Crop fixed regions from a frame before stitching
    QImage cropFixedRegions(const QImage &frame) const;

private:
    double calculateRegionSimilarity(const QImage &region1, const QImage &region2) const;
    bool compareRows(const QImage &img1, const QImage &img2, int y) const;
    void analyzeLeadingRegion();
    void analyzeTrailingRegion();

    bool m_enabled = true;
    bool m_detected = false;
    int m_frameCount = 0;

    std::vector<QImage> m_leadingRegions;
    std::vector<QImage> m_trailingRegions;

    int m_leadingCropSize = 0;
    int m_trailingCropSize = 0;

    static constexpr int ANALYSIS_REGION_SIZE = 100;   // Height/width to analyze
    static constexpr double SIMILARITY_THRESHOLD = 0.95;
    static constexpr int MIN_FRAMES_FOR_DETECTION = 3;
    static constexpr int MAX_FRAMES_TO_STORE = 5;
};

#endif // FIXEDELEMENTDETECTOR_H
