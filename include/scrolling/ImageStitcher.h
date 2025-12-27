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
        Auto
    };
    Q_ENUM(Algorithm)

    enum class ScrollDirection {
        Down,
        Up
    };
    Q_ENUM(ScrollDirection)

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

    // Helper methods
    cv::Mat qImageToCvMat(const QImage &image) const;
    QImage cvMatToQImage(const cv::Mat &mat) const;
    MatchCandidate computeORBMatchCandidate(const QImage &newFrame, ScrollDirection direction);
    MatchCandidate computeTemplateMatchCandidate(const QImage &newFrame, ScrollDirection direction);
    StitchResult applyCandidate(const QImage &newFrame, const MatchCandidate &candidate, Algorithm algorithm);
    StitchResult performStitch(const QImage &newFrame, int overlapPixels, ScrollDirection direction);

    Algorithm m_algorithm = Algorithm::Auto;
    bool m_detectFixedElements = true;
    double m_confidenceThreshold = 0.60;

    QImage m_stitchedResult;
    QImage m_lastFrame;
    QRect m_currentViewportRect;
    int m_frameCount = 0;
    int m_validHeight = 0;

    // ROI parameters
    static constexpr double ROI_FIRST_RATIO = 0.25;   // Bottom/Right 25% of first frame
    static constexpr double ROI_SECOND_RATIO = 0.45;  // Top/Left 45% of second frame
    static constexpr int MIN_OVERLAP = 20;
    static constexpr int MAX_OVERLAP = 500;
};

#endif // IMAGESTITCHER_H