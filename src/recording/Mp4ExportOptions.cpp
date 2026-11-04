#include "recording/Mp4ExportOptions.h"

#include "encoding/IntermediateQuality.h"
#include "encoding/VideoBitrate.h"
#include "video/IVideoTranscoder.h"

#include <algorithm>
#include <cmath>

namespace SnapTray {

namespace {

constexpr double kBitsPerByte = 8.0;
constexpr double kMsPerSecond = 1000.0;
constexpr int kShortSide1080 = 1080;
constexpr int kShortSide720 = 720;

template <typename Enum>
struct Named {
    Enum value;
    const char* name;
};

constexpr Named<Mp4ExportOptions::Resolution> kResolutions[] = {
    {Mp4ExportOptions::Resolution::Original, "original"},
    {Mp4ExportOptions::Resolution::P1080, "1080p"},
    {Mp4ExportOptions::Resolution::P720, "720p"},
    {Mp4ExportOptions::Resolution::CustomWidth, "customWidth"},
};

constexpr Named<Mp4ExportOptions::Quality> kQualities[] = {
    {Mp4ExportOptions::Quality::Default, "default"},
    {Mp4ExportOptions::Quality::Low, "low"},
    {Mp4ExportOptions::Quality::Medium, "medium"},
    {Mp4ExportOptions::Quality::High, "high"},
};

constexpr Named<Mp4ExportOptions::Codec> kCodecs[] = {
    {Mp4ExportOptions::Codec::H264, kVideoCodecH264},
    {Mp4ExportOptions::Codec::Hevc, kVideoCodecHevc},
};

template <typename Enum, size_t N>
const char* nameOf(const Named<Enum> (&table)[N], Enum value)
{
    for (const auto& entry : table) {
        if (entry.value == value) return entry.name;
    }
    return table[0].name;
}

template <typename Enum, size_t N>
Enum valueOf(const Named<Enum> (&table)[N], const QString& name, Enum fallback)
{
    for (const auto& entry : table) {
        if (name == QLatin1String(entry.name)) return entry.value;
    }
    return fallback;
}

int evenFloor(double value)
{
    return std::max(2, int(std::floor(value / 2.0)) * 2);
}

} // namespace

bool Mp4ExportOptions::operator==(const Mp4ExportOptions& o) const
{
    return resolution == o.resolution && customWidth == o.customWidth && frameRate == o.frameRate
        && quality == o.quality && codec == o.codec;
}

QSize Mp4ExportOptions::outputSize(const QSize& sourceSize) const
{
    if (sourceSize.isEmpty()) return sourceSize;
    double scale = 1.0;
    const int shortSide = std::min(sourceSize.width(), sourceSize.height());
    switch (resolution) {
    case Resolution::Original:
        return sourceSize;
    case Resolution::P1080:
        scale = double(kShortSide1080) / shortSide;
        break;
    case Resolution::P720:
        scale = double(kShortSide720) / shortSide;
        break;
    case Resolution::CustomWidth:
        scale = double(std::clamp(customWidth, kMinCustomWidth, kMaxCustomWidth)) / sourceSize.width();
        break;
    }
    if (scale >= 1.0) return sourceSize; // never upscale
    return QSize(evenFloor(sourceSize.width() * scale), evenFloor(sourceSize.height() * scale));
}

int Mp4ExportOptions::outputFrameRate(double sourceFps) const
{
    const int source = IntermediateQuality::effectiveFrameRate(sourceFps);
    return frameRate > 0 ? std::min(frameRate, source) : source;
}

int Mp4ExportOptions::outputQuality(int settingsQuality) const
{
    switch (quality) {
    case Quality::Default: return settingsQuality;
    case Quality::Low: return kQualityLow;
    case Quality::Medium: return kQualityMedium;
    case Quality::High: return kQualityHigh;
    }
    return settingsQuality;
}

QString Mp4ExportOptions::codecId() const
{
    return QString::fromLatin1(nameOf(kCodecs, codec));
}

int Mp4ExportOptions::videoBitrate(const QSize& outputSize, int outputFps, int settingsQuality) const
{
    const int h264 = VideoBitrate::forQuality(outputSize, outputFps, outputQuality(settingsQuality));
    if (codec != Codec::Hevc) return h264;
    return std::max(VideoBitrate::kMinBitrate, int(h264 * kHevcBitrateFactor));
}

bool Mp4ExportOptions::changesStream(const QSize& sourceSize, double sourceFps, const QString& sourceCodec) const
{
    if (outputSize(sourceSize) != sourceSize) return true;
    if (frameRate > 0 && sourceFps > 0 && outputFrameRate(sourceFps) < qRound(sourceFps)) return true;
    return codec != Codec::H264 && sourceCodec != codecId();
}

QJsonObject Mp4ExportOptions::toJson() const
{
    return {{QStringLiteral("resolution"), QLatin1String(nameOf(kResolutions, resolution))},
            {QStringLiteral("customWidth"), customWidth},
            {QStringLiteral("frameRate"), frameRate},
            {QStringLiteral("quality"), QLatin1String(nameOf(kQualities, quality))},
            {QStringLiteral("codec"), QLatin1String(nameOf(kCodecs, codec))}};
}

Mp4ExportOptions Mp4ExportOptions::fromJson(const QJsonObject& json)
{
    Mp4ExportOptions options;
    options.resolution = valueOf(kResolutions, json.value(QStringLiteral("resolution")).toString(), options.resolution);
    options.customWidth = std::clamp(json.value(QStringLiteral("customWidth")).toInt(options.customWidth),
                                     kMinCustomWidth, kMaxCustomWidth);
    const int fps = json.value(QStringLiteral("frameRate")).toInt(0);
    if (std::find(std::begin(kFrameRateChoices), std::end(kFrameRateChoices), fps) != std::end(kFrameRateChoices)) {
        options.frameRate = fps;
    }
    options.quality = valueOf(kQualities, json.value(QStringLiteral("quality")).toString(), options.quality);
    options.codec = valueOf(kCodecs, json.value(QStringLiteral("codec")).toString(), options.codec);
    return options;
}

namespace Mp4SizeEstimate {

qint64 bytes(int videoBitrate, bool hasAudio, qint64 durationMs)
{
    if (durationMs <= 0) return 0;
    const double bitsPerSecond = double(videoBitrate) + (hasAudio ? kAudioBitrate : 0);
    return qint64(bitsPerSecond / kBitsPerByte * (durationMs / kMsPerSecond) * kContainerOverhead);
}

int expectedVideoBitrate(int targetBitrate, bool outputHevc, const Source& source, const QSize& outputSize)
{
    if (targetBitrate <= 0 || source.fileBytes <= 0 || source.durationMs <= 0
        || source.videoSize.isEmpty() || outputSize.isEmpty()) {
        return targetBitrate;
    }
    const double total = double(source.fileBytes) * kBitsPerByte * kMsPerSecond / double(source.durationMs)
                         / kContainerOverhead;
    const double video = std::max(0.0, total - (source.hasAudio ? kAudioBitrate : 0));
    const double area = double(outputSize.width()) * outputSize.height()
                        / (double(source.videoSize.width()) * source.videoSize.height());
    double codecScale = 1.0;
    if (source.hevc != outputHevc) {
        codecScale = outputHevc ? Mp4ExportOptions::kHevcBitrateFactor
                                : 1.0 / Mp4ExportOptions::kHevcBitrateFactor;
    }
    return int(std::min(double(targetBitrate), std::ceil(video * area * codecScale)));
}

} // namespace Mp4SizeEstimate

} // namespace SnapTray
