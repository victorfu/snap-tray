#include "recording/RecordingProject.h"

#include "recording/AudioTracks.h"
#include "recording/CursorTimeline.h"
#include "recording/KeyTimeline.h"
#include "recording/WindowTimelineSidecar.h"

#include <QDebug>
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
constexpr auto kThumbnailName = "thumbnail.png";
constexpr auto kManifestType = "snaptray-project";
constexpr int kManifestVersion = 1;

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

bool writeJson(const QString& path, const QJsonObject& object)
{
    if (auto callback = std::exchange(beforeManifestWrite(), {})) callback();
    if (std::exchange(failNextManifestWrite(), false)) return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    return file.commit();
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
            {QStringLiteral("recipe"), p.recipe}};
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
                           {QStringLiteral("audio"), s.audio.toJson()}};
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
    s.overlays = OverlayTrack::fromJson(e.value(QStringLiteral("overlays")).toObject());
    s.audio = AudioMixSettings::fromJson(e.value(QStringLiteral("audio")).toObject());
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
    return QDir(dir).filePath(QLatin1String(kThumbnailName));
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

QString root()
{
    if (!rootOverride().isEmpty()) return rootOverride();
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("RecordingProjects"));
}

void setRootForTests(const QString& path)
{
    rootOverride() = path;
}

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
            QLatin1String(AudioTracks::kSystemAudioSuffix)};
}

std::optional<RecordingProject> create(const QString& videoPath, bool copy, const QJsonObject& recipe,
                                       const QImage& thumbnail, qint64 durationMs, const QString& savedPath,
                                       const RecipePreparer& prepareRecipe)
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
    if (ok && !thumbnail.isNull()) thumbnail.save(project.thumbnailPath(), "PNG");
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
    return project;
}

std::optional<RecordingProject> load(const QString& dir)
{
    QFile file(QDir(dir).filePath(QLatin1String(kManifestName)));
    if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
    const QJsonObject m = QJsonDocument::fromJson(file.readAll()).object();
    if (m.value(QStringLiteral("type")).toString() != QLatin1String(kManifestType)) return std::nullopt;
    RecordingProject p;
    p.dir = dir;
    p.videoName = QFileInfo(m.value(QStringLiteral("video")).toString()).fileName(); // never a path out of the folder
    p.created = QDateTime::fromString(m.value(QStringLiteral("created")).toString(), Qt::ISODateWithMs);
    p.modified = QDateTime::fromString(m.value(QStringLiteral("modified")).toString(), Qt::ISODateWithMs);
    if (!p.modified.isValid()) p.modified = p.created;
    p.durationMs = qint64(m.value(QStringLiteral("durationMs")).toDouble(0));
    p.savedPath = m.value(QStringLiteral("savedPath")).toString();
    p.recipe = RecordingRecipe::migrate(m.value(QStringLiteral("recipe")).toObject());
    if (p.videoName.isEmpty() || !QFileInfo::exists(p.videoPath())) return std::nullopt;
    return p;
}

bool update(RecordingProject& project, const QJsonObject& recipe, const QString& savedPath)
{
    RecordingProject updated = project;
    updated.recipe = recipe;
    if (!savedPath.isEmpty()) updated.savedPath = savedPath;
    updated.modified = QDateTime::currentDateTime();
    if (!writeJson(QDir(project.dir).filePath(QLatin1String(kManifestName)), manifestOf(updated))) return false;
    project = std::move(updated);
    return true;
}

QList<RecordingProject> list()
{
    QList<RecordingProject> projects;
    const QDir dir(root());
    for (const QString& name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (auto project = load(dir.filePath(name))) projects.append(*project);
    }
    std::sort(projects.begin(), projects.end(),
              [](const RecordingProject& a, const RecordingProject& b) { return a.modified > b.modified; });
    return projects;
}

bool remove(const RecordingProject& project)
{
    // Only ever a folder inside the projects root.
    if (!contains(project.dir) || QDir::cleanPath(project.dir) == QDir::cleanPath(root())) return false;
    return QDir(project.dir).removeRecursively();
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
    for (const auto& project : list()) {
        if (project.modified.daysTo(now) >= days && remove(project)) ++removed;
    }
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
