#include "stitching/OpticalFlowPredictor.h"

#include <opencv2/core.hpp>
#include <opencv2/video/tracking.hpp>
#include <opencv2/imgproc.hpp>

#include <QDebug>
#include <QRect>
#include <QPointF>
#include <algorithm>
#include <cmath>

class OpticalFlowPredictor::Private
{
public:
    Config config;

    cv::Mat prevFrame;
    std::vector<cv::Point2f> prevPoints;

    std::vector<cv::Point2f> velocityHistory;
    static constexpr int MAX_HISTORY = 10;

    int frameCount = 0;

    void detectGoodFeatures(const cv::Mat &frame);
    cv::Point2f computeMedianVelocity(const std::vector<cv::Point2f> &velocities);
};

void OpticalFlowPredictor::Private::detectGoodFeatures(const cv::Mat &frame)
{
    prevPoints.clear();

    // Use Shi-Tomasi corner detection (goodFeaturesToTrack)
    cv::goodFeaturesToTrack(
        frame,
        prevPoints,
        config.numPoints,
        0.01,              // quality level
        10,                // min distance between features
        cv::noArray(),     // mask
        3,                 // block size
        false,             // use Harris detector
        0.04               // k (Harris parameter, unused here)
    );

    if (prevPoints.size() < 10) {
        // Fallback: grid sampling if few corners detected
        prevPoints.clear();
        int gridSize = 10;
        int stepX = frame.cols / gridSize;
        int stepY = frame.rows / gridSize;

        for (int y = stepY / 2; y < frame.rows; y += stepY) {
            for (int x = stepX / 2; x < frame.cols; x += stepX) {
                prevPoints.emplace_back(static_cast<float>(x), static_cast<float>(y));
            }
        }
    }
}

cv::Point2f OpticalFlowPredictor::Private::computeMedianVelocity(
    const std::vector<cv::Point2f> &velocities)
{
    if (velocities.empty())
        return cv::Point2f(0, 0);

    std::vector<float> vx, vy;
    vx.reserve(velocities.size());
    vy.reserve(velocities.size());

    for (const auto &v : velocities) {
        vx.push_back(v.x);
        vy.push_back(v.y);
    }

    std::sort(vx.begin(), vx.end());
    std::sort(vy.begin(), vy.end());

    size_t mid = velocities.size() / 2;
    return cv::Point2f(vx[mid], vy[mid]);
}

// === Public Implementation ===

OpticalFlowPredictor::OpticalFlowPredictor()
    : d(new Private)
{
}

OpticalFlowPredictor::~OpticalFlowPredictor()
{
    delete d;
}

OpticalFlowPredictor::OpticalFlowPredictor(OpticalFlowPredictor &&other) noexcept
    : d(other.d)
{
    other.d = nullptr;
}

OpticalFlowPredictor &OpticalFlowPredictor::operator=(OpticalFlowPredictor &&other) noexcept
{
    if (this != &other) {
        delete d;
        d = other.d;
        other.d = nullptr;
    }
    return *this;
}

void OpticalFlowPredictor::setConfig(const Config &config)
{
    d->config = config;
}

OpticalFlowPredictor::Config OpticalFlowPredictor::config() const
{
    return d->config;
}

void OpticalFlowPredictor::reset()
{
    d->prevFrame = cv::Mat();
    d->prevPoints.clear();
    d->velocityHistory.clear();
    d->frameCount = 0;
}

int OpticalFlowPredictor::frameCount() const
{
    return d->frameCount;
}

