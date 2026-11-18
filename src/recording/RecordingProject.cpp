#include "recording/RecordingProject.h"
#include "recording/RecordingDraft.h"

#include "recording/AudioTracks.h"
#include "recording/CursorTimeline.h"
#include "recording/KeyTimeline.h"
#include "recording/WindowTimelineSidecar.h"

#include <QDebug>
#include <QHash>
#include <QSignalBlocker>
#include <QCryptographicHash>
#include <QUuid>
#include <QUrl>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QLockFile>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <filesystem>
#include <utility>

namespace SnapTray {

namespace {

constexpr auto kRecipeType = "snaptray-recipe";
constexpr auto kManifestName = "project.json";
constexpr auto kPreviousManifestName = "manifest.prev.json";
bool failManifestRead = false;
QString openProjectDir;
QString pendingDraftDir;
constexpr auto kManifestType = "snaptray-project";
constexpr int kManifestVersion = 1;

// Application-thread cache of the last loaded/committed revision. Role reads
// never reparse manifests; refresh and publication are the disk boundaries.
QHash<QString, QString>& currentThumbnails()
{
    static QHash<QString, QString> paths;
    return paths;
}

QJsonObject thumbnailRecipe(QJsonObject recipe)
{
    recipe.remove(QStringLiteral("export")); // output format/quality do not alter the poster
    return recipe;
}

QString& rootOverride()
{
    static QString path;
    return path;
}

QJsonArray rectToJson(const QRect& r)
{
    return {r.x(), r.y(), r.width(), r.height()};
}

QRect rectFromJson(const QJsonValue& value)
{
    const QJsonArray a = value.toArray();
    if (a.size() != 4) return {};
    return QRect(a[0].toInt(), a[1].toInt(), a[2].toInt(), a[3].toInt());
}

bool& failNextManifestWrite()
{
    static bool fail = false;
    return fail;
}

std::function<void()>& beforeManifestWrite()
{
    static std::function<void()> callback;
    return callback;
}

bool& failNextProjectCleanup()
{
    static bool fail = false;
    return fail;
}

const QString kPendingPrefix = QStringLiteral(".creating-");
const QString kPendingSuffix = QStringLiteral(".pending");

QString pendingMarker(const QDir& root, const QString& id)
{
    return root.filePath(kPendingPrefix + id + kPendingSuffix);
}

void retryIncompleteProjects(const QDir& projects)
{
    // Only this transaction protocol's markers authorize cleanup. Legacy
    // manifest-less folders may contain the only copy of a recording.
    static const QRegularExpression idPattern(QStringLiteral("^[0-9]{8}-[0-9]{6}-[0-9]{3}-[0-9a-f]{1,4}$"));
    for (const QString& marker : projects.entryList({kPendingPrefix + '*' + kPendingSuffix}, QDir::Files | QDir::Hidden)) {
        const QString id = marker.mid(kPendingPrefix.size(), marker.size() - kPendingPrefix.size() - kPendingSuffix.size());
        if (!idPattern.match(id).hasMatch()) continue;
        const QString markerPath = projects.filePath(marker);
        QLockFile lock(markerPath + QStringLiteral(".lock"));
        lock.setStaleLockTime(0);
        if (!lock.tryLock(0)) continue; // another create() still owns it
        if (QFileInfo(projects.filePath(id)).isSymLink()) continue;
        QDir dir(projects.filePath(id));
        // A crash after commit must never turn a valid project into cleanup.
        if (QFileInfo::exists(dir.filePath(QLatin1String(kManifestName)))
            || !dir.exists() || dir.removeRecursively()) {
            QFile::remove(markerPath);
        }
    }
}

bool validManifest(const QJsonObject& object)
{
    return object.value(QStringLiteral("type")).toString() == QLatin1String(kManifestType)
        && object.value(QStringLiteral("version")).toInt(-1) == kManifestVersion
        && object.value(QStringLiteral("recipe")).isObject();
}

enum class ManifestRead { Valid, Missing, Corrupt, IoError, UnsupportedVersion };
ManifestRead readManifest(const QString& path, QJsonObject& object)
{
    QFile file(path);
    if (!file.exists()) return ManifestRead::Missing;
    if (!file.open(QIODevice::ReadOnly)) return ManifestRead::IoError;
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) return ManifestRead::IoError;
    QJsonParseError error;
    object = QJsonDocument::fromJson(bytes, &error).object();
    if (error.error == QJsonParseError::NoError
        && object.value(QStringLiteral("type")).toString() == QLatin1String(kManifestType)
        && object.value(QStringLiteral("version")).toDouble(-1) > kManifestVersion)
        return ManifestRead::UnsupportedVersion;
    return error.error == QJsonParseError::NoError && validManifest(object)
        ? ManifestRead::Valid : ManifestRead::Corrupt;
}

bool commitJson(const QString& path, const QJsonObject& object)
{
    QSaveFile file(path);
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

bool writeJson(const QString& path, const QJsonObject& object)
{
    if (auto callback = std::exchange(beforeManifestWrite(), {})) callback();
    if (std::exchange(failNextManifestWrite(), false)) return false;
    QJsonObject previous;
    const auto status = readManifest(path, previous);
    if (status == ManifestRead::IoError || status == ManifestRead::UnsupportedVersion) return false;
    if (status == ManifestRead::Valid
        && !commitJson(QFileInfo(path).dir().filePath(QLatin1String(kPreviousManifestName)), previous)) return false;
    // A corrupt primary must never replace the last valid backup.
    return commitJson(path, object);
}

QJsonObject manifestOf(const RecordingProject& p)
{
    return {{QStringLiteral("type"), QLatin1String(kManifestType)},
            {QStringLiteral("version"), kManifestVersion},
            {QStringLiteral("video"), p.videoName},
            {QStringLiteral("created"), p.created.toString(Qt::ISODateWithMs)},
            {QStringLiteral("modified"), p.modified.toString(Qt::ISODateWithMs)},
            {QStringLiteral("durationMs"), double(p.durationMs)},
            {QStringLiteral("savedPath"), p.savedPath},
            {QStringLiteral("recoveredFromProjectId"), p.recoveredFromProjectId},
            {QStringLiteral("recipe"), p.recipe},
            {QStringLiteral("thumbnailGeneration"), p.thumbnailGeneration}};
}

} // namespace

namespace RecordingRecipe {

QJsonObject toJson(const PreviewEditState& s, const QJsonObject& exportChoices)
{
    const QJsonObject edit{{QStringLiteral("crop"), rectToJson(s.crop)},
                           {QStringLiteral("trimStart"), double(s.trimStart)},
                           {QStringLiteral("trimEnd"), double(s.trimEnd)},
                           {QStringLiteral("cursor"), s.cursor.toJson()},
                           {QStringLiteral("zoom"), s.zoom.toJson()},
                           {QStringLiteral("redaction"), s.redaction.toJson()},
                           {QStringLiteral("appearance"), s.appearance.toJson()},
                           {QStringLiteral("timeline"), s.timeline.toJson()},
                           {QStringLiteral("keys"), s.keys.toJson()},
                           {QStringLiteral("overlays"), s.overlays.toJson()},
                           {QStringLiteral("shapes"), s.shapes.toJson()},
                           {QStringLiteral("audio"), s.audio.toJson()},
                           {QStringLiteral("audioClips"), s.audioClips.toJson()}};
    return {{QStringLiteral("type"), QLatin1String(kRecipeType)},
            {QStringLiteral("version"), kVersion},
            {QStringLiteral("edit"), edit},
            {QStringLiteral("export"), exportChoices}};
}

QJsonObject migrate(const QJsonObject& recipe)
{
    // Version 0 (development builds): the edit fields at the top level.
    if (!recipe.contains(QStringLiteral("version")) && recipe.contains(QStringLiteral("trimStart"))) {
        QJsonObject edit = recipe;
        QJsonObject exportChoices = edit.take(QStringLiteral("export")).toObject();
        return {{QStringLiteral("type"), QLatin1String(kRecipeType)},
                {QStringLiteral("version"), kVersion},
                {QStringLiteral("edit"), edit},
                {QStringLiteral("export"), exportChoices}};
    }
    return recipe;
}

std::optional<PreviewEditState> editState(const QJsonObject& input)
{
    const QJsonObject recipe = migrate(input);
    if (recipe.value(QStringLiteral("type")).toString() != QLatin1String(kRecipeType)) return std::nullopt;
    // A newer version is read for what it shares with this one.
    const QJsonObject e = recipe.value(QStringLiteral("edit")).toObject();
    PreviewEditState s;
    s.crop = rectFromJson(e.value(QStringLiteral("crop")));
    s.trimStart = std::max<qint64>(0, qint64(e.value(QStringLiteral("trimStart")).toDouble(0)));
    s.trimEnd = qint64(e.value(QStringLiteral("trimEnd")).toDouble(-1));
    if (s.trimEnd >= 0 && s.trimEnd <= s.trimStart) s.trimEnd = -1;
    s.cursor = CursorEffectSettings::fromJson(e.value(QStringLiteral("cursor")).toObject());
    s.zoom = ZoomTrack::fromJson(e.value(QStringLiteral("zoom")).toObject());
    s.redaction = RedactionTrack::fromJson(e.value(QStringLiteral("redaction")).toObject());
    s.appearance = RecordingAppearance::fromJson(e.value(QStringLiteral("appearance")).toObject());
    s.timeline = TimelineEdit::fromJson(e.value(QStringLiteral("timeline")).toObject());
    s.keys = KeyOverlaySettings::fromJson(e.value(QStringLiteral("keys")).toObject());
    s.shapes = ShapeTrack::fromJson(e.value(QStringLiteral("shapes")).toObject());
    s.overlays = OverlayTrack::fromJson(e.value(QStringLiteral("overlays")).toObject());
    s.audio = AudioMixSettings::fromJson(e.value(QStringLiteral("audio")).toObject());
    s.audioClips = AudioClipTrack::fromJson(e.value(QStringLiteral("audioClips")).toObject());
    return s;
}

QJsonObject exportChoices(const QJsonObject& recipe)
{
    return migrate(recipe).value(QStringLiteral("export")).toObject();
}

} // namespace RecordingRecipe

QString RecordingProject::videoPath() const
{
    return QDir(dir).filePath(videoName);
}

QString RecordingProject::thumbnailPath() const
{
    if (m_thumbnailPath.isEmpty() || m_thumbnailRecipe != recipe || m_thumbnailGeneration != thumbnailGeneration) {
        // Legacy thumbnail.png never participates. Cache the digest on this
        // loaded snapshot; export-only choices do not change poster pixels.
        const QByteArray revision = QCryptographicHash::hash(
            QJsonDocument(thumbnailRecipe(recipe)).toJson(QJsonDocument::Compact) + thumbnailGeneration.toUtf8(),
            QCryptographicHash::Sha256).toHex();
        m_thumbnailPath = QStringLiteral("thumbnail-v1-%1.png").arg(QString::fromLatin1(revision));
        m_thumbnailRecipe = recipe;
        m_thumbnailGeneration = thumbnailGeneration;
    }
    return QDir(dir).filePath(m_thumbnailPath);
}

QString RecordingProject::thumbnailUrl() const
{
    if (unreadable || RecordingProjects::hasDraft(*this)) return {};
    const QString path = thumbnailPath();
    if (currentThumbnails().value(dir) != path || !QFileInfo(path).isFile()) return {};
    return QUrl::fromLocalFile(path).toString();
}

QString RecordingProject::title() const
{
    if (!savedPath.isEmpty()) return QFileInfo(savedPath).fileName();
    return QLocale().toString(created, QLocale::ShortFormat);
}

qint64 RecordingProject::sizeBytes() const
{
    qint64 total = 0;
    QDirIterator it(dir, QDir::Files);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

namespace RecordingProjects {

RecordingProjectEvents* events()
{
    static RecordingProjectEvents notifier;
    return &notifier;
}

bool publishThumbnail(const RecordingProject& snapshot, const QImage& image)
{
    const auto current = load(snapshot.dir);
    if (image.isNull() || !current || current->thumbnailPath() != snapshot.thumbnailPath()) return false;
    if (QFileInfo(snapshot.thumbnailPath()).isFile()) return true; // immutable URL contents
    // Never create a directory here: a deleted project must stay deleted.
    QSaveFile file(snapshot.thumbnailPath());
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit()) return false;
    emit events()->thumbnailChanged(snapshot.dir);
    return true;
}

QString root()
{
    if (!rootOverride().isEmpty()) return rootOverride();
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("RecordingProjects"));
}

void setRootForTests(const QString& path)
{
    rootOverride() = path;
    openProjectDir.clear();
    pendingDraftDir.clear();
    failManifestRead = false;
    currentThumbnails().clear();
}

void setOpenProjectDir(const QString& dir) { openProjectDir = dir; }

std::unique_ptr<QLockFile> lockEditor(const QString& dir)
{
    const QFileInfo info(dir);
    if (!info.isDir() || info.isSymLink()
        || info.absoluteDir().canonicalPath() != QDir(root()).canonicalPath()) return {};
    auto lock = std::make_unique<QLockFile>(info.absoluteFilePath() + QStringLiteral(".edit.lock"));
    lock->setStaleLockTime(0);
    if (!lock->tryLock()) return {};
    return lock;
}

void setPendingDraft(const QString& dir, bool pending)
{
    if (pending == (dir == pendingDraftDir)) return;
    pendingDraftDir = pending ? dir : QString();
    emit events()->thumbnailChanged(dir);
}

bool hasDraft(const RecordingProject& project)
{
    return project.dir == pendingDraftDir || RecordingDraft::exists(project.videoPath());
}

void failNextManifestReadForTests() { failManifestRead = true; }

void failNextManifestWriteForTests()
{
    failNextManifestWrite() = true;
}

void beforeNextManifestWriteForTests(std::function<void()> callback)
{
    beforeManifestWrite() = std::move(callback);
}

void failNextProjectCleanupForTests()
{
    failNextProjectCleanup() = true;
}

QStringList sidecarSuffixes()
{
    return {QLatin1String(WindowTimelineSidecar::kSuffix), QLatin1String(CursorTimelineSidecar::kSuffix),
            QLatin1String(KeyTimelineSidecar::kSuffix), QLatin1String(AudioTracks::kMicrophoneSuffix),
            QLatin1String(AudioTracks::kSystemAudioSuffix), QLatin1String(AudioClipTrack::kSuffix), QLatin1String(AudioClipTrack::kMixedSuffix), QStringLiteral(".imports.json")};
}

std::optional<RecordingProject> create(const QString& videoPath, bool copy, const QJsonObject& recipe,
                                       const QImage& thumbnail, qint64 durationMs, const QString& savedPath,
                                       const RecipePreparer& prepareRecipe, const QString& recoveredFromProjectId)
{
    if (!QFileInfo::exists(videoPath)) return std::nullopt;
    QDir projects(root());
    if (!projects.mkpath(QStringLiteral("."))) return std::nullopt;
    retryIncompleteProjects(projects);
    const QDateTime now = QDateTime::currentDateTime();
    const QString id = now.toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")) + QLatin1Char('-')
        + QString::number(QRandomGenerator::global()->bounded(0x10000), 16);
    const QString markerPath = pendingMarker(projects, id);
    QLockFile creationLock(markerPath + QStringLiteral(".lock"));
    creationLock.setStaleLockTime(0);
    if (!creationLock.tryLock(0) || !projects.mkdir(id)) return std::nullopt;
    QFile marker(markerPath);
    if (!marker.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        projects.rmdir(id);
        return std::nullopt;
    }
    marker.close();
    RecordingProject project;
    project.dir = projects.filePath(id);
    const QString suffix = QFileInfo(videoPath).suffix();
    project.videoName = QStringLiteral("recording.") + (suffix.isEmpty() ? QStringLiteral("mp4") : suffix);
    project.created = project.modified = now;
    project.durationMs = durationMs;
    project.savedPath = savedPath;
    project.recipe = recipe;
    project.recoveredFromProjectId = recoveredFromProjectId;
    project.thumbnailGeneration = QUuid::createUuid().toString(QUuid::WithoutBraces);
    // No source is removed before the complete recipe and all assets have
    // committed. A failed save can retry using exactly the same source paths,
    // without relying on a rollback move that could itself fail.
    bool ok = true;
    if (prepareRecipe) {
        const auto prepared = prepareRecipe(project.dir, recipe);
        ok = prepared.has_value();
        if (prepared) project.recipe = *prepared;
    }
    QStringList sources;
    const auto transfer = [copy, &sources](const QString& from, const QString& to) {
        bool linked = false;
        if (!copy && !QFileInfo(from).isSymLink()) {
            // A second name keeps the original path retryable without copying
            // media on the same filesystem. QFile::link is a symlink on Unix,
            // so use the C++17 hard-link API with UTF-8 path conversion.
            std::error_code error;
            std::filesystem::create_hard_link(std::filesystem::u8path(from.toUtf8().constData()),
                                              std::filesystem::u8path(to.toUtf8().constData()), error);
            linked = !error;
        }
        // Cross-volume/unsupported filesystems retain the previous copy path.
        // Explicit copy mode must remain independent, never a shared inode.
        if (!linked && !QFile::copy(from, to)) return false;
        sources.append(from);
        return true;
    };
    ok = ok && transfer(videoPath, project.videoPath());
    for (const QString& sidecar : sidecarSuffixes()) {
        if (ok && QFileInfo::exists(videoPath + sidecar)) ok = transfer(videoPath + sidecar, project.videoPath() + sidecar);
    }
    // The only commit point: no partially prepared project is published.
    ok = ok && writeJson(QDir(project.dir).filePath(QLatin1String(kManifestName)), manifestOf(project));
    if (!ok) {
        qWarning() << "RecordingProjects: could not create a project";
        if (!std::exchange(failNextProjectCleanup(), false) && QDir(project.dir).removeRecursively()) {
            QFile::remove(markerPath);
        } else {
            // The marker lives outside the folder so partial recursive cleanup
            // cannot erase the evidence needed to retry on the next save.
            qWarning() << "RecordingProjects: could not remove an incomplete project; cleanup will retry";
        }
        return std::nullopt;
    }
    QFile::remove(markerPath); // the committed manifest also protects this directory
    if (!copy) {
        for (const QString& source : sources) {
            if (!QFile::remove(source)) {
                qWarning() << "RecordingProjects: could not remove a committed project's temporary source";
            }
        }
    }
    currentThumbnails().insert(project.dir, project.thumbnailPath());
    // A preparer may change visual edits as well as asset paths. A supplied
    // poster belongs to the input recipe and cannot stand in for new edits.
    if (!thumbnail.isNull() && thumbnailRecipe(project.recipe) == thumbnailRecipe(recipe))
        publishThumbnail(project, thumbnail);
    emit events()->changed();
    return project;
}

std::optional<RecordingProject> recoverSource(const RecordingProject& damaged, qint64 durationMs)
{
    if (!damaged.unreadable || !damaged.sourceRecoverable || !contains(damaged.dir)) return std::nullopt;
    const QString sourceId = QFileInfo(damaged.dir).fileName();
    for (const auto& project : list()) {
        if (!project.unreadable && project.recoveredFromProjectId == sourceId) return project;
    }
    return create(damaged.videoPath(), true, RecordingRecipe::toJson({}, {}), {}, durationMs, {}, {}, sourceId);
}

std::optional<RecordingProject> load(const QString& dir)
{
    currentThumbnails().remove(dir);
    QJsonObject m;
    auto status = std::exchange(failManifestRead, false) ? ManifestRead::IoError
        : readManifest(QDir(dir).filePath(QLatin1String(kManifestName)), m);
    if (status == ManifestRead::IoError) {
        qWarning() << "RecordingProjects: cannot read project.json";
        return std::nullopt;
    }
    if (status == ManifestRead::UnsupportedVersion) {
        qWarning() << "RecordingProjects: unsupported manifest version; leaving the project untouched";
        return std::nullopt;
    }
    const bool recovered = status != ManifestRead::Valid;
    if (recovered) {
        status = readManifest(QDir(dir).filePath(QLatin1String(kPreviousManifestName)), m);
        if (status != ManifestRead::Valid) {
            qWarning() << "RecordingProjects: no readable manifest";
            return std::nullopt;
        }
        qWarning() << "RecordingProjects: recovered manifest.prev.json";
    }
    RecordingProject p;
    p.recoveredFromBackup = recovered;
    p.dir = dir;
    p.videoName = QFileInfo(m.value(QStringLiteral("video")).toString()).fileName(); // never a path out of the folder
    p.created = QDateTime::fromString(m.value(QStringLiteral("created")).toString(), Qt::ISODateWithMs);
    p.modified = QDateTime::fromString(m.value(QStringLiteral("modified")).toString(), Qt::ISODateWithMs);
    if (!p.modified.isValid()) p.modified = p.created;
    p.durationMs = qint64(m.value(QStringLiteral("durationMs")).toDouble(0));
    p.savedPath = m.value(QStringLiteral("savedPath")).toString();
    p.recoveredFromProjectId = m.value(QStringLiteral("recoveredFromProjectId")).toString();
    p.thumbnailGeneration = m.value(QStringLiteral("thumbnailGeneration")).toString();
    p.recipe = RecordingRecipe::migrate(m.value(QStringLiteral("recipe")).toObject());
    if (p.videoName.isEmpty() || !QFileInfo::exists(p.videoPath())) return std::nullopt;
    currentThumbnails().insert(dir, p.thumbnailPath());
    return p;
}

bool update(RecordingProject& project, const QJsonObject& recipe, const QString& savedPath)
{
    const auto committed = load(project.dir);
    if (!committed) return false;
    const bool changed = thumbnailRecipe(committed->recipe) != thumbnailRecipe(recipe);
    RecordingProject next = *committed;
    next.recipe = recipe;
    if (!savedPath.isEmpty()) next.savedPath = savedPath;
    next.modified = QDateTime::currentDateTime();
    if (changed) next.thumbnailGeneration = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!writeJson(QDir(next.dir).filePath(QLatin1String(kManifestName)), manifestOf(next))) return false;
    next.recoveredFromBackup = false;
    project = next;
    currentThumbnails().insert(project.dir, project.thumbnailPath());
    if (changed) {
        const QDir dir(project.dir);
        for (const QString& name : dir.entryList({QStringLiteral("thumbnail*.png")}, QDir::Files))
            QFile::remove(dir.filePath(name));
    }
    emit events()->changed();
    return true;
}

