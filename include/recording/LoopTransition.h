#pragma once
#include "recording/TimelineEdit.h"
#include <algorithm>

namespace SnapTray {
// Presentation-only return shared by the pointer and camera. Its interval is
// measured in output time and confined to the final kept part.
struct LoopTransition {
    static constexpr qint64 kReturnMs = 700;
    static constexpr qint64 kSettleMs = 120;
    qint64 startOutputMs = 0;
    qint64 settleOutputMs = 0;
    qint64 endOutputMs = 0;
    qint64 firstSourceMs = 0;
    qint64 returnSourceMs = 0;
    qint64 lastSourceMs = 0;

    double progress(qint64 outputMs) const {
        const double t = std::clamp(double(outputMs - startOutputMs)
            / std::max<qint64>(1, settleOutputMs - startOutputMs), 0.0, 1.0);
        return 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);
    }
    static std::optional<LoopTransition> forTimeline(const TimelineMap& timeline, qint64 clipEndMs) {
        const qint64 end = timeline.isEmpty() ? clipEndMs : timeline.outputDurationMs();
        if (end <= 1) return std::nullopt;
        const qint64 lastPartStart = timeline.isEmpty() ? 0
            : timeline.segmentOutputStartMs(int(timeline.segments().size()) - 1);
        const qint64 begin = std::max(lastPartStart, end - kReturnMs);
        const auto source = [&timeline](qint64 t) { return timeline.isEmpty() ? t : timeline.outputToSource(t); };
        return LoopTransition{begin, end - std::min(kSettleMs, (end - begin) / 3), end,
                              source(0), source(begin), source(end - 1)};
    }
};
} // namespace SnapTray
