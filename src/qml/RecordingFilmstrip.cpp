#include "qml/RecordingFilmstrip.h"

#include "qml/RecordingPreviewBackend.h"
#include "video/IVideoFrameReader.h"

#include <QPainter>
#include <QtConcurrent/QtConcurrentRun>
#include <utility>

namespace {
constexpr int kTileWidth = 56;
constexpr int kMaxTiles = 64;
const QSize kSampleSize(112, 56);
}

RecordingFilmstrip::RecordingFilmstrip(QQuickItem* parent) : QQuickPaintedItem(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(60);
    connect(&m_timer, &QTimer::timeout, this, &RecordingFilmstrip::request);
    connect(this, &QQuickItem::widthChanged, this, [this] {
        if (density() != m_density) schedule();
    });
    connect(&m_sourceWatcher, &QFileSystemWatcher::fileChanged, this, [this] {
        // The editor's geometry and masks describe the originally opened media.
        m_sourceChanged = true;
        invalidate();
    });
}

RecordingFilmstrip::~RecordingFilmstrip()
{
    releaseReader();
}

void RecordingFilmstrip::releaseReader()
{
    // Discard and project saves may unlink or move the source on Windows.
    // A cancelled worker still owns its native reader until the call returns.
    m_timer.stop();
    m_released = true;
    ++m_generation;
    if (m_worker) {
        m_worker->cancel();
        m_worker->waitForFinished();
    }
}

QObject* RecordingFilmstrip::backend() const { return m_backend.data(); }

void RecordingFilmstrip::setBackend(QObject* object)
{
    auto* backend = qobject_cast<RecordingPreviewBackend*>(object);
    if (backend == m_backend) return;
    if (m_backend) disconnect(m_backend, nullptr, this, nullptr);
    if (!m_sourceWatcher.files().isEmpty()) m_sourceWatcher.removePaths(m_sourceWatcher.files());
    m_backend = backend;
    m_released = false;
    m_sourceChanged = false;
    if (backend) {
        m_sourceChanged = !m_sourceWatcher.addPath(backend->videoPath());
        for (const auto signal : {&RecordingPreviewBackend::timelineChanged, &RecordingPreviewBackend::masksChanged,
                                  &RecordingPreviewBackend::videoSizeChanged, &RecordingPreviewBackend::cropRectChanged})
            connect(backend, signal, this, &RecordingFilmstrip::invalidate);
        connect(backend, &RecordingPreviewBackend::projectChanged, this, [this] {
            if (m_backend && (m_backend->projectLoading() || m_visible.isEmpty())) invalidate();
        });
        connect(backend, &RecordingPreviewBackend::saveRequested, this, &RecordingFilmstrip::releaseReader);
        connect(backend, &RecordingPreviewBackend::closed, this, &RecordingFilmstrip::releaseReader);
        connect(backend, &RecordingPreviewBackend::processingChanged, this, [this] {
            if (!m_backend->isProcessing() && (m_released || m_visible.isEmpty())) {
                m_released = false;
                schedule();
            }
        });
        connect(backend, &QObject::destroyed, this, &RecordingFilmstrip::invalidate);
    }
    invalidate();
    emit backendChanged();
}

int RecordingFilmstrip::density() const
{
    // Powers of two keep sampling levels stable during small window resizes.
    if (width() <= 0) return 0;
    int tiles = 1;
    while (tiles < kMaxTiles && tiles * kTileWidth < width()) tiles *= 2;
    return tiles;
}

void RecordingFilmstrip::setHasTiles(bool available)
{
    if (m_hasTiles == available) return;
    m_hasTiles = available;
    emit hasTilesChanged();
}

void RecordingFilmstrip::invalidate()
{
    m_cache.clear();
    schedule();
}

void RecordingFilmstrip::schedule()
{
    ++m_generation;
    if (m_worker) m_worker->cancel();
    m_visible.clear();
    setHasTiles(false);
    m_density = density();
    update(); // remove the previous revision before asynchronous work starts
    m_timer.start();
}

void RecordingFilmstrip::request()
{
    if (m_worker || m_released || !m_backend || m_backend->isProcessing() || m_backend->projectLoading() || m_sourceChanged || !m_density) return;
    m_duration = m_backend->duration();
    const auto map = m_backend->timelineMap();
    const QSize size = m_backend->videoSize();
    if (m_duration <= 0 || size.isEmpty() || map.isEmpty()) return;

    // The editor ruler is in source time, including deleted parts. Intersect
    // each grid cell with its sampled kept segment so images never span a cut.
    for (int i = 0; i < m_density; ++i) {
        const qint64 start = m_duration * i / m_density;
        const qint64 end = m_duration * (i + 1) / m_density;
        const qint64 sample = map.nextShownSourceMs(start);
        if (sample < start || sample >= end) continue;
        const int part = map.segmentAt(sample);
        if (part >= 0) m_visible.append({sample, qMin(end, map.segments()[part].endMs)});
    }
    QList<Interval> missing;
    for (const auto& interval : m_visible)
        if (!m_cache.contains(interval)) missing.append(interval);
    setHasTiles(missing.size() < m_visible.size());
    update();
    if (missing.isEmpty()) return;
    const quint64 generation = m_generation;
    auto* worker = new QFutureWatcher<Tile>(this);
    m_worker = worker;
    connect(worker, &QFutureWatcher<Tile>::resultReadyAt, this, [this, worker, generation](int index) {
        if (generation != m_generation) return;
        const auto tile = worker->resultAt(index);
        m_cache.insert(tile.first, new QImage(tile.second), tile.second.sizeInBytes());
        setHasTiles(true);
        update();
    });
    connect(worker, &QFutureWatcher<Tile>::finished, this, [this, worker, generation] {
        worker->deleteLater();
        m_worker = nullptr;
        if (generation != m_generation && !m_timer.isActive()) request();
    });
    worker->setFuture(QtConcurrent::run([path = m_backend->videoPath(), intervals = std::move(missing),
            size, crop = m_backend->cropRect(), masks = m_backend->redaction()](QPromise<Tile>& results) {
        auto reader = IVideoFrameReader::create();
        if (results.isCanceled() || !reader || !reader->load(path) || reader->videoSize() != size) return;
        for (const auto& interval : intervals) {
            if (results.isCanceled()) return;
            QImage frame = reader->sampleAt(interval.first);
            if (frame.size() != size) continue;
            masks.apply(frame, interval.first, frame.rect(), interval.second);
            if (!crop.isEmpty()) {
                if (!frame.rect().contains(crop)) continue;
                frame = frame.copy(crop);
            }
            auto image = frame.scaled(kSampleSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            if (!image.isNull()) results.addResult(Tile{interval, image});
        }
    }));
}

void RecordingFilmstrip::paint(QPainter* painter)
{
    if (m_duration <= 0) return;
    const qreal scale = width() / m_duration;
    for (const auto& interval : m_visible) {
        const auto* image = m_cache.object(interval);
        if (!image) continue;
        const QRectF cell(interval.first * scale, 0, (interval.second - interval.first) * scale, height());
        QSizeF fitted = image->size();
        fitted.scale(cell.size(), Qt::KeepAspectRatio);
        QRectF dest(QPointF(), fitted);
        dest.moveCenter(cell.center());
        painter->drawImage(dest, *image);
    }
}