QList<RecordingProject> list()
{
    QList<RecordingProject> projects;
    const QDir dir(root());
    for (const QString& name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString path = dir.filePath(name);
        if (QFileInfo(path).isSymLink()) continue;
        // Uncommitted create transactions are never library entries.
        if (QFileInfo::exists(pendingMarker(dir, name))) continue;
        if (auto project = load(path)) projects.append(*project);
        else {
            RecordingProject damaged;
            damaged.dir = path;
            damaged.unreadable = true;
            damaged.created = damaged.modified = QFileInfo(path).lastModified();
            const QDir folder(path);
            const auto videos = folder.entryList({QStringLiteral("recording.mp4"), QStringLiteral("recording.gif"),
                                                  QStringLiteral("recording.webp")}, QDir::Files | QDir::NoSymLinks);
            if (!videos.isEmpty()) damaged.videoName = videos.front();
            QJsonObject ignored;
            const auto primary = readManifest(folder.filePath(QLatin1String(kManifestName)), ignored);
            const auto backup = readManifest(folder.filePath(QLatin1String(kPreviousManifestName)), ignored);
            damaged.sourceRecoverable = !damaged.videoName.isEmpty() && !RecordingDraft::exists(damaged.videoPath())
                && primary != ManifestRead::IoError && backup != ManifestRead::IoError
                && primary != ManifestRead::UnsupportedVersion && backup != ManifestRead::UnsupportedVersion;
            projects.append(damaged);
        }
    }
    std::sort(projects.begin(), projects.end(),
              [](const RecordingProject& a, const RecordingProject& b) { return a.modified > b.modified; });
    return projects;
}

