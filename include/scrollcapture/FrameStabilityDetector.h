#ifndef FRAMESTABILITYDETECTOR_H
#define FRAMESTABILITYDETECTOR_H

#include <QImage>
#include <QRect>

#include <atomic>

class ICaptureEngine;

class FrameStabilityDetector
{
public:
    struct Params {
        int intervalMs = 120;
        int requiredStableCount = 4;
        double stableThreshold = 1.8;
        int maxSettleMs = 1200;
    };

    struct Result {
        bool stable = false;
        bool weak = false;
        QImage frame;
        double lastDiff = 0.0;
    };

    Result waitUntilStable(ICaptureEngine* engine,
                           const QRect& contentRect,
                           const QRect& windowRect,
                           const Params& params,
                           std::atomic_bool* cancelFlag,
                           std::atomic_bool* stopFlag = nullptr) const;
};

#endif // FRAMESTABILITYDETECTOR_H
