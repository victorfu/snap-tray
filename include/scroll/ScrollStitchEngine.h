#ifndef SCROLLSTITCHENGINE_H
#define SCROLLSTITCHENGINE_H

#include <QImage>
#include <QColor>
#include <QSize>
#include <QVector>
#include <QtGlobal>

#include "detection/ScrollAlignmentEngine.h"
#include "detection/ScrollSeamBlender.h"

enum class StitchQuality {
    Good,
    PartiallyGood,
    Bad,
    NoChange
};

enum class StitchRejectionCode {
    None,
    LowConfidence,
    DuplicateTail,
    DuplicateLoop,
    ImplausibleAppend,
    InvalidFrame,
    OverlapTooSmall
};

enum class StitchMatchStrategy {
    RowDiff,
    TemplateNcc1D,
    ColumnSample,
    BestGuess,
    FeatureHnswSurf,
    PhaseCorrelation,
    PhaseCorrelationScale
};

struct StitchFrameResult {
    StitchQuality quality = StitchQuality::Bad;
    StitchRejectionCode rejectionCode = StitchRejectionCode::None;
    double confidence = 0.0;
    int overlapY = 0;
    int appendHeight = 0;
    int stickyHeaderPx = 0;
    int stickyFooterPx = 0;
    StitchMatchStrategy strategy = StitchMatchStrategy::RowDiff;
    int bottomIgnorePx = 0;
    int dySigned = 0;
    double dx = 0.0;
    double scale = 1.0;
    double shear = 0.0;
    double mad = 1.0;
    double rawNccScore = 0.0;
    double confidencePenalty = 0.0;
    int overlapPx = 0;
    bool movementVerified = false;
    bool rejectedByConfidence = false;
    bool duplicateLoopDetected = false;
    double appendPlausibilityScore = 0.0;
    double dynamicMaskCoverage = 0.0;
    bool poissonApplied = false;
};

struct StitchSegment {
    QImage image;
    int appendHeight = 0;
    int trimTop = 0;
    int trimBottom = 0;
    double confidence = 0.0;
    StitchMatchStrategy strategy = StitchMatchStrategy::RowDiff;
};

struct StitchPreviewMeta {
    int frames = 0;
    int height = 0;
    int lastAppend = 0;
    double lastConfidence = 0.0;
    StitchMatchStrategy lastStrategy = StitchMatchStrategy::RowDiff;
    StitchQuality lastQuality = StitchQuality::NoChange;
    int stickyHeaderPx = 0;
    int stickyFooterPx = 0;
    int bottomIgnorePx = 0;
    bool hasLastAppend = false;
};

struct ScrollStitchOptions {
    double goodThreshold = 0.86;
    double partialThreshold = 0.72;
    int minScrollAmount = 12;
    int maxFrames = 300;
    int maxOutputPixels = 120000000;  // ~120 MP
    int maxOutputHeightPx = 30000;
    bool autoIgnoreBottomEdge = true;
    int seamWidthPx = 32;
    double seamFeatherSigma = 8.0;
    bool usePoissonSeam = false;
    double dynamicTvThreshold = 0.015;
    double dynamicWeight = 0.2;
};

class ScrollStitchEngine
{
public:
    explicit ScrollStitchEngine(const ScrollStitchOptions &options = ScrollStitchOptions());

    bool start(const QImage &firstFrame);
    StitchFrameResult append(const QImage &frame);
    void setExternalEstimatedDyHint(int dyPx);

    QImage composeFinal() const;
    QImage preview(int maxWidth, int maxHeight) const;
    StitchPreviewMeta previewMeta() const;

    bool canUndoLastSegment() const;
    bool undoLastSegment();

    bool hasStarted() const { return m_started; }
    int frameCount() const { return m_frameCount; }
    int totalHeight() const { return m_totalHeight; }
    int stickyHeaderPx() const { return m_stickyHeaderPx; }
    int stickyFooterPx() const { return m_stickyFooterPx; }

private:
    struct MatchResult {
        bool valid = false;
        int dy = 0;
        int overlap = 0;
        int matchedRows = 0;
        int bottomIgnore = 0;
        bool bestGuess = false;
        double confidence = 0.0;
        StitchMatchStrategy strategy = StitchMatchStrategy::RowDiff;
    };

