#pragma once

#include "recording/RecordingProject.h"

namespace SnapTray::RecordingDraft {

// Owned by the same session/project as the source. A single atomic file holds
// the recipe and its background; imported audio stays in the owned PCM sidecar.
QString pathFor(const QString& video);
bool exists(const QString& video);
bool remove(const QString& video);
struct Snapshot {
    PreviewEditState edits;
    QJsonObject choices;
};
struct Recipes {
    QJsonObject current;
    QJsonObject sessionBaseline;
};
bool write(const QString& video, const QString& identity, const Snapshot& current,
           const std::optional<Snapshot>& sessionBaseline);
// Materialize the embedded background only in the caller's private temporary
// directory. Never resolve an external asset path from a recovery document.
std::optional<Recipes> read(const QString& video, const QString& identity,
                                const QString& assetDirectory);

} // namespace SnapTray::RecordingDraft
