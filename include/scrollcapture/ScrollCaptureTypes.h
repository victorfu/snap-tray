#ifndef SCROLLCAPTURETYPES_H
#define SCROLLCAPTURETYPES_H

#include <QPixmap>
#include <QString>

enum class ScrollCapturePreset {
    WebPage,
    ChatHistory
};

enum class ScrollDirection {
    Down,
    Up
};

enum class ScrollCaptureState {
    Idle,
    PickingWindow,
    Arming,
    Capturing,
    Stitching,
    Completed,
    Cancelled,
    Error
};

struct ScrollCaptureParams {
    ScrollDirection direction = ScrollDirection::Down;
    double stepRatio = 0.85;
    int minStepPx = 120;
    double maxStepRatio = 0.92;
    int maxSettleMs = 1200;
    double matchThreshold = 0.65;
    int minDeltaFullPx = 12;
    int maxFrames = 80;
    int maxHeightPx = 30000;
    int maxDurationMs = 35000;
    int noProgressStreakLimit = 8;
    int minProgressDeltaPx = 10;
    int noWarpRetryBeforeFallback = 2;
    int maxWarpFallbackCount = 1;
};

struct ScrollCaptureResult {
    bool success = false;
    QPixmap pixmap;
    int frameCount = 0;
    int stitchedHeight = 0;
    QString reason;
    QString warning;
};

inline ScrollCaptureParams paramsForPreset(ScrollCapturePreset preset)
{
    ScrollCaptureParams params;
    if (preset == ScrollCapturePreset::ChatHistory) {
        params.direction = ScrollDirection::Up;
        // Chat UIs often contain repeated patterns; keep more overlap for stable matching.
        params.stepRatio = 0.50;
        params.maxStepRatio = 0.80;
        params.minStepPx = 90;
        params.maxSettleMs = 1800;
        params.matchThreshold = 0.60;
        params.maxFrames = 120;
        params.maxHeightPx = 50000;
        params.maxDurationMs = 60000;
        params.noProgressStreakLimit = 10;
        params.minProgressDeltaPx = 8;
        params.noWarpRetryBeforeFallback = 2;
        params.maxWarpFallbackCount = 1;
    }
    return params;
}

inline ScrollDirection directionForPreset(ScrollCapturePreset preset)
{
    return (preset == ScrollCapturePreset::ChatHistory)
        ? ScrollDirection::Up
        : ScrollDirection::Down;
}

#endif // SCROLLCAPTURETYPES_H
