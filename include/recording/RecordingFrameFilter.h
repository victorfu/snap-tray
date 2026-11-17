#pragma once

#include "recording/PreviewEditHistory.h"
#include <functional>
#include <memory>

namespace SnapTray {
// Immutable edit snapshot. Input frames must already show state.crop (or the
// whole source when empty). Times and mask/zoom coordinates remain source based.
std::function<void(QImage&, qint64, qint64)> recordingFrameFilter(
    const PreviewEditState& state, const QSize& videoSize, const TimelineMap& editTimeline,
    std::shared_ptr<const CursorTimeline> timeline = {},
    std::shared_ptr<const KeyTimeline> keyTimeline = {},
    std::shared_ptr<const ZoomTrack> preparedZoom = {}, qint64 clipEndMs = -1);
} // namespace SnapTray
