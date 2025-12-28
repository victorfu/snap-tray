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
    class KeyPoint;
}

class ImageStitcher : public QObject
{
    Q_OBJECT

public:
    enum class Algorithm {
        ORB,
        SIFT,
        TemplateMatching,
        RowProjection,  // Recommended for text documents - uses 1D cross-correlation
        Auto
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
        int matchedFeatures = 0;
        Algorithm usedAlgorithm = Algorithm::ORB;
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

    void setDetectFixedElements(bool enabled);
    bool detectFixedElements() const { return m_detectFixedElements; }

    void setConfidenceThreshold(double threshold);
    double confidenceThreshold() const { return m_confidenceThreshold; }

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
        int matchedFeatures = 0;
        int overlap = 0;
        ScrollDirection direction = ScrollDirection::Down;
        QString failureReason;
    };

    // Algorithm implementations
    StitchResult tryORBMatch(const QImage &newFrame);
    StitchResult trySIFTMatch(const QImage &newFrame);
    StitchResult tryTemplateMatch(const QImage &newFrame);
    StitchResult tryRowProjectionMatch(const QImage &newFrame);
    StitchResult tryInPlaceMatchInStitched(const QImage &newFrame);

    // Helper methods
    cv::Mat qImageToCvMat(const QImage &image) const;
    QImage cvMatToQImage(const cv::Mat &mat) const;
    MatchCandidate computeORBMatchCandidate(const QImage &newFrame, ScrollDirection direction);
    MatchCandidate computeTemplateMatchCandidate(const QImage &newFrame, ScrollDirection direction);
    MatchCandidate computeRowProjectionCandidate(const QImage &newFrame, ScrollDirection direction);
    StitchResult applyCandidate(const QImage &newFrame, const MatchCandidate &candidate, Algorithm algorithm);
    StitchResult performStitch(const QImage &newFrame, int overlapPixels, ScrollDirection direction);

    Algorithm m_algorithm = Algorithm::Auto;
    CaptureMode m_captureMode = CaptureMode::Vertical;
    bool m_detectFixedElements = true;
    double m_confidenceThreshold = 0.45;

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
