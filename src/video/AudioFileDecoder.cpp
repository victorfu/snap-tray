#include "video/AudioFileDecoder.h"
#include "recording/AudioTracks.h"
#include <QFileInfo>
#include <QScopeGuard>
#include <QtEndian>

namespace SnapTray {
#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN) && !defined(SNAPTRAY_ENABLE_LINUX_RECORDING)
bool decodeAudioFile(const QString&, const AudioPcmSink&, QString& error)
{
    error = QStringLiteral("Audio decoding is unavailable in this build."); return false;
}
#endif
bool cacheRecordingAudio(const QString& videoPath, qint64 audioStartMs, QString& error)
{
    const QString path = videoPath + QLatin1String(AudioClipTrack::kMixedSuffix);
    AudioTrackReader existing;
    if (existing.open(path) && existing.frames() > 0) return true;
    const QString staging = path + QStringLiteral(".part");
    AudioTrackWriter writer(staging);
    qint64 at = qMax<qint64>(0, audioStartMs) * 48;
    bool ok = writer.open();
    if (ok) ok = decodeAudioFile(videoPath, [&](const QByteArray& pcm, std::optional<qint64> position) {
        const qint64 begin = position.value_or(at);
        const qint64 skip = qMax<qint64>(0, -begin);
        writer.write(pcm.mid(qMin<qint64>(pcm.size(), skip * AudioTracks::kBytesPerFrame)), qMax<qint64>(0, begin));
        at = begin + pcm.size() / AudioTracks::kBytesPerFrame;
        return !writer.failed();
    }, error);
    writer.close();
    ok = ok && !writer.failed();
    if (ok && QFileInfo::exists(path)) ok = QFile::remove(path);
    if (ok) ok = QFile::rename(staging, path);
    if (!ok) {
        QFile::remove(staging);
        if (error.isEmpty()) error = QStringLiteral("Cannot prepare the recording's original audio for preview.");
    }
    return ok;
}
std::optional<AudioClip> importAudioClip(const QString& input, const QString& videoPath, QString& error)
{
    const QFileInfo info(input);
    if (!info.isFile() || info.size() <= 0 || info.size() > AudioClipTrack::kMaxFileBytes) {
        error = QStringLiteral("Select an audio file smaller than 512 MB."); return {};
    }
    const QString path = videoPath + QLatin1String(AudioClipTrack::kSuffix);
    if (!QFileInfo::exists(path)) {
        AudioTrackWriter writer(path);
        if (!writer.open()) { error = QStringLiteral("Cannot create the imported audio sidecar."); return {}; }
        writer.close();
    }
    AudioTrackReader check;
    if (!check.open(path)) { error = QStringLiteral("The imported audio sidecar is damaged."); return {}; }
    const qint64 assetStart = check.frames();
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite) || !file.seek(file.size())) { error = file.errorString(); return {}; }
    const qint64 before = file.size();
    bool committed = false;
    const auto rollback = qScopeGuard([&] { if (!committed) file.resize(before); });
    qint64 frames = 0;
    const bool decoded = decodeAudioFile(input, [&](const QByteArray& pcm, std::optional<qint64>) {
        frames += pcm.size() / AudioTracks::kBytesPerFrame;
        if (frames > AudioClipTrack::kMaxDurationMs * 48) {
            error = QStringLiteral("Audio files must be at most 30 minutes long."); return false;
        }
        if (before + frames * 4 > AudioClipTrack::kMaxArchiveBytes) {
            error = QStringLiteral("This project's imported audio has reached the 4 GB limit."); return false;
        }
        if (pcm.size() % 4 || file.write(pcm) != pcm.size()) { error = file.errorString(); return false; }
        return true;
    }, error);
    if (!decoded || frames == 0 || !file.flush()) {
        if (error.isEmpty()) error = QStringLiteral("This audio format is not supported by the system decoder, or the file is empty.");
        return {};
    }
    committed = true;
    AudioClip clip;
    clip.name = info.fileName(); clip.assetFrame = assetStart; clip.assetFrames = frames;
    clip.endMs = frames / 48;
    return clip;
}
}