static bool removeProject(const RecordingProject& project, bool automatic)
{
    auto lock = lockEditor(project.dir);
    if (!lock || (automatic && hasDraft(project))) return false;
    // Only ever a folder inside the projects root.
    if (!contains(project.dir) || QDir::cleanPath(project.dir) == QDir::cleanPath(root())) return false;
    const bool removed = QDir(project.dir).removeRecursively();
    if (removed) {
        currentThumbnails().remove(project.dir);
        emit events()->changed();
    }
    return removed;
}

bool remove(const RecordingProject& project)
{
    return removeProject(project, false);
}

qint64 totalBytes()
{
    qint64 total = 0;
    for (const auto& project : list()) total += project.sizeBytes();
    return total;
}

int removeOlderThan(int days, const QDateTime& now)
{
    if (days <= 0) return 0;
    int removed = 0;
    {
        const QSignalBlocker batch(events());
        for (const auto& project : list()) {
            if (project.unreadable || project.dir == openProjectDir || RecordingDraft::exists(project.videoPath())) continue;
            if (project.modified.daysTo(now) >= days && removeProject(project, true)) ++removed;
        }
    }
    if (removed) emit events()->changed();
    return removed;
}

bool contains(const QString& path)
{
    const QString base = QDir::cleanPath(QFileInfo(root()).absoluteFilePath());
    const QString target = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    return target.startsWith(base + QLatin1Char('/'));
}

} // namespace RecordingProjects

} // namespace SnapTray
