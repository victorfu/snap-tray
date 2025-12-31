#ifndef OPTICALFLOWPREDICTOR_H
#define OPTICALFLOWPREDICTOR_H

#include <QRect>
#include <QPointF>
#include <vector>

namespace cv {
class Mat;
}

/**
 * @brief Sparse optical flow for scroll velocity estimation
 *
 * Uses Lucas-Kanade pyramid optical flow to estimate the scroll
 * velocity between frames. This prediction is used to:
 *
 * 1. Narrow FeatureMatcher search region (faster + fewer false positives)
 * 2. Recover from fast scrolling (large displacement)
 * 3. Detect scroll direction changes
 */
class OpticalFlowPredictor
{
public:
    struct Config {
        int numPoints = 100;          // Number of tracking points
        int maxLevel = 3;             // Pyramid levels (0 = no pyramid)
        int winSizeWidth = 21;        // Search window width
        int winSizeHeight = 21;       // Search window height
        int maxIterations = 30;       // LK max iterations
        double epsilon = 0.01;        // LK convergence threshold
        double minEigenThreshold = 0.001; // Minimum eigenvalue for good features
    };

    struct Prediction {
        bool valid = false;

        float velocityX = 0.0f;       // Horizontal velocity (pixels/frame)
        float velocityY = 0.0f;       // Vertical velocity (pixels/frame)

        float predictedOffsetX = 0.0f; // Predicted next offset (smoothed)
        float predictedOffsetY = 0.0f;

        double confidence = 0.0;      // 0-1 based on tracking quality
        int trackedPoints = 0;

        // Search region hint for FeatureMatcher
        QRect suggestedSearchROI;
    };

    OpticalFlowPredictor();
    ~OpticalFlowPredictor();

    // Non-copyable but movable
    OpticalFlowPredictor(const OpticalFlowPredictor &) = delete;
    OpticalFlowPredictor &operator=(const OpticalFlowPredictor &) = delete;
    OpticalFlowPredictor(OpticalFlowPredictor &&other) noexcept;
    OpticalFlowPredictor &operator=(OpticalFlowPredictor &&other) noexcept;

    void setConfig(const Config &config);
    Config config() const;

    /**
     * @brief Update with new frame and get prediction
     *
     * @param frame Current frame (color or grayscale)
     * @return Prediction with velocity and search hint
     */
    Prediction update(const cv::Mat &frame);

    /**
     * @brief Reset tracking state (call when capture starts)
     */
    void reset();

    /**
     * @brief Get velocity history for analysis
     */
    std::vector<QPointF> velocityHistory() const;

    /**
     * @brief Get number of frames processed
     */
    int frameCount() const;

private:
    class Private;
    Private *d;
};

#endif // OPTICALFLOWPREDICTOR_H
