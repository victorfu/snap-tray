#ifndef FRAMESTABILITYDETECTOR_H
#define FRAMESTABILITYDETECTOR_H

#include <QObject>
#include <QImage>
#include <QRect>
#include <deque>

/**
 * @brief Detects frame stability for closed-loop scroll capture control
 *
 * This class analyzes consecutive frames to determine when the display
 * has stabilized after a scroll event. It uses normalized cross-correlation
 * (NCC) on a center ROI to detect changes, avoiding fixed headers/footers.
 *
 * Key features:
 * - Uses center ROI to avoid fixed UI elements
 * - Requires K consecutive stable frames for confirmation
 * - Provides stability score (0..1) for confidence
 * - Tracks time-to-stability for adaptive delay adjustment
 *
 * Usage:
 *   detector.reset();  // Start fresh after scroll
 *   for each frame:
 *       if (detector.addFrame(frame)) {
 *           // Frame is stable, safe to capture
 *       }
 */
class FrameStabilityDetector : public QObject
{
    Q_OBJECT

public:
    struct Config {
        int roiMarginPercent = 15;           // Margin from edges for ROI (avoid fixed elements)
        double stabilityThreshold = 0.98;     // NCC threshold for "stable" (0..1)
        int consecutiveStableRequired = 2;    // K stable frames needed
        int maxWaitFrames = 30;              // Max frames before timeout (~500ms at 60fps)
        int downsampleMaxDim = 128;          // Downsample for speed
        QRect fixedHeaderRect;               // Known fixed header to exclude
        QRect fixedFooterRect;               // Known fixed footer to exclude
    };

    struct StabilityResult {
        bool stable = false;            // True if frame is stable
        double stabilityScore = 0.0;    // NCC score (0..1, higher = more stable)
        int consecutiveStableCount = 0; // How many stable frames in a row
        int totalFramesChecked = 0;     // Total frames since reset
        bool timedOut = false;          // True if maxWaitFrames exceeded
    };

    explicit FrameStabilityDetector(QObject *parent = nullptr);
    ~FrameStabilityDetector() override;

    // Configuration
    void setConfig(const Config &config);
    Config config() const;

    void setFixedRegions(const QRect &header, const QRect &footer);

    // Core operations
    void reset();
    StabilityResult addFrame(const QImage &frame);
    StabilityResult currentResult() const;

    // Static utility for single-pair comparison
    static double compareFrames(const QImage &frame1, const QImage &frame2,
                               const QRect &roi = QRect(), int maxDim = 128);

private:
    QImage extractROI(const QImage &frame) const;
    QImage downsample(const QImage &image) const;
    double computeNCC(const QImage &img1, const QImage &img2) const;

    Config m_config;
    StabilityResult m_result;
    QImage m_lastFrame;
    QImage m_lastROI;
};

#endif // FRAMESTABILITYDETECTOR_H
