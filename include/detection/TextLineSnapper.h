#pragma once
#include "detection/OCRTypes.h"
#include <QPointF>
#include <memory>
#include <optional>

// All geometry here is canvas-logical, with a top-left origin.
struct TextLineSnapOptions {
    qreal verticalTolerance = 0.75; // center distance in line heights
    qreal ambiguityMargin = 0.2;
    qreal maximumWordGap = 0.75; // in line heights; do not bridge columns
    qreal minimumVerticalOverlap = 0.7;
    qreal maximumHeightRatio = 1.4;
    qreal maximumWordOverlap = 0.2;
    qreal minimumOverlap = 0.25; // in line heights
    qreal minimumConfidence = 0.35;
    qreal heightPadding = 0.15;
};
struct TextLineSnap {
    int key = -1;
    QRectF rect;
    int width = 0;
};
class TextLineSnapshot {
public:
    enum class Reason { Hit, NoMatch, Ambiguous, InvalidInput };
    struct Match {
        std::optional<TextLineSnap> line;
        Reason reason = Reason::NoMatch;
    };
    static std::shared_ptr<const TextLineSnapshot> fromOCR(
        const QVector<OCRTextBlock>& blocks, const QRectF& logicalCrop,
        const TextLineSnapOptions& options = {});
    Match query(const QPointF& start, std::optional<qreal> endX = {}) const;
    const QVector<TextLineSnap>& lines() const { return m_lines; }
private:
    QVector<TextLineSnap> m_lines; // sorted by vertical center
    TextLineSnapOptions m_options;
    qreal m_maxHeight = 0;
};
