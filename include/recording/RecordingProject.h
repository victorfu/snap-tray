#pragma once

#include "recording/PreviewEditHistory.h"

#include <QDateTime>
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

// Editable recording projects (#191). Off by default: after a save the
// temporary recording and its sidecars are deleted, as before. With "Keep
// editable projects" on, they move into a project folder instead, with the
// edit recipe, so the recording can be opened, changed and exported again:
//
//   <projects root>/<id>/recording.<ext>   the full-screen source (unmasked!)
//                         recording.<ext>.cursor.jsonl ...   its sidecars
//                         project.json   version, dates, recipe, saved file
//                         thumbnail.png
//
// The projects root is separate from the temporary recordings that crash
// recovery (#157) looks after, so neither lifecycle ever deletes the
// other's files.
namespace SnapTray {

namespace RecordingRecipe {
constexpr int kVersion = 1;
// The edits and export choices, versioned. Unknown keys are ignored.
QJsonObject toJson(const PreviewEditState& state, const QJsonObject& exportChoices);
// Reads any known version (older ones are migrated); nullopt only when the
// object is not a recipe at all.
std::optional<PreviewEditState> editState(const QJsonObject& recipe);
QJsonObject exportChoices(const QJsonObject& recipe);
// Brings an older recipe to kVersion.
QJsonObject migrate(const QJsonObject& recipe);
} // namespace RecordingRecipe

struct RecordingProject {
    QString dir;
    QString videoName;   // "recording.mp4"
    QDateTime created;
    QDateTime modified;
    qint64 durationMs = 0;
    QString savedPath;   // the last file exported from it
    QJsonObject recipe;

    QString videoPath() const;
    QString thumbnailPath() const;
    QString title() const; // the saved file's name, or the date
    qint64 sizeBytes() const;
};

namespace RecordingProjects {

// Under the app's local data; tests point it elsewhere.
QString root();
void setRootForTests(const QString& path);
// Tests: make the next create() or update() fail at its manifest write.
void failNextManifestWriteForTests();
void beforeNextManifestWriteForTests(std::function<void()> callback);
void failNextProjectCleanupForTests();

// Every sidecar a recording can have, by suffix of its video path.
QStringList sidecarSuffixes();

// Moves the recording and its sidecars into a new project, or copies them
// (copy: the source stays where it is). Moves stage hard links when supported,
// falling back to copies across volumes; explicit copies stay independent. Originals
// are removed only after the final manifest commits. nullopt leaves the
// sources intact. prepareRecipe, when supplied, prepares owned assets in
// the new folder before that commit; nullopt aborts and removes the folder.
using RecipePreparer = std::function<std::optional<QJsonObject>(const QString& projectDir, const QJsonObject& recipe)>;
std::optional<RecordingProject> create(const QString& videoPath, bool copy, const QJsonObject& recipe,
                                       const QImage& thumbnail, qint64 durationMs, const QString& savedPath,
                                       const RecipePreparer& prepareRecipe = {});
std::optional<RecordingProject> load(const QString& dir);
bool update(RecordingProject& project, const QJsonObject& recipe, const QString& savedPath);
QList<RecordingProject> list(); // newest first
bool remove(const RecordingProject& project);
qint64 totalBytes();
// Removes projects not changed for `days` (0 = never); returns how many.
int removeOlderThan(int days, const QDateTime& now = QDateTime::currentDateTime());
// True when the path is inside the projects root (its files are a project's).
bool contains(const QString& path);

} // namespace RecordingProjects

} // namespace SnapTray
