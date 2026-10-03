#pragma once

namespace SnapTray {

// How a video encoder is asked to spend bits. Bitrate is the historical mode
// (average bitrate from VideoBitrate::forQuality). ConstantQuality asks for
// quality-based encoding bounded by the intermediate bitrate ceiling;
// platforms whose quality mode cannot be bounded (Windows Media Foundation)
// encode VBR at that ceiling instead and report Bitrate through
// IVideoEncoder::effectiveRateControl().
enum class VideoRateControl {
    Bitrate,
    ConstantQuality,
};

} // namespace SnapTray
