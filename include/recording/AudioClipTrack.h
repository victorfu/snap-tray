#pragma once
#include <QJsonObject>
#include <QString>
#include <vector>

namespace SnapTray {
// All imported assets live in one append-only PCM sidecar. Keeping unused
// regions makes undo and crash recovery independent of the original files.
struct AudioClip {
    QString name;
    qint64 assetFrame = 0, assetFrames = 0; // offset and length in 48 kHz PCM frames
    qint64 startMs = 0, endMs = 0; // placement on the recording source timeline
    qint64 offsetMs = 0; // time skipped from the imported asset
    double gainDb = 0;
    qint64 fadeInMs = 5, fadeOutMs = 5;
    bool loop = false;
    qint64 durationMs() const;
    bool operator==(const AudioClip& o) const;
};
struct AudioClipTrack {
    static constexpr size_t kMaxClips = 128;
    static constexpr auto kSuffix = ".imports.pcm";
    static constexpr auto kMixedSuffix = ".mixed.pcm";
    static constexpr qint64 kMaxFileBytes = 512LL * 1024 * 1024;
    static constexpr qint64 kMaxDurationMs = 30 * 60 * 1000;
    static constexpr qint64 kMaxArchiveBytes = 4LL * 1024 * 1024 * 1024;
    std::vector<AudioClip> clips;
    bool isEmpty() const { return clips.empty(); }
    bool operator==(const AudioClipTrack& o) const { return clips == o.clips; }
    bool operator!=(const AudioClipTrack& o) const { return !(*this == o); }
    QJsonObject toJson() const;
    static AudioClipTrack fromJson(const QJsonObject& json);
};
}
