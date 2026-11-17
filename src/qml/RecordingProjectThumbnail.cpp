#include "qml/RecordingProjectThumbnail.h"
#include "recording/RecordingFrameFilter.h"
#include "video/IVideoFrameReader.h"

#include <QCoreApplication>
#include <QFutureWatcher>
#include <QHash>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <new>

namespace SnapTray {
namespace {
const QSize kThumbnailSize(320, 180);
}

QImage renderProjectThumbnail(const RecordingProject& project, IVideoFrameReader& reader)
{
    // Unknown recipe versions may contain effects this build cannot apply.
    if (project.recipe.value(QStringLiteral("version")).toInt() != RecordingRecipe::kVersion) return {};
    const auto state = RecordingRecipe::editState(project.recipe);
    if (!state || !reader.load(project.videoPath())) return {};
    const qint64 end = state->trimEnd < 0 ? reader.duration() : std::min(state->trimEnd, reader.duration());
    const TimelineMap map = state->timeline.map(state->trimStart, end);
    if (map.isEmpty() || map.outputDurationMs() <= 0) return {};
    const qint64 middle = map.outputToSource(map.outputDurationMs() / 2);
    QImage frame = reader.frameAt(middle);
    if (frame.isNull() || frame.size() != reader.videoSize()) return {};
    const QRect crop = state->crop.isEmpty() ? frame.rect() : state->crop;
    if (crop.isEmpty() || !frame.rect().contains(crop)) return {};
    // The shared export filter assumes cropped input but source coordinates.
    if (crop != frame.rect()) frame = frame.copy(crop);
    if (frame.isNull()) return {};
    std::shared_ptr<const CursorTimeline> cursor;
    if (auto data = CursorTimelineSidecar::read(project.videoPath()))
        cursor = std::make_shared<const CursorTimeline>(std::move(*data));
    std::shared_ptr<const KeyTimeline> keys;
    if (auto data = KeyTimelineSidecar::read(project.videoPath()))
        keys = std::make_shared<const KeyTimeline>(std::move(*data));
    auto filter = recordingFrameFilter(*state, reader.videoSize(), map, cursor, keys);
    // Match export's conservative interval, bounded to the kept part by the
    // filter. The next native frame also covers long VFR holds.
    const qint64 nextUs = reader.nextFrameTimeMicroseconds();
    const qint64 frameEnd = nextUs < 0 ? map.sourceEndMs() : (nextUs + 999) / 1000;
    filter(frame, middle, frameEnd);
    if (frame.isNull()) return {};
    return frame.scaled(kThumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

void requestProjectThumbnail(const RecordingProject& project)
{
    // One attempt per committed revision per process, including failures.
    // This avoids retry loops when the library refreshes after another job.
    // A new save or application restart allows a failed decode to be retried.
    static QHash<QString, QString> attempted;
    const QString revision = project.thumbnailPath();
    if (!project.thumbnailUrl().isEmpty() || attempted.value(project.dir) == revision) return;
    attempted.insert(project.dir, revision);
    auto* watcher = new QFutureWatcher<QImage>(QCoreApplication::instance());
    QObject::connect(watcher, &QFutureWatcher<QImage>::finished, watcher, [watcher, project] {
        const QImage image = watcher->result();
        RecordingProjects::publishThumbnail(project, image);
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([project] {
        try {
            const auto reader = IVideoFrameReader::create();
            return reader ? renderProjectThumbnail(project, *reader) : QImage();
        } catch (const std::bad_alloc&) {
            return QImage();
        }
    }));
}
} // namespace SnapTray
