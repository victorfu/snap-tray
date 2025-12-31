#ifndef STATICSCENEANALYZER_H
#define STATICSCENEANALYZER_H

#include <QObject>
#include <QRect>
#include <QString>
#include <vector>
#include <memory>

namespace cv {
class Mat;
}

/**
 * @brief Global static element detection using temporal variance analysis
 *
 * Unlike edge-scanning approaches, this analyzer can detect static elements
 * at ANY position in the frame, including:
 * - Floating headers/footers
 * - Sticky sidebars
 * - Centered watermarks
 * - Chat app date bubbles
 * - "Back to top" floating buttons
 *
 * Uses Welford's algorithm for numerically stable online variance computation.
 *
 * Algorithm:
 * For each pixel (x,y), compute variance over N frames:
 *   V(x,y) = sum((I_i(x,y) - mean)^2) / N
 *
 * Low variance = Static (UI element)
 * High variance = Dynamic (scrolling content)
 */
class StaticSceneAnalyzer : public QObject
{
    Q_OBJECT

public:
    struct Config {
        int minFramesForAnalysis = 5;        // Minimum frames before mask is valid
        float varianceThreshold = 0.001f;    // Below this = static
        int morphKernelSize = 5;             // Morphological cleanup kernel
        int minRegionArea = 100;             // Minimum pixels for a valid region
        bool debugVisualization = false;
    };

    struct StaticRegion {
        QRect bounds;
        double staticness;      // 0-1, how static the region is
        QString type;           // "header", "footer", "floating", "sidebar-left", "sidebar-right"
    };

    explicit StaticSceneAnalyzer(QObject *parent = nullptr);
    ~StaticSceneAnalyzer() override;

    void setConfig(const Config &config);
    Config config() const;

    /**
     * @brief Add a new frame to the analysis
     *
     * Frames should be added in sequence during scrolling capture.
     * The analyzer will update its variance estimates online using
     * Welford's numerically stable algorithm.
     */
    void addFrame(const cv::Mat &frame);

    /**
     * @brief Get the current static mask
     *
     * Returns a binary mask where:
     * - White (255) = Static/fixed element (should be excluded from stitching)
     * - Black (0) = Dynamic content (should be stitched)
     */
    cv::Mat getStaticMask() const;

    /**
     * @brief Get the raw variance map (for debugging)
     *
     * Returns normalized variance map (0-255) for visualization.
     */
    cv::Mat getVarianceMap() const;

    /**
     * @brief Apply static mask to a frame
     *
     * Returns the frame with static regions zeroed out.
     * Useful for preprocessing before stitching.
     */
    cv::Mat applyMask(const cv::Mat &frame) const;

    /**
     * @brief Detect and classify static regions
     *
     * Returns a list of detected static regions with their types.
     */
    std::vector<StaticRegion> detectRegions() const;

    /**
     * @brief Get crop values for top/bottom static regions
     *
     * Convenience method for compatibility with existing crop logic.
     */
    void getCropValues(int &topCrop, int &bottomCrop) const;

    /**
     * @brief Check if analysis is ready
     */
    bool isReady() const;

    /**
     * @brief Reset for new capture session
     */
    void reset();

    /**
     * @brief Get frame count
     */
    int frameCount() const;

signals:
    void analysisReady();
    void staticRegionDetected(const StaticRegion &region);

private:
    class Private;
    std::unique_ptr<Private> d;
};

#endif // STATICSCENEANALYZER_H