OpticalFlowPredictor::Prediction OpticalFlowPredictor::update(const cv::Mat &frame)
{
    Prediction pred;

    // Convert to grayscale if needed
    cv::Mat gray;
    if (frame.channels() > 1) {
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = frame;
    }

    d->frameCount++;

    // First frame: initialize tracking points
    if (d->prevFrame.empty()) {
        d->prevFrame = gray.clone();
        d->detectGoodFeatures(gray);
        // No prediction on first frame
        return pred;
    }

    // Not enough points to track - reinitialize
    if (d->prevPoints.size() < 10) {
        d->detectGoodFeatures(d->prevFrame);
        if (d->prevPoints.size() < 10) {
            d->prevFrame = gray.clone();
            return pred;
        }
    }

    // Calculate optical flow using Lucas-Kanade
    std::vector<cv::Point2f> nextPoints;
    std::vector<uchar> status;
    std::vector<float> err;

    cv::Size winSize(d->config.winSizeWidth, d->config.winSizeHeight);

    cv::calcOpticalFlowPyrLK(
        d->prevFrame,
        gray,
        d->prevPoints,
        nextPoints,
        status,
        err,
        winSize,
        d->config.maxLevel,
        cv::TermCriteria(
            cv::TermCriteria::COUNT | cv::TermCriteria::EPS,
            d->config.maxIterations,
            d->config.epsilon),
        0,
        d->config.minEigenThreshold);

    // Compute velocities for successfully tracked points
    std::vector<cv::Point2f> velocities;
    std::vector<cv::Point2f> goodPrevPoints, goodNextPoints;

    for (size_t i = 0; i < status.size(); i++) {
        if (status[i] && err[i] < 10.0f) {
            cv::Point2f v = nextPoints[i] - d->prevPoints[i];
            velocities.push_back(v);
            goodPrevPoints.push_back(d->prevPoints[i]);
            goodNextPoints.push_back(nextPoints[i]);
        }
    }

    pred.trackedPoints = static_cast<int>(velocities.size());

    if (velocities.size() < 10) {
        // Tracking failed, reinitialize for next frame
        d->detectGoodFeatures(gray);
        d->prevFrame = gray.clone();
        pred.valid = false;
        return pred;
    }

    // Compute median velocity (robust to outliers)
    cv::Point2f medianVel = d->computeMedianVelocity(velocities);
    pred.velocityX = medianVel.x;
    pred.velocityY = medianVel.y;

    // Update velocity history for smoothing
    d->velocityHistory.push_back(medianVel);
    if (d->velocityHistory.size() > d->MAX_HISTORY) {
        d->velocityHistory.erase(d->velocityHistory.begin());
    }

    // Smooth prediction using velocity history
    cv::Point2f smoothVelocity(0, 0);
    for (const auto &v : d->velocityHistory) {
        smoothVelocity += v;
    }
    smoothVelocity *= (1.0f / static_cast<float>(d->velocityHistory.size()));

    pred.predictedOffsetX = smoothVelocity.x;
    pred.predictedOffsetY = smoothVelocity.y;

    // Compute confidence based on velocity consistency
    float velocityVariance = 0;
    for (const auto &v : velocities) {
        float dx = v.x - medianVel.x;
        float dy = v.y - medianVel.y;
        velocityVariance += dx * dx + dy * dy;
    }
    velocityVariance /= static_cast<float>(velocities.size());

    // Lower variance = higher confidence
    pred.confidence = std::exp(-static_cast<double>(velocityVariance) / 100.0);

    // Suggest search ROI for feature matcher
    float searchMargin = 50.0f; // Extra pixels for safety
    float predictedDY = std::abs(pred.predictedOffsetY);

    int roiHeight = static_cast<int>(predictedDY + searchMargin);
    roiHeight = std::min(roiHeight, gray.rows);

    if (pred.predictedOffsetY > 0) {
        // Scrolling down: content moves up, search top portion of target
        pred.suggestedSearchROI = QRect(0, 0, gray.cols, roiHeight);
    } else if (pred.predictedOffsetY < 0) {
        // Scrolling up: content moves down, search bottom portion of target
        int roiY = gray.rows - roiHeight;
        pred.suggestedSearchROI = QRect(0, roiY, gray.cols, roiHeight);
    } else {
        // No vertical motion detected
        pred.suggestedSearchROI = QRect(0, 0, gray.cols, gray.rows / 3);
    }

    pred.valid = true;

    // Update state for next frame
    d->prevFrame = gray.clone();
    d->prevPoints = goodNextPoints;

    // Periodically refresh tracking points if count drops
    if (d->prevPoints.size() < 50) {
        d->detectGoodFeatures(gray);
    }

    qDebug() << "OpticalFlow: velocity=(" << pred.velocityX << "," << pred.velocityY << ")"
             << "tracked=" << pred.trackedPoints
             << "confidence=" << pred.confidence;

    return pred;
}

std::vector<QPointF> OpticalFlowPredictor::velocityHistory() const
{
    std::vector<QPointF> result;
    result.reserve(d->velocityHistory.size());
    for (const auto &v : d->velocityHistory) {
        result.emplace_back(static_cast<qreal>(v.x), static_cast<qreal>(v.y));
    }
    return result;
}
