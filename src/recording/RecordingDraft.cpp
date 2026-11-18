#include "recording/RecordingDraft.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include "recording/AudioTracks.h"

namespace SnapTray::RecordingDraft {
namespace {
constexpr qint64 kSampleBytes = 64 * 1024;
constexpr qint64 kMaxDraftBytes = 128 * 1024 * 1024;
constexpr int kVersion = 1;

QByteArray digest(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

QString sourceFingerprint(const QString& video)
{
    QFile file(video);
    const QFileInfo info(file);
    if (!info.isFile() || !file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(info.canonicalFilePath().toUtf8());
    hash.addData(QByteArray::number(info.size()) + ':' + QByteArray::number(info.lastModified().toMSecsSinceEpoch()));
    hash.addData(file.read(kSampleBytes));
    if (!file.seek(qMax<qint64>(0, info.size() - kSampleBytes))) return {};
    hash.addData(file.read(kSampleBytes));
    return file.error() == QFileDevice::NoError ? QString::fromLatin1(hash.result().toHex()) : QString();
}
}

QString pathFor(const QString& video) { return video + QStringLiteral(".edit-draft.json"); }
bool exists(const QString& video)
{
    const QFileInfo info(pathFor(video));
    return info.exists() || info.isSymLink();
}
bool remove(const QString& video) { return !exists(video) || QFile::remove(pathFor(video)); }

namespace {
std::optional<QJsonObject> pack(const Snapshot& snapshot, QJsonObject& images)
{
    PreviewEditState owned = snapshot.edits;
    QString imageId;
    if (!owned.appearance.imagePath.isEmpty()) {
        QFile image(owned.appearance.imagePath);
        if (image.open(QIODevice::ReadOnly) && image.size() <= kMaxDraftBytes / 2) {
            const QByteArray data = image.readAll();
            if (image.error() == QFileDevice::NoError && !QImage::fromData(data).isNull()) {
                imageId = QString::fromLatin1(digest(data));
                if (!images.contains(imageId)) images.insert(imageId, QString::fromLatin1(data.toBase64()));
            }
        }
        owned.appearance.imagePath.clear();
    }
    if (imageId.isEmpty() && owned.appearance.enabled
        && owned.appearance.background == RecordingAppearance::Background::Image) return std::nullopt;
    return QJsonObject{{"recipe", RecordingRecipe::toJson(owned, snapshot.choices)}, {"background", imageId}};
}

std::optional<QJsonObject> unpack(const QString& video, const QJsonObject& packed,
                                  const QJsonObject& images, const QString& assetDirectory)
{
    auto recipe = packed.value("recipe").toObject();
    auto edit = recipe.value("edit").toObject();
    auto appearance = edit.value("appearance").toObject();
    const auto state = RecordingRecipe::editState(recipe);
    if (!state || recipe.value("version").toInt() != RecordingRecipe::kVersion
        || state->redaction.toJson() != edit.value("redaction").toObject()
        || state->audioClips.toJson() != edit.value("audioClips").toObject()
        || !state->appearance.imagePath.isEmpty()) return std::nullopt;
    if (!state->audioClips.clips.empty()) {
        const QString path = video + QLatin1String(AudioClipTrack::kSuffix);
        AudioTrackReader audio;
        if (QFileInfo(path).isSymLink() || !audio.open(path)) return std::nullopt;
        for (const auto& clip : state->audioClips.clips) {
            if (clip.assetFrame > audio.frames() || clip.assetFrames > audio.frames() - clip.assetFrame)
                return std::nullopt;
        }
    }
    const QString id = packed.value("background").toString();
    if (!id.isEmpty()) {
        const QByteArray bytes = QByteArray::fromBase64(images.value(id).toString().toLatin1());
        if (assetDirectory.isEmpty() || id != QString::fromLatin1(digest(bytes))) return std::nullopt;
        const QString path = QDir(assetDirectory).filePath(id + QStringLiteral(".png"));
        if (!QFileInfo::exists(path)) {
            const QImage image = QImage::fromData(bytes);
            QSaveFile out(path);
            if (image.isNull() || !out.open(QIODevice::WriteOnly) || !image.save(&out, "PNG") || !out.commit())
                return std::nullopt;
        }
        appearance.insert("imagePath", path);
        edit.insert("appearance", appearance);
        recipe.insert("edit", edit);
    } else if (state->appearance.enabled && state->appearance.background == RecordingAppearance::Background::Image) {
        return std::nullopt;
    }
    return recipe;
}
}

bool write(const QString& video, const QString& identity, const Snapshot& current,
           const std::optional<Snapshot>& sessionBaseline)
{
    const QString fingerprint = sourceFingerprint(video);
    if (identity.isEmpty() || fingerprint.isEmpty() || QFileInfo(pathFor(video)).isSymLink()) return false;
    QJsonObject images;
    const auto edits = pack(current, images);
    if (!edits) return false;
    QJsonObject payload{{"identity", identity}, {"source", fingerprint}, {"current", *edits}};
    if (sessionBaseline) {
        const auto baseline = pack(*sessionBaseline, images);
        if (!baseline) return false;
        payload.insert("sessionBaseline", *baseline);
    }
    payload.insert("images", images);
    const QByteArray data = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    const QByteArray bytes = QJsonDocument(QJsonObject{
        {"version", kVersion}, {"sha256", QString::fromLatin1(digest(data))}, {"payload", payload}
    }).toJson(QJsonDocument::Compact);
    QSaveFile out(pathFor(video));
    if (bytes.size() > kMaxDraftBytes || !out.open(QIODevice::WriteOnly)) return false;
#ifdef Q_OS_UNIX
    // Session files can live in a shared temporary directory.
    if (!out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) return false;
#endif
    return out.write(bytes) == bytes.size() && out.commit();
}

std::optional<Recipes> read(const QString& video, const QString& identity, const QString& assetDirectory)
{
    QFile file(pathFor(video));
    if (QFileInfo(file).isSymLink() || !file.open(QIODevice::ReadOnly) || file.size() > kMaxDraftBytes)
        return std::nullopt;
    const auto envelope = QJsonDocument::fromJson(file.readAll()).object();
    const auto payload = envelope.value("payload").toObject();
    const QString fingerprint = sourceFingerprint(video);
    if (file.error() != QFileDevice::NoError || envelope.value("version").toInt() != kVersion
        || envelope.value("sha256").toString() != QString::fromLatin1(digest(QJsonDocument(payload).toJson(QJsonDocument::Compact)))
        || fingerprint.isEmpty() || payload.value("source").toString() != fingerprint
        || identity.isEmpty() || payload.value("identity").toString() != identity) return std::nullopt;
    const auto images = payload.value("images").toObject();
    const auto current = unpack(video, payload.value("current").toObject(), images, assetDirectory);
    if (!current) return std::nullopt;
    Recipes recipes{*current, {}};
    if (payload.contains("sessionBaseline")) {
        const auto baseline = unpack(video, payload.value("sessionBaseline").toObject(), images, assetDirectory);
        if (!baseline) return std::nullopt;
        recipes.sessionBaseline = *baseline;
    }
    return recipes;
}
} // namespace SnapTray::RecordingDraft