    struct UndoState {
        int segmentCount = 0;
        int totalHeight = 0;
        int frameCount = 0;
        int stickyHeaderPx = 0;
        int stickyFooterPx = 0;
        int headerStableCount = 0;
        int footerStableCount = 0;
        int lastHeaderCandidate = 0;
        int lastFooterCandidate = 0;
        bool hasBestMatch = false;
        int bestMatchDy = 0;
        int bestBottomIgnore = 0;
        int bestMatchedRows = 0;
        int bottomIgnoreCandidate = 0;
        int bottomIgnoreStableCount = 0;
        int committedBottomIgnore = 0;
        bool hasDyHistory = false;
        double averageDy = 0.0;
        int dyHistoryCount = 0;
        QImage lastFrame;
        QImage lastGray;
        QImage observedPreviousGray;
        QImage observedOlderGray;
        bool hasLastResult = false;
        StitchFrameResult lastResult;
        QVector<quint64> recentFingerprints;
    };

    static QImage toGray(const QImage &image);
    static double rowDifference(const uchar *a, const uchar *b, int width);
    static double regionDifference(const QImage &a, int ay, const QImage &b, int by,
                                   int width, int height, int leftCrop, int rightCrop);
    static int contiguousStableRows(const QImage &a, const QImage &b, bool fromTop, int maxScanRows);

    MatchResult findBestVerticalMatch(const QImage &prevGray,
                                      const QImage &currGray,
                                      int bottomIgnore) const;
    MatchResult findColumnSampleMatch(const QImage &prevGray,
                                      const QImage &currGray,
                                      int bottomIgnore) const;
    int calculateBottomIgnore(const QImage &prevGray, const QImage &currGray) const;
    void updateBottomIgnoreStability(int bottomIgnore);
    void updateStickyBands(const QImage &prevGray, const QImage &currGray);
    void invalidatePreviewCache();
    static quint64 segmentFingerprint(const QImage &segmentGray);
    bool detectFingerprintLoop(quint64 fingerprint) const;

    ScrollStitchOptions m_options;
    ScrollAlignmentEngine m_alignmentEngine;
    ScrollSeamBlender m_seamBlender;

    bool m_started = false;
    int m_frameCount = 0;
    int m_stickyHeaderPx = 0;
    int m_stickyFooterPx = 0;
    int m_headerStableCount = 0;
    int m_footerStableCount = 0;
    int m_lastHeaderCandidate = 0;
    int m_lastFooterCandidate = 0;
    bool m_hasBestMatch = false;
    int m_bestMatchDy = 0;
    int m_bestBottomIgnore = 0;
    int m_bestMatchedRows = 0;
    int m_bottomIgnoreCandidate = 0;
    int m_bottomIgnoreStableCount = 0;
    int m_committedBottomIgnore = 0;
    bool m_hasDyHistory = false;
    double m_averageDy = 0.0;
    int m_dyHistoryCount = 0;
    int m_externalEstimatedDyHint = 0;

    QImage m_lastFrame;
    QImage m_lastGray;
    QImage m_observedPreviousGray;
    QImage m_observedOlderGray;
    QVector<StitchSegment> m_segments;
    QVector<UndoState> m_undoStack;
    int m_totalHeight = 0;
    QColor m_backgroundColor = QColor(255, 255, 255, 255);
    bool m_hasLastResult = false;
    StitchFrameResult m_lastResult;

    mutable bool m_composedCacheValid = false;
    mutable QImage m_cachedComposed;
    mutable bool m_previewCacheValid = false;
    mutable QSize m_cachedPreviewSize;
    mutable QImage m_cachedPreview;
    QVector<quint64> m_recentFingerprints;
};

#endif // SCROLLSTITCHENGINE_H
