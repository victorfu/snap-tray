#include "recording/RecordingSession.h"

#include <QImageReader>

#include "recording/WindowTimelineSidecar.h"
#include "recording/CursorTimeline.h"
#include "recording/KeyTimeline.h"
#include "recording/AudioTracks.h"

#include <QDir>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>

namespace SnapTray::RecordingSession {

namespace {

constexpr auto kManifestSuffix = ".session.json";
constexpr auto kLockSuffix = ".session.lock";
constexpr auto kManifestFilter = "SnapTray_Recording_*.session.json";
constexpr qint64 kMaxManifestBytes = 64 * 1024;

QString stateName(RecordingSessionInfo::State state)
{
    return state == RecordingSessionInfo::State::Finished ? QStringLiteral("finished")
                                                          : QStringLiteral("recording");
}

} // namespace

QString manifestPathFor(const QString& videoPath)
{
    return videoPath + QLatin1String(kManifestSuffix);
}

QString lockPathFor(const QString& videoPath)
{
    return videoPath + QLatin1String(kLockSuffix);
}

bool write(const RecordingSessionInfo& info, QString* error)
{
    QJsonObject object;
    object[QStringLiteral("version")] = info.version;
    object[QStringLiteral("sessionId")] = info.sessionId;
    object[QStringLiteral("state")] = stateName(info.state);
    // Stored by name: the manifest sits next to the video, so moving the
    // folder (or a different temp path) still finds it.
    object[QStringLiteral("video")] = QFileInfo(info.videoPath).fileName();
    object[QStringLiteral("format")] = info.format;
    object[QStringLiteral("width")] = info.frameSize.width();
    object[QStringLiteral("height")] = info.frameSize.height();
    object[QStringLiteral("frameRate")] = info.frameRate;
    object[QStringLiteral("audioRequired")] = info.audioRequired;
    object[QStringLiteral("intermediate")] = info.intermediate;
    object[QStringLiteral("fragmented")] = info.fragmented;
    object[QStringLiteral("cleanupPending")] = info.cleanupPending;
    object[QStringLiteral("startedAt")] = info.startedAt.toUTC().toString(Qt::ISODateWithMs);
    object[QStringLiteral("updatedAt")] = info.updatedAt.toUTC().toString(Qt::ISODateWithMs);
    object[QStringLiteral("recordedMs")] = QString::number(info.recordedMs);
    if (!info.initialCrop.isEmpty()) {
        object[QStringLiteral("initialCrop")] = QJsonArray{info.initialCrop.x(), info.initialCrop.y(),
                                                           info.initialCrop.width(), info.initialCrop.height()};
    }

    // Write-and-rename: a crash leaves the old manifest or the new one, never half.
    QSaveFile file(manifestPathFor(info.videoPath));
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

std::optional<RecordingSessionInfo> read(const QString& manifestPath, QString* error)
{
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot open manifest");
        return std::nullopt;
    }
    if (file.size() > kMaxManifestBytes) {
        if (error) *error = QStringLiteral("manifest too large");
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("corrupt manifest");
        return std::nullopt;
    }
    const QJsonObject object = document.object();
    RecordingSessionInfo info;
    info.version = object.value(QStringLiteral("version")).toInt(0);
    if (info.version < 1 || info.version > RecordingSessionInfo::kVersion) {
        if (error) *error = QStringLiteral("unsupported manifest version %1").arg(info.version);
        return std::nullopt;
    }
    const QString video = object.value(QStringLiteral("video")).toString();
    // Only a plain file name next to the manifest; never a path elsewhere.
    if (video.isEmpty() || video != QFileInfo(video).fileName()) {
        if (error) *error = QStringLiteral("invalid video name");
        return std::nullopt;
    }
    info.videoPath = QFileInfo(manifestPath).dir().filePath(video);
    if (manifestPathFor(info.videoPath) != QFileInfo(manifestPath).absoluteFilePath()
        && manifestPathFor(info.videoPath) != manifestPath) {
        if (error) *error = QStringLiteral("manifest does not match its video");
        return std::nullopt;
    }
    info.sessionId = object.value(QStringLiteral("sessionId")).toString();
    info.state = object.value(QStringLiteral("state")).toString() == QLatin1String("finished")
        ? RecordingSessionInfo::State::Finished
        : RecordingSessionInfo::State::Recording;
    info.format = object.value(QStringLiteral("format")).toString();
    info.frameSize = QSize(object.value(QStringLiteral("width")).toInt(),
                           object.value(QStringLiteral("height")).toInt());
    info.frameRate = object.value(QStringLiteral("frameRate")).toInt();
    info.audioRequired = object.value(QStringLiteral("audioRequired")).toBool();
    info.intermediate = object.value(QStringLiteral("intermediate")).toBool(true);
    info.fragmented = object.value(QStringLiteral("fragmented")).toBool();
    info.cleanupPending = object.value(QStringLiteral("cleanupPending")).toBool();
    info.startedAt = QDateTime::fromString(object.value(QStringLiteral("startedAt")).toString(), Qt::ISODateWithMs);
    info.updatedAt = QDateTime::fromString(object.value(QStringLiteral("updatedAt")).toString(), Qt::ISODateWithMs);
    info.recordedMs = object.value(QStringLiteral("recordedMs")).toString().toLongLong();
    const QJsonArray crop = object.value(QStringLiteral("initialCrop")).toArray();
    if (crop.size() == 4) {
        const QRect rect(crop[0].toInt(), crop[1].toInt(), crop[2].toInt(), crop[3].toInt());
        // Only what fits the recording it describes.
        if (rect.isValid() && !info.frameSize.isEmpty() && QRect(QPoint(0, 0), info.frameSize).contains(rect)) {
            info.initialCrop = rect;
        }
    }
    return info;
}

void remove(const QString& videoPath)
{
    QFile::remove(manifestPathFor(videoPath));
}

bool hasSession(const QString& videoPath)
{
    return QFileInfo::exists(manifestPathFor(videoPath));
}

DiscardResult discard(const QString& videoPath, const RemoveFile& removeFile)
{
    DiscardResult result;
    // Never enumerate the directory or follow paths from manifest contents.
    const QStringList materials{
        videoPath, WindowTimelineSidecar::pathFor(videoPath),
        CursorTimelineSidecar::pathFor(videoPath), KeyTimelineSidecar::pathFor(videoPath),
        AudioTracks::pathFor(videoPath, AudioTracks::Track::Microphone),
        AudioTracks::pathFor(videoPath, AudioTracks::Track::SystemAudio),
        videoPath + QLatin1String(AudioClipTrack::kSuffix), videoPath + QLatin1String(AudioClipTrack::kMixedSuffix),
        videoPath + QStringLiteral(".imports.json")};
    const QString manifest = manifestPathFor(videoPath);
    const auto exists = [](const QString& path) {
        const QFileInfo file(path);
        return file.exists() || file.isSymLink();
    };
    const auto collectRemaining = [&] {
        for (const auto& path : materials) {
            if (exists(path)) result.remainingPaths.append(path);
        }
        if (exists(manifest)) result.remainingPaths.append(manifest);
    };
    collectRemaining();
    if (result.remainingPaths.isEmpty()) {
        result.complete = true;
        return result;
    }
    const auto reportError = [&](const QString& path, const QString& error) {
        const QString detail = QStringLiteral("%1: %2").arg(path, error);
        result.errors.append(detail);
        qWarning().noquote() << "RecordingSession: discard failed:" << detail;
    };
    // QSaveFile follows an existing symlink. Never let a cleanup-intent write
    // replace a link target or a non-file in the shared temporary directory.
    const QFileInfo manifestInfo(manifest);
    if (manifestInfo.isSymLink() || (manifestInfo.exists() && !manifestInfo.isFile())) {
        reportError(manifest, QStringLiteral("manifest is not a regular, non-symlink file"));
        return result;
    }
    auto info = read(manifest).value_or(RecordingSessionInfo{});
    info.videoPath = videoPath;
    if (!info.cleanupPending) {
        info.cleanupPending = true;
        QString error;
        if (!write(info, &error)) {
            reportError(manifest, error);
            return result; // no deletion without a durable retry record
        }
    }
    result.intentPersisted = true;
    const auto removeOne = [&](const QString& path) {
        if (!exists(path)) return true;
        QString error;
        bool removed;
        if (removeFile) {
            removed = removeFile(path, &error);
        } else {
            QFile file(path);
            removed = file.remove();
            error = file.errorString();
        }
        if (!removed) reportError(path, error);
        return removed;
    };
    bool materialsRemoved = true;
    for (const auto& path : materials) {
        if (!removeOne(path)) materialsRemoved = false;
    }
    if (materialsRemoved) removeOne(manifest);
    result.remainingPaths.clear();
    collectRemaining();
    result.complete = result.errors.isEmpty() && result.remainingPaths.isEmpty();
    return result;
}

bool isAnimatedFormat(const QString& videoPath)
{
    const QString suffix = QFileInfo(videoPath).suffix().toLower();
    return suffix == QLatin1String("gif") || suffix == QLatin1String("webp");
}

ProbeResult probeFinishedAnimation(const QString& path)
{
    ProbeResult result;
    result.durationFromManifest = true;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() < 16) return result;
    const qint64 size = file.size();
    const QByteArray head = file.read(12);
    bool complete = false;
    if (head.startsWith("GIF8")) {
        // A GIF ends with its trailer byte.
        file.seek(size - 1);
        complete = file.read(1) == QByteArray(1, '\x3B');
    } else if (head.startsWith("RIFF") && head.mid(8, 4) == "WEBP") {
        // The RIFF header states the whole file's size.
        const auto* bytes = reinterpret_cast<const uchar*>(head.constData());
        const qint64 riffSize = qint64(bytes[4]) | qint64(bytes[5]) << 8 | qint64(bytes[6]) << 16
            | qint64(bytes[7]) << 24;
        complete = riffSize + 8 == size;
    }
    file.close();
    if (!complete) return result;
    QImageReader reader(path);
    result.decodable = reader.canRead() && !reader.read().isNull();
    return result;
}

