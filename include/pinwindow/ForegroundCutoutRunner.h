#ifndef FOREGROUNDCUTOUTRUNNER_H
#define FOREGROUNDCUTOUTRUNNER_H

#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QPointer>

#include <atomic>
#include <functional>
#include <memory>

#include "pinwindow/ForegroundCutout.h"

namespace SnapTray {

// Runs one background removal at a time for a pin, off the GUI thread, and
// delivers its result on the thread that owns the runner (#363).
//
// All runners share a single worker thread, so several pins never segment
// at once. The worker gets an immutable QImage snapshot; a result is
// delivered only for the job still in flight, and never after cancel() or
// after the runner is gone. Neither waits for the worker: a cancelled job
// that has already started finishes on its own and is dropped.
class ForegroundCutoutRunner : public QObject
{
    Q_OBJECT

public:
    using Completion = std::function<void(const ForegroundCutout::Result&)>;

    explicit ForegroundCutoutRunner(ForegroundCutout::MaskProvider provider,
                                    QObject* parent = nullptr);
    ~ForegroundCutoutRunner() override;

    bool isBusy() const;

    // Returns false, without starting anything, while another job is in flight.
    [[nodiscard]] bool start(const QImage& source, Completion onFinished);

    // Drops the job in flight; its completion is never called. A job that
    // has not started yet is skipped.
    void cancel();

private:
    void deliver(QFutureWatcher<ForegroundCutout::Result>* watcher);

    ForegroundCutout::MaskProvider m_provider;
    QPointer<QFutureWatcher<ForegroundCutout::Result>> m_watcher;
    std::shared_ptr<std::atomic_bool> m_cancelled;
    Completion m_completion;
};

} // namespace SnapTray

#endif // FOREGROUNDCUTOUTRUNNER_H
