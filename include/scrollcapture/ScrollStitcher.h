#ifndef SCROLLSTITCHER_H
#define SCROLLSTITCHER_H

#include "scrollcapture/ScrollCaptureTypes.h"

#include <QImage>
#include <QPixmap>
#include <QString>

#include <deque>

class ScrollStitcher
{
public:
    struct MatchResult {
        bool valid = false;
        int deltaFullPx = 0;
        double score = 0.0;
        bool weak = false;
        QString message;
    };

    struct MatchOptions {
        ScrollDirection direction = ScrollDirection::Down;
        double matchThreshold = 0.65;
        int minDeltaDownscaled = 16;
        double xTrimLeftRatio = 0.06;
        double xTrimRightRatio = 0.94;
        bool useMultiTemplate = true;
    };

    void reset();
    bool initialize(const QImage& firstContentFrame);
    void setOptions(const MatchOptions& options);

    MatchResult appendFromPair(const QImage& prevContentFrame,
                               const QImage& currContentFrame);
    bool appendByDelta(const QImage& currContentFrame, int deltaFullPx);

    int currentHeight() const { return m_totalHeight; }
    int frameCount() const { return m_frameCount; }
    bool isInitialized() const { return m_initialized; }

    QPixmap compose() const;

private:
    bool appendStrip(const QImage& currentFrame, int deltaFullPx);

    MatchOptions m_options;
    std::deque<QImage> m_segments;
    int m_width = 0;
    int m_totalHeight = 0;
    int m_frameCount = 0;
    bool m_initialized = false;
};

#endif // SCROLLSTITCHER_H
