#ifndef SCROLLALIGNMENTTYPES_H
#define SCROLLALIGNMENTTYPES_H

#include <QString>

enum class ScrollAlignmentStrategy {
    TemplateNcc1D
};

enum class ScrollAlignmentRejectionCode {
    None,
    InvalidInput,
    LowTexture,
    LowConfidence,
    AmbiguousCluster,
    HorizontalDrift,
    BoundaryAmbiguity,
    PhotometricMismatch
};

struct ScrollAlignmentOptions {
    double templateTopStartRatio = 0.20;
    double templateTopEndRatio = 0.30;
    double searchBottomStartRatio = 0.50;
    int xTolerancePx = 2;
    double confidenceThreshold = 0.92;
    int consensusMinBands = 2;
    double historyDySoftWindowRatio = 0.35;
    double ambiguityRejectGap = 0.008;

    int minMovementPx = 12;
    int minScrollPx = 8;
};

struct ScrollAlignmentResult {
    bool ok = false;
    ScrollAlignmentStrategy strategy = ScrollAlignmentStrategy::TemplateNcc1D;
    QString reason;

    int dySigned = 0;
    double dx = 0.0;
    double scale = 1.0;
    double shear = 0.0;

    double confidence = 0.0;
    double rawNccScore = 0.0;
    double confidencePenalty = 0.0;
    double mad = 1.0;
    int overlapPx = 0;

    bool movementVerified = false;
    int inliers = 0;
    bool rejectedByConfidence = false;
    int consensusSupport = 0;
    double dyClusterSpread = 0.0;
    ScrollAlignmentRejectionCode rejectionCode = ScrollAlignmentRejectionCode::None;
};

#endif // SCROLLALIGNMENTTYPES_H
