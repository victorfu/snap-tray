#include "recording/AudioClipTrack.h"
#include <QJsonArray>
#include <cmath>

namespace SnapTray {
bool AudioClip::operator==(const AudioClip& o) const
{
    return name == o.name && assetFrame == o.assetFrame && assetFrames == o.assetFrames
        && startMs == o.startMs && endMs == o.endMs && offsetMs == o.offsetMs
        && gainDb == o.gainDb && fadeInMs == o.fadeInMs && fadeOutMs == o.fadeOutMs && loop == o.loop;
}
QJsonObject AudioClipTrack::toJson() const
{
    QJsonArray list;
    for (const auto& c : clips) list.append(QJsonObject{
        {"name", c.name}, {"assetFrame", c.assetFrame}, {"assetFrames", c.assetFrames},
        {"startMs", c.startMs}, {"endMs", c.endMs}, {"offsetMs", c.offsetMs},
        {"gainDb", c.gainDb}, {"fadeInMs", c.fadeInMs}, {"fadeOutMs", c.fadeOutMs}, {"loop", c.loop}});
    return {{"clips", list}};
}
AudioClipTrack AudioClipTrack::fromJson(const QJsonObject& json)
{
    AudioClipTrack track;
    constexpr qint64 maxTimelineMs = 24LL * 60 * 60 * 1000;
    for (const auto& value : json.value("clips").toArray()) {
        if (track.clips.size() >= 128) break;
        const auto j = value.toObject();
        AudioClip c;
        c.name = j.value("name").toString().left(256);
        c.assetFrame = j.value("assetFrame").toInteger(-1);
        c.assetFrames = j.value("assetFrames").toInteger();
        c.startMs = j.value("startMs").toInteger(-1);
        c.endMs = j.value("endMs").toInteger();
        c.offsetMs = j.value("offsetMs").toInteger();
        c.gainDb = j.value("gainDb").toDouble();
        c.fadeInMs = j.value("fadeInMs").toInteger(5);
        c.fadeOutMs = j.value("fadeOutMs").toInteger(5);
        c.loop = j.value("loop").toBool();
        if (c.assetFrame < 0 || c.assetFrames <= 0 || c.assetFrames > kMaxDurationMs * 48
            || c.assetFrame > kMaxArchiveBytes / 4 - c.assetFrames
            || c.startMs < 0 || c.endMs <= c.startMs || c.endMs > maxTimelineMs
            || c.offsetMs < 0 || c.offsetMs >= (c.assetFrames + 47) / 48
            || !std::isfinite(c.gainDb) || c.gainDb < -24 || c.gainDb > 12
            || c.fadeInMs < 0 || c.fadeOutMs < 0
            || c.fadeInMs > c.endMs - c.startMs || c.fadeOutMs > c.endMs - c.startMs) continue;
        track.clips.push_back(c);
    }
    return track;
}
}
