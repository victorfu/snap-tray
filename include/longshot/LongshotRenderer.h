#pragma once

#include "longshot/LongshotFrameSource.h"
#include "longshot/LongshotPipeline.h"
#include "longshot/LongshotTypes.h"

#include <QImage>
#include <QList>

#include <vector>

namespace SnapTray::Longshot {

// Edges below this confidence mark the rows they place as low confidence.
constexpr double kLowConfidence = 0.5;

struct RenderResult {
    LongshotError error = LongshotError::None;
    QList<QImage> parts;            // one image unless options.splitOversize
    int fullHeightPx = 0;           // height before any cap or split
    bool heightCapped = false;
    int autoCroppedLeft = 0;        // static side columns removed
    int autoCroppedRight = 0;
    std::vector<int> breakRows;     // output rows left empty because no frame covered them reliably
    std::vector<int> lowConfidenceRows;
    bool stickyHeaderIncluded = false;
};

struct TileAssignment {
    int outputTop = 0;
    int outputBottom = 0; // exclusive
    int frameIndex = -1;  // -1 = no reliable coverage
};

// Pass 2. Decodes the range once more sequentially and paints each tile from
// its chosen frame's valid pixels only.
class LongshotRenderer
{
public:
    static RenderResult render(LongshotFrameSource& source, const QString& path, qint64 startMs, qint64 endMs,
                               const QRect& crop, const AnalysisResult& analysis, const LongshotOptions& options,
                               const ProgressFn& progress);

    // One frame per tile: must cover the tile with valid rows; scored by
    // stationary (+2), keyframe (+1), tile centre near frame centre (+0..1),
    // later frames win ties; frames whose thumbnail disagrees with the
    // majority of candidates are rejected.
    static std::vector<TileAssignment> assignTiles(const AnalysisResult& analysis, const LongshotOptions& options,
                                                   int outputHeight, int minPosition);

    // Moves each boundary between different frames to the lowest-gradient
    // row within kSeamSearchRows of it (gradient of the frame above).
    static void placeSeams(std::vector<TileAssignment>& tiles, const AnalysisResult& analysis, int minPosition);
};

} // namespace SnapTray::Longshot
