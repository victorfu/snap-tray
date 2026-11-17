#pragma once

#include "annotations/AnnotationItem.h"
#include "history/HistoryStore.h"

#include <QPixmap>
#include <QSize>
#include <QString>
#include <memory>
#include <optional>
#include <vector>

using SharedPixmap = std::shared_ptr<const QPixmap>;

namespace SnapTray {

// Everything replaying a history entry needs, fully read and decoded.
struct HistoryReplayDocument
{
    QPixmap canvas;
    qreal devicePixelRatio = 1.0;
    QSize canvasLogicalSize;
    // The canvas as the mosaic annotations below hold it.
    SharedPixmap sourcePixmap;
    std::vector<std::unique_ptr<AnnotationItem>> annotations;
};

// Loads an entry's replay document without side effects, so a caller can find
// out whether the entry opens before giving up what is on screen. Fails when
// the entry is not replayable, the canvas cannot be read, the entry has no
// selection, or annotations.json is missing, empty or not a complete document
// (see decodeAnnotationItems). A missing annotations file is never "no
// annotations": the canvas alone would show what a mosaic was hiding.
std::optional<HistoryReplayDocument> loadHistoryReplayDocument(const HistoryEntry& entry,
                                                               QString* errorMessage = nullptr);

} // namespace SnapTray
