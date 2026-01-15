#ifndef IMAGESTITCHER_H
#define IMAGESTITCHER_H

#include <QObject>
#include <QImage>
#include <QPoint>
#include <QSize>
#include <QString>
#include <vector>
#include <memory>

namespace cv {
    class Mat;
}

// Configuration for the RSSA stitching algorithm
struct StitchConfig {
    double confidenceThreshold = 0.85;    // Minimum acceptable match confidence
    bool detectStaticRegions = true;      // Enable header/footer detection

    // Algorithm flags
    bool usePhaseCorrelation = true;
    bool useWindowedDuplicateCheck = true;

    // Thresholds
    double ambiguityThreshold = 0.05;       // best - secondBest gap (lower = stricter)
    double duplicateThreshold = 0.95;       // matchVal above this = duplicate candidate
    double minPhaseResponse = 0.30;         // phase correlation response threshold
    int duplicateWindowSize = 200;          // min window size for duplicate check
    int phaseMaxCrossAxisShift = 8;         // max pixels off scroll axis
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

    enum class FailureCode {
        None,                   // Success
        FrameUnchanged,         // Frame identical to previous (normal, skip)
        ScrollTooSmall,         // Scroll delta too small (warning)
        OverlapTooSmall,        // Overlap region too small to match
        OverlapMismatch,        // Cannot find matching overlap
        AmbiguousMatch,         // Multiple similar match candidates (NEW)
        LowConfidence,          // Match confidence below threshold
        ViewportMismatch,       // Viewport/window size changed
        DuplicateDetected,      // Duplicate content detected
        MaxSizeReached,         // Maximum stitch dimensions reached
        InvalidState,           // Internal error (e.g., frame size mismatch)
        NoAlgorithmSucceeded,   // All algorithms failed
        Timeout,                // Reserved for future use (processing timeout)
        Unknown                 // Unclassified error
    };
    Q_ENUM(FailureCode)

    struct StitchResult {
        bool success = false;
        FailureCode failureCode = FailureCode::None;
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
    void setConfidenceThreshold(double threshold);
    double confidenceThreshold() const { return m_stitchConfig.confidenceThreshold; }

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

    /**
     * @brief Downsample a frame for fast comparison (64x64 RGB32)
     * Use with compareDownsampled() to avoid redundant downsampling
     */
    static QImage downsampleForComparison(const QImage &frame);

    /**
     * @brief Compare two already-downsampled images (64x64 expected)
     * @return true if frames are different enough to be considered changed
     */
    static bool compareDownsampled(const QImage &small1, const QImage &small2);

signals:
    void progressUpdated(int framesProcessed, const QSize &currentSize);

private:
    struct MatchCandidate {
        bool success = false;
        FailureCode failureCode = FailureCode::None;
        double confidence = 0.0;
        int overlap = 0;
        ScrollDirection direction = ScrollDirection::Down;
        QString failureReason;
    };

    // Algorithm implementations (RSSA: template matching and row projection only)
    StitchResult tryTemplateMatch(const QImage &newFrame);
    StitchResult tryRowProjectionMatch(const QImage &newFrame);
    StitchResult tryPhaseCorrelation(const QImage &newFrame);

    // Frame cache for avoiding redundant QImage→Mat conversions (PIMPL pattern)
    struct FrameCacheImpl;  // Forward declaration - defined in cpp file
    void prepareFrameCache(const QImage &frame, FrameCacheImpl &cache);
    void clearFrameCache(FrameCacheImpl &cache);

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

    QImage m_stitchedResult;
    QImage m_lastFrame;
    QRect m_currentViewportRect;
    std::unique_ptr<FrameCacheImpl> m_lastFrameCache;    // Cache for m_lastFrame
    std::unique_ptr<FrameCacheImpl> m_currentFrameCache; // Cache for current processing frame
    int m_frameCount = 0;
    int m_validHeight = 0;
    int m_validWidth = 0;  // For horizontal mode
    ScrollDirection m_lastSuccessfulDirection = ScrollDirection::Down;

    // ROI parameters
    static constexpr double ROI_FIRST_RATIO = 0.40;   // Larger symmetric coverage
    static constexpr double ROI_SECOND_RATIO = 0.50;  // More overlap detection range
    static constexpr int MIN_OVERLAP = 10;            // Allow smaller overlaps for fast scrolling
    static constexpr int MAX_OVERLAP = 600;           // Allow larger overlaps
    static constexpr int MAX_STITCHED_HEIGHT = 32768; // Maximum height in pixels (~32K)
    static constexpr int MAX_STITCHED_WIDTH = 32768;  // Maximum width in pixels (~32K)
    static constexpr double MAX_OVERLAP_RATIO = 0.90; // Max overlap as % of frame - prevents false matches
};

#endif // IMAGESTITCHER_H
