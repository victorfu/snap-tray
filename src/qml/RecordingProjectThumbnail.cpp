#include "qml/RecordingProjectThumbnail.h"
#include "recording/RecordingFrameFilter.h"
#include "video/IVideoFrameReader.h"

#include <QCoreApplication>
#include <QFutureWatcher>
#include <QHash>
#include <QDebug>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <new>

namespace SnapTray {
namespace {
const QSize kThumbnailSize(320, 180);
}

QImage renderProjectThumbnail(const RecordingProject& project, IVideoFrameReader& reader,
                              const std::atomic_bool* cancelled)
{
    // Unknown recipe versions may contain effects this build cannot apply.
    if (project.recipe.value(QStringLiteral("version")).toInt() != RecordingRecipe::kVersion) return {};
    const auto state = RecordingRecipe::editState(project.recipe);
    if (!state || !reader.load(project.videoPath())) return {};
    const qint64 end = state->trimEnd < 0 ? reader.duration() : std::min(state->trimEnd, reader.duration());
    const TimelineMap map = state->timeline.map(state->trimStart, end);
    if (map.isEmpty() || map.outputDurationMs() <= 0) return {};
    const qint64 middle = map.outputToSource(map.outputDurationMs() / 2);
    // Native readers decode sequentially. Yield cancellation opportunities
    // between bounded timestamp steps rather than decoding to the midpoint in
    // one call. A native load/frame read itself is not interruptible.
    constexpr qint64 kDecodeStepMs = 1000;
    QImage frame;
    for (qint64 time = 0; ; time = std::min(time + kDecodeStepMs, middle)) {
        if (cancelled && cancelled->load()) return {};
        frame = reader.frameAt(time);
        if (frame.isNull() || time == middle) break;
    }
    if (cancelled && cancelled->load()) return {};
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

struct RecordingProjectThumbnailQueue::State {
    struct Job {
        RecordingProject project;
        int attempts = 0;
    };
    QThreadPool pool;
    Renderer renderer;
    QList<Job> pending;
    QHash<QString, QString> requested;
    QString activeDir;
    std::shared_ptr<std::atomic_bool> cancelled;
    bool stopped = false;
};

RecordingProjectThumbnailQueue::RecordingProjectThumbnailQueue(QObject* parent, Renderer renderer)
    : QObject(parent), m_state(std::make_unique<State>())
{
    m_state->pool.setMaxThreadCount(1);
    m_state->renderer = renderer ? std::move(renderer) : Renderer([](const RecordingProject& project,
                                                                  const std::atomic_bool& cancelled) {
        const auto reader = IVideoFrameReader::create();
        return reader ? renderProjectThumbnail(project, *reader, &cancelled) : QImage();
    });
    if (auto* app = QCoreApplication::instance())
        connect(app, &QCoreApplication::aboutToQuit, this, [this] { shutdown(); });
}

RecordingProjectThumbnailQueue::~RecordingProjectThumbnailQueue()
{
    shutdown();
    m_state->pool.waitForDone();
}

void RecordingProjectThumbnailQueue::shutdown()
{
    m_state->stopped = true;
    m_state->pending.clear();
    if (m_state->cancelled) m_state->cancelled->store(true);
}

void RecordingProjectThumbnailQueue::request(const RecordingProject& project)
{
    auto& s = *m_state;
    const QString revision = project.thumbnailPath();
    if (s.stopped || RecordingProjects::hasDraft(project) || !project.thumbnailUrl().isEmpty() || s.requested.value(project.dir) == revision) return;
    s.requested.insert(project.dir, revision);
    s.pending.removeIf([&](const State::Job& job) { return job.project.dir == project.dir; });
    if (s.activeDir == project.dir && s.cancelled) s.cancelled->store(true);
    // Known unsupported recipes cannot produce a safe image. Do not decode or
    // retry them, and never fall back to the unedited legacy poster.
    if (project.recipe.value(QStringLiteral("version")).toInt() != RecordingRecipe::kVersion
        || !RecordingRecipe::editState(project.recipe)) return;
    s.pending.append({project, 0});
    // A create followed by an immediate save coalesces before work starts.
    QTimer::singleShot(0, this, [this] { startNext(); });
}

void RecordingProjectThumbnailQueue::startNext()
{
    auto& s = *m_state;
    if (s.stopped || s.cancelled || s.pending.isEmpty()) return;
    auto job = s.pending.takeFirst();
    ++job.attempts;
    s.activeDir = job.project.dir;
    const auto cancelled = std::make_shared<std::atomic_bool>(false);
    s.cancelled = cancelled;
    auto* watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, job, cancelled] {
        const QImage image = watcher->result();
        watcher->deleteLater();
        auto& s = *m_state;
        s.cancelled.reset();
        s.activeDir.clear();
        if (!s.stopped && !cancelled->load()) {
            const auto current = RecordingProjects::load(job.project.dir);
            if (current && RecordingProjects::hasDraft(*current)) {
                s.requested.remove(job.project.dir);
            } else if (current && current->thumbnailPath() == job.project.thumbnailPath()
                && !RecordingProjects::publishThumbnail(job.project, image)) {
                qWarning() << "RecordingProjects: thumbnail generation/publication failed, attempt" << job.attempts
                           << "for" << job.project.dir;
                constexpr int kMaxAttempts = 3;
                constexpr int kRetryDelayMs = 1000;
                if (job.attempts < kMaxAttempts) {
                    QTimer::singleShot(kRetryDelayMs, this, [this, job] {
                        auto& s = *m_state;
                        if (!s.stopped && s.requested.value(job.project.dir) == job.project.thumbnailPath()) {
                            s.pending.append(job);
                            startNext();
                        }
                    });
                }
            }
        }
        startNext();
    });
    watcher->setFuture(QtConcurrent::run(&s.pool, [job, cancelled, renderer = s.renderer] {
        try {
            return cancelled->load() ? QImage() : renderer(job.project, *cancelled);
        } catch (const std::bad_alloc&) {
            return QImage();
        }
    }));
}

void requestProjectThumbnail(const RecordingProject& project)
{
    static auto* queue = new RecordingProjectThumbnailQueue(QCoreApplication::instance());
    queue->request(project);
}
} // namespace SnapTray
