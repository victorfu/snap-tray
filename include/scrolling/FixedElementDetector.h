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
    enum class CaptureMode {
        Vertical,   // Detect header/footer (top/bottom)
        Horizontal  // Detect left/right sidebars
    };
    Q_ENUM(CaptureMode)

    struct DetectionResult {
        bool detected = false;
        int leadingFixed = 0;   // top (header) or left (sidebar)
        int trailingFixed = 0;  // bottom (footer) or right (sidebar)
        double confidence = 0.0;
    };

    explicit FixedElementDetector(QObject *parent = nullptr);
    ~FixedElementDetector();

    // Configuration
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    void setCaptureMode(CaptureMode mode);
    CaptureMode captureMode() const { return m_captureMode; }

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
    bool compareRows(const QImage &img1, const QImage &img2, int y) const;
    bool compareColumns(const QImage &img1, const QImage &img2, int x) const;
    void analyzeLeadingRegion();
    void analyzeTrailingRegion();

    bool m_enabled = true;
    CaptureMode m_captureMode = CaptureMode::Vertical;
    bool m_detected = false;
    int m_frameCount = 0;
    int m_regionWriteIndex = 0;  // Circular buffer write index

    // Circular buffer for regions (more efficient than vector erase)
    std::vector<QImage> m_leadingRegions;
    std::vector<QImage> m_trailingRegions;

    int m_leadingCropSize = 0;
    int m_trailingCropSize = 0;

    static constexpr int ANALYSIS_REGION_SIZE = 100;   // Height/width to analyze
    static constexpr int MIN_ANALYSIS_REGION_SIZE = 20; // Minimum region size
    static constexpr double SIMILARITY_THRESHOLD = 0.95;
    static constexpr int MIN_FRAMES_FOR_DETECTION = 4;  // Increased for more reliable detection
    static constexpr int MAX_FRAMES_TO_STORE = 5;
};

#endif // FIXEDELEMENTDETECTOR_H
