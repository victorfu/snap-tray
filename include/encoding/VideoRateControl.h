#pragma once

namespace SnapTray {

// How a video encoder is asked to spend bits. Bitrate is the historical mode
// (average bitrate from VideoBitrate::forQuality). ConstantQuality asks the
// platform encoder for a quality target with a bitrate ceiling; encoders that
// cannot honour it fall back to Bitrate and report that through
// IVideoEncoder::effectiveRateControl().
enum class VideoRateControl {
    Bitrate,
    ConstantQuality,
};

} // namespace SnapTray
