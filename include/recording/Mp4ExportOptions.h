#pragma once

#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QtGlobal>

// MP4 export choices in the recording preview (#190): output size, frame
// rate, quality and codec, plus the size estimate shown next to them. Every
// default keeps the export exactly as before (crop size, source frame rate,
// the Recording settings' quality, H.264), so the unedited fast paths stay.
namespace SnapTray {

struct Mp4ExportOptions {
    enum class Resolution { Original, P1080, P720, CustomWidth };
    enum class Quality { Default, Low, Medium, High }; // Default = the Recording settings' quality
    enum class Codec { H264, Hevc };

    Resolution resolution = Resolution::Original;
    int customWidth = 1280;  // CustomWidth only; the height keeps the aspect ratio
    int frameRate = 0;       // 0 = the source's; otherwise at most the source's
    Quality quality = Quality::Default;
    Codec codec = Codec::H264;

    static constexpr int kMinCustomWidth = 64;
    static constexpr int kMaxCustomWidth = 7680;
    static constexpr int kFrameRateChoices[] = {0, 60, 30, 15};

    bool operator==(const Mp4ExportOptions& o) const;
    bool operator!=(const Mp4ExportOptions& o) const { return !(*this == o); }

    // Size of the encoded picture for a (cropped) source picture: never
    // larger than it, aspect ratio kept, both sides even.
    QSize outputSize(const QSize& sourceSize) const;
    // Encoded frame rate for a source frame rate (rounded; 30 when unknown).
    int outputFrameRate(double sourceFps) const;
    // The 0-100 quality the bitrate formula uses.
    int outputQuality(int settingsQuality) const;
    QString codecId() const; // kVideoCodecH264 / kVideoCodecHevc
    // Video bitrate the export targets (HEVC needs less for the same look).
    int videoBitrate(const QSize& outputSize, int outputFps, int settingsQuality) const;

    // True when, for this source, the options change the picture or stream:
    // smaller size, lower frame rate or another codec. Quality alone does not
    // (it is judged by the smart-save bitrate rule, as before).
    bool changesStream(const QSize& sourceSize, double sourceFps, const QString& sourceCodec) const;
    // Without knowing the source: false when the options can change nothing
    // (original size and rate, H.264), so the source need not be probed.
    bool mayChangeStream() const { return resolution != Resolution::Original || frameRate > 0 || codec != Codec::H264; }

    QJsonObject toJson() const;
    static Mp4ExportOptions fromJson(const QJsonObject& json); // missing or unknown keys keep defaults

    static constexpr int kQualityLow = 30;
    static constexpr int kQualityMedium = 60;
    static constexpr int kQualityHigh = 90;
    static constexpr double kHevcBitrateFactor = 0.6;
};

// Estimated MP4 size: video and audio bitrate over the exported duration,
// plus container overhead. An estimate: real encoders land near the target
// on average but vary with the content.
namespace Mp4SizeEstimate {
constexpr int kAudioBitrate = 192000;       // what SnapTray records AAC at
constexpr double kContainerOverhead = 1.02; // MP4 boxes and indexes
qint64 bytes(int videoBitrate, bool hasAudio, qint64 durationMs);

// What the recording itself spent on the video it keeps, the best guide to
// how much this content needs.
struct Source {
    qint64 keptVideoBytes = -1; // -1: unknown
    QSize videoSize;
    bool hevc = false;
};

// Estimated MP4 size of an export lasting outputDurationMs. Encoders treat
// the target bitrate as a ceiling and simple content (a still screen, a
// terminal) stays far below it, so a target-only estimate came out about 10x
// high for a still recording. The video is estimated as the smaller of:
// - the target over the exported duration (the encoder writes no more), and
// - the video bytes the recording holds in the kept parts, scaled by the
//   output area over source.videoSize (the caller decides what scaling is
//   uniform enough to count) and by codec. These are counted from the file, not averaged over
//   it: a short busy part kept from a long still recording holds far more
//   than its share, and a still part far less (PR #213 review).
// The output frame rate is not scaled: still frames cost almost nothing, so
// fewer of them saves little. Without a usable source, the target alone.
qint64 expectedBytes(int targetBitrate, bool outputHevc, const Source& source, const QSize& outputSize,
                     bool hasAudio, qint64 outputDurationMs);
} // namespace Mp4SizeEstimate

} // namespace SnapTray
