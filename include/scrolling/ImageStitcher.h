#ifndef IMAGESTITCHER_H
#define IMAGESTITCHER_H

#include <QObject>
#include <QImage>
#include <QPoint>
#include <QSize>
#include <QString>
#include <vector>

namespace cv {
    class Mat;
}

// Static regions detected in consecutive frames (sticky headers, footers, scrollbars)
struct StaticRegions {
    int headerHeight = 0;      // Pixels from top that are static (sticky header)
    int footerHeight = 0;      // Pixels from bottom that are static (sticky footer)
    int scrollbarWidth = 0;    // Pixels from right edge (scrollbar track)
    bool detected = false;
};

// Configuration for the RSSA stitching algorithm
struct StitchConfig {
    int templateHeight = 80;              // Rows to use as template (bottom of img1)
    int searchHeight = 400;               // Search region height (top of img2)
    double confidenceThreshold = 0.85;    // Minimum acceptable match confidence
    int minOverlap = 20;                  // Minimum overlap to consider valid
    int maxOverlap = 500;                 // Maximum expected overlap
    double staticRowThreshold = 8.0;      // Max avg pixel diff to consider row static
    bool detectStaticRegions = true;      // Enable header/footer detection
    bool useGrayscale = true;             // Convert to grayscale before matching
};

class ImageStitcher : public QObject
{
    Q_OBJECT

public:
    // RSSA: Template matching and row projection only (no feature-based algorithms)
    enum class Algorithm {
        TemplateMatching,  // Default - uses cv::TM_CCOEFF_NORMED
        RowProjection,     // Fast 1D cross-correlation for text documents
        Auto               // Try RowProjection first, fallback to TemplateMatching
    };
    Q_ENUM(Algorithm)

    enum class ScrollDirection {
        Down,
        Up,
        Left,
        Right
    };
    Q_ENUM(ScrollDirection)

    enum class CaptureMode {
        Vertical,
        Horizontal
    };
    Q_ENUM(CaptureMode)

    struct StitchResult {
        bool success = false;
        QPoint offset;              // x for horizontal, y for vertical
        double confidence = 0.0;    // 0.0 - 1.0
        int overlapPixels = 0;      // Detected overlap amount
        Algorithm usedAlgorithm = Algorithm::TemplateMatching;
        ScrollDirection direction = ScrollDirection::Down;
        QString failureReason;
    };

    explicit ImageStitcher(QObject *parent = nullptr);
    ~ImageStitcher();

    // Configuration
    void setAlgorithm(Algorithm algo);
    Algorithm algorithm() const { return m_algorithm; }

    void setCaptureMode(CaptureMode mode);
    CaptureMode captureMode() const { return m_captureMode; }

    void setStitchConfig(const StitchConfig &config);
    StitchConfig stitchConfig() const { return m_stitchConfig; }

    // Legacy API (wraps StitchConfig)
    void setDetectFixedElements(bool enabled);
    bool detectFixedElements() const { return m_stitchConfig.detectStaticRegions; }

    void setConfidenceThreshold(double threshold);
    double confidenceThreshold() const { return m_stitchConfig.confidenceThreshold; }

    // Static region detection (header/footer/scrollbar)
    StaticRegions detectedStaticRegions() const { return m_staticRegions; }

    // Core operations
    StitchResult addFrame(const QImage &frame);
    void reset();

    // Getters
    QImage getStitchedImage() const;
    QSize getCurrentSize() const;
    int frameCount() const { return m_frameCount; }
    QRect currentViewportRect() const { return m_currentViewportRect; }

    // Helper methods
    static bool isFrameChanged(const QImage &frame1, const QImage &frame2);

signals:
    void progressUpdated(int framesProcessed, const QSize &currentSize);

private:
    struct MatchCandidate {
        bool success = false;
        double confidence = 0.0;
        int overlap = 0;
        ScrollDirection direction = ScrollDirection::Down;
        QString failureReason;
    };

    // Algorithm implementations (RSSA: template matching and row projection only)
    StitchResult tryTemplateMatch(const QImage &newFrame);
    StitchResult tryRowProjectionMatch(const QImage &newFrame);
    StitchResult tryInPlaceMatchInStitched(const QImage &newFrame);

    // Static region detection (header/footer/scrollbar)
    StaticRegions detectStaticRegions(const QImage &img1, const QImage &img2);
    double calculateRowDifference(const cv::Mat &row1, const cv::Mat &row2) const;

    // Helper methods
    cv::Mat qImageToCvMat(const QImage &image) const;
    QImage cvMatToQImage(const cv::Mat &mat) const;
    MatchCandidate computeTemplateMatchCandidate(const QImage &newFrame, ScrollDirection direction);
    MatchCandidate computeRowProjectionCandidate(const QImage &newFrame, ScrollDirection direction);
    StitchResult applyCandidate(const QImage &newFrame, const MatchCandidate &candidate, Algorithm algorithm);
    StitchResult performStitch(const QImage &newFrame, int overlapPixels, ScrollDirection direction);
    bool wouldCreateDuplicate(const QImage &newFrame, int overlapPixels, ScrollDirection direction) const;

    Algorithm m_algorithm = Algorithm::Auto;
    CaptureMode m_captureMode = CaptureMode::Vertical;
    StitchConfig m_stitchConfig;
    StaticRegions m_staticRegions;

    QImage m_stitchedResult;
    QImage m_lastFrame;
    QRect m_currentViewportRect;
    int m_frameCount = 0;
    int m_validHeight = 0;
    int m_validWidth = 0;  // For horizontal mode
    ScrollDirection m_lastSuccessfulDirection = ScrollDirection::Down;

    // ROI parameters
    static constexpr double ROI_FIRST_RATIO = 0.40;   // Larger symmetric coverage
    static constexpr double ROI_SECOND_RATIO = 0.50;  // More overlap detection range
    static constexpr int MIN_OVERLAP = 15;            // Allow smaller overlaps
    static constexpr int MAX_OVERLAP = 600;           // Allow larger overlaps
    static constexpr int MAX_STITCHED_HEIGHT = 32768; // Maximum height in pixels (~32K)
    static constexpr int MAX_STITCHED_WIDTH = 32768;  // Maximum width in pixels (~32K)
    static constexpr double MAX_OVERLAP_RATIO = 0.90; // Max overlap as % of frame - prevents false matches
};

#endif // IMAGESTITCHER_H