ProbeResult probeForRecovery(const QString& path, const Probe& videoProbe)
{
    if (isAnimatedFormat(path)) return probeFinishedAnimation(path);
    return videoProbe ? videoProbe(path) : ProbeResult{};
}

QList<Recoverable> scan(const QString& directory, const Probe& probe, const IsActive& isActive)
{
    QList<Recoverable> found;
    const QDir dir(directory);
    const QFileInfoList manifests = dir.entryInfoList({QLatin1String(kManifestFilter)}, QDir::Files, QDir::Time);
    for (const QFileInfo& manifest : manifests) {
        const QString manifestPath = manifest.absoluteFilePath();
        const QString videoPath = manifestPath.left(manifestPath.size() - int(qstrlen(kManifestSuffix)));
        if (isActive && isActive(videoPath)) {
            continue;  // still recording, or open in a preview
        }
        // The active callback is only a hint: an owner can start immediately
        // after it returns. Hold the lock through reading, probing and orphan
        // cleanup so a concurrent discard cannot replace our manifest snapshot.
        QLockFile lock(lockPathFor(videoPath));
        lock.setStaleLockTime(0);
        Recoverable item;
        item.manifestPath = manifestPath;
        if (!lock.tryLock(0)) {
            if (lock.error() == QLockFile::LockFailedError) continue;
            item.info.videoPath = videoPath;
            item.accessBlocked = true;
            item.problem = QStringLiteral("cannot lock session (error %1)").arg(int(lock.error()));
            qWarning().noquote() << "RecordingSession:" << item.problem << lock.fileName();
            found.append(item);
            continue; // no probe or cleanup without ownership
        }
        QString error;
        const auto info = read(manifestPath, &error);
        if (!info) {
            // Do not delete what we cannot understand; let the user decide.
            item.info.videoPath = videoPath;
            item.problem = error;
            item.decodable = false;
            if (QFileInfo::exists(videoPath)) found.append(item);
            else QFile::remove(manifestPath);
            continue;
        }
        item.info = *info;
        if (info->cleanupPending) {
            found.append(item);
            continue;
        }
        if (!QFileInfo::exists(info->videoPath)) {
            remove(info->videoPath);  // nothing left to recover
            continue;
        }
        item.complete = info->state == RecordingSessionInfo::State::Finished;
        // Trust the file, not the manifest: decode what is really there.
        const ProbeResult result = probe ? probe(info->videoPath) : ProbeResult{};
        const qint64 durationMs = result.durationFromManifest ? info->recordedMs : result.durationMs;
        item.decodable = result.decodable && (result.durationFromManifest || durationMs > 0);
        item.durationMs = item.decodable ? qMax<qint64>(0, durationMs) : 0;
        item.audioMissing = item.decodable && info->audioRequired && !result.hasAudio;
        if (!item.decodable) {
            item.problem = info->fragmented
                ? QStringLiteral("no playable data")
                : QStringLiteral("not finished and not crash-safe");
        }
        found.append(item);
    }
    return found;
}

} // namespace SnapTray::RecordingSession
