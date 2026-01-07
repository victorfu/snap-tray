#ifndef SEAMQUALITYEVALUATOR_H
#define SEAMQUALITYEVALUATOR_H

#include <QImage>
#include <QRect>

/**
 * @brief Evaluates the quality of image stitching seams
 *
 * This class analyzes the overlap region between two stitched images
 * to determine if the alignment is correct. It uses multiple metrics:
 * - NCC (Normalized Cross-Correlation) for overall similarity
 * - MSE (Mean Squared Error) for pixel-level difference
 * - Edge alignment score for sharp boundaries
 *
 * Key features:
 * - Pure algorithm class (no state, unit-testable)
 * - Provides confidence score and pass/fail decision
 * - Suggests corrections (e.g., retry with different overlap)
 *
 * Usage:
 *   SeamQualityEvaluator::Result result = SeamQualityEvaluator::evaluate(
 *       prevTail, newHead, overlap, ScrollDirection::Down);
 *   if (!result.passed) {
 *       // Try alternative overlap or matcher
 *   }
 */
class SeamQualityEvaluator
{
public:
    enum class ScrollDirection {
        Down,
        Up,
        Left,
        Right
    };

    struct Config {
        double nccThreshold;           // Minimum NCC for pass
        double mseThreshold;           // Maximum normalized MSE for pass (0..1)
        double edgeAlignThreshold;     // Minimum edge alignment score
        int sampleStripHeight;         // Height of strip to sample at seam
        bool useEdgeAlignment;         // Enable edge-based scoring

        Config()
            : nccThreshold(0.85)
            , mseThreshold(0.08)
            , edgeAlignThreshold(0.80)
            , sampleStripHeight(8)
            , useEdgeAlignment(true)
        {}
    };

    struct Result {
        bool passed = false;          // True if seam quality is acceptable
        double seamScore = 0.0;       // Combined quality score (0..1)
        double nccScore = 0.0;        // NCC similarity (0..1)
        double mseScore = 0.0;        // Normalized MSE (0..1, lower is better)
        double edgeScore = 0.0;       // Edge alignment score (0..1)
        int suggestedOverlapDelta = 0; // Suggested overlap adjustment (-N..+N pixels)
        QString failureReason;        // Human-readable failure description
    };

    // Evaluate seam quality between prev image tail and new image head
    static Result evaluate(const QImage &prevImage, const QImage &newImage,
                          int overlap, ScrollDirection direction,
                          const Config &config = Config());

    // Simplified API: check if overlap is good without full Result
    static bool isOverlapValid(const QImage &prevImage, const QImage &newImage,
                               int overlap, ScrollDirection direction);

    // Find best overlap by searching around detected value
    static int findBestOverlap(const QImage &prevImage, const QImage &newImage,
                               int detectedOverlap, ScrollDirection direction,
                               int searchRadius = 3);

private:
    static double computeNCC(const QImage &strip1, const QImage &strip2);
    static double computeMSE(const QImage &strip1, const QImage &strip2);
    static double computeEdgeAlignment(const QImage &strip1, const QImage &strip2, bool isVertical);
    static QImage extractOverlapStrip(const QImage &image, int overlap,
                                       ScrollDirection direction, bool isTrailing);
};

#endif // SEAMQUALITYEVALUATOR_H
