#include "pinwindow/ForegroundCutoutRunner.h"

#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>

#include <utility>

namespace SnapTray {

namespace {

// One thread for every pin: segmentation holds hundreds of megabytes at
// large sizes, so jobs from several pins queue instead of running together.
// Deliberately never destroyed, so neither a pin closing nor the app quitting
// waits for a request in progress; its idle thread exits after the pool's
// expiry timeout.
QThreadPool* cutoutPool()
{
    static QThreadPool* pool = [] {
        auto* created = new QThreadPool();
        created->setMaxThreadCount(1);
        created->setServiceLevel(QThread::QualityOfService::High);
        return created;
    }();
    return pool;
}

} // namespace

ForegroundCutoutRunner::ForegroundCutoutRunner(ForegroundCutout::MaskProvider provider,
                                               QObject* parent)
    : QObject(parent)
    , m_provider(std::move(provider))
{
}

ForegroundCutoutRunner::~ForegroundCutoutRunner()
{
    cancel();
}

bool ForegroundCutoutRunner::isBusy() const
{
    return !m_watcher.isNull();
}

bool ForegroundCutoutRunner::start(const QImage& source, Completion onFinished)
{
    if (isBusy()) {
        return false;
    }

    auto cancelled = std::make_shared<std::atomic_bool>(false);
    auto* watcher = new QFutureWatcher<ForegroundCutout::Result>(this);
    m_watcher = watcher;
    m_cancelled = cancelled;
    m_completion = std::move(onFinished);
    connect(watcher, &QFutureWatcher<ForegroundCutout::Result>::finished, this,
        [this, watcher]() { deliver(watcher); });
    watcher->setFuture(QtConcurrent::run(cutoutPool(),
        [source, provider = m_provider, cancelled]() {
            if (cancelled->load()) {
                ForegroundCutout::Result skipped;
                skipped.status = ForegroundCutout::Status::Cancelled;
                return skipped;
            }
            return ForegroundCutout::run(source, provider);
        }));
    return true;
}

void ForegroundCutoutRunner::cancel()
{
    if (m_cancelled) {
        m_cancelled->store(true);
        m_cancelled.reset();
    }
    if (QFutureWatcher<ForegroundCutout::Result>* watcher = m_watcher) {
        watcher->disconnect(this);
        m_watcher.clear();
        watcher->deleteLater();
    }
    m_completion = Completion();
}

void ForegroundCutoutRunner::deliver(QFutureWatcher<ForegroundCutout::Result>* watcher)
{
    if (watcher != m_watcher) {
        return;
    }
    const ForegroundCutout::Result result = watcher->result();
    m_watcher.clear();
    m_cancelled.reset();
    watcher->deleteLater();
    // Cleared before calling, so the completion may start the next job.
    const Completion completion = std::exchange(m_completion, Completion());
    if (completion) {
        completion(result);
    }
}

} // namespace SnapTray
