#pragma once

// Internal test seam for native IVideoTranscoder implementations.
//
// Production code never sets a fault: IVideoTranscoder::create() returns a
// transcoder whose fault is VideoTranscodeFault::None, and nothing outside the
// tests includes this header. Tests reach the seam with
//   dynamic_cast<VideoTranscoderFaultInjection*>(transcoder.get())
// so the public IVideoTranscoder contract stays unchanged. Every native
// implementation (AVFoundation, Media Foundation) must implement each fault at
// the equivalent step of its own pipeline.
enum class VideoTranscodeFault {
    None,
    // The writer/sink refuses the audio passthrough stream during setup.
    AudioInputUnsupported,
    // Re-timing an audio sample for the trimmed timeline fails mid-stream.
    AudioRetimeFailure,
    // Appending/writing an audio sample to the output fails mid-stream.
    AudioAppendFailure,
};

// Index (0-based) of the source audio sample buffer (data-carrying buffers
// only) at which the mid-stream faults trigger. Past the first buffer, so the
// output already holds written media when the fault fires. The test trims
// 1 s of 48 kHz AAC, which AVFoundation delivers as 3 buffers of up to ~23
// packets each; an implementation with other chunking must still reach it.
constexpr int kVideoTranscodeFaultAudioSampleIndex = 2;

class VideoTranscoderFaultInjection
{
public:
    virtual ~VideoTranscoderFaultInjection() = default;

    // Applies to every following transcode() call until reset to None.
    virtual void setFaultForTesting(VideoTranscodeFault fault) = 0;
};
