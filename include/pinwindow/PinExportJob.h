#ifndef PINEXPORTJOB_H
#define PINEXPORTJOB_H

#include <QColorSpace>
#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QThreadPool>

#include <functional>
#include <optional>

#include "beautify/BeautifySettings.h"
#include "utils/ImageSaveUtils.h"

namespace SnapTray {

// A pin's final export, run off the GUI thread (#353). Everything the worker
// needs is captured on the GUI thread first: the picture is an immutable
// QImage snapshot, so edits made while it is written are not part of it.
struct PinExportRequest {
    enum class Destination {
        UniqueFile, // automatic save: a new file name, never replacing a file
        ExactFile,  // the path chosen in the save dialog, replaced atomically
        Clipboard,  // only the composed picture is produced; the GUI copies it
    };

    QImage source;
    std::optional<BeautifySettings> beautify;
    // From exportColorSpaceForScreen, resolved on the GUI thread.
    QColorSpace screenColorSpace;
    Destination destination = Destination::UniqueFile;
    // UniqueFile only; its outputDir must be set.
    ImageSaveUtils::UniqueSaveSpec uniqueSave;
    // ExactFile only; must be set.
    QString filePath;
};

struct PinExportResult {
    bool success = false;
    // The source is null or could not be beautified (for example a picture
    // or one of its intermediates too large to allocate); nothing was written.
    bool renderFailed = false;
    // The composed picture with the source's device pixel ratio: the source
    // itself when not beautified (keeping any colour space it has), otherwise
    // the beautified picture, which is untagged.
    QImage image;
    // Clipboard only: the picture normalized as normalizeImageForExport does.
    QImage clipboardImage;
    QString filePath;
    QString renderWarning;
    ImageSaveUtils::Error error;
};

// Composes and writes one export. Uses only QImage and file APIs, so it is
// safe on any thread. A file is reported as saved only once it is published.
PinExportResult runPinExport(const PinExportRequest& request);

// Runs one export at a time for a pin and delivers its result on the thread
// that owns the runner. A result is delivered only for the job still in
// flight, and never after the runner is gone.
class PinExportRunner : public QObject
{
    Q_OBJECT

public:
    using Completion = std::function<void(const PinExportResult&)>;

    explicit PinExportRunner(QObject* parent = nullptr);
    // Waits for a job in flight without delivering it: a file being
    // published is finished rather than abandoned half-written.
    ~PinExportRunner() override;

    bool isBusy() const;

    // Returns false, without starting anything, while another job is in
    // flight; a burst of key presses never queues unbounded work.
    [[nodiscard]] bool start(PinExportRequest request, Completion onFinished);

    // Waits for the job in flight and delivers its result now. Used when
    // the pin closes, so its save is still reported.
    void finishNow();

    // Waits for the job in flight and drops its result. Used when the app
    // quits: the file is finished, but there is no UI left to report it to.
    void waitWithoutReporting();

private:
    void deliver(QFutureWatcher<PinExportResult>* watcher);

    // Its own single-thread pool at high quality of service, so an export the
    // user is waiting for runs on a performance core; on Apple silicon Qt's
    // global pool ran it noticeably slower than the GUI thread (2026-11).
    // Its idle thread exits after the pool's expiry timeout (30 s by default).
    QThreadPool m_pool;
    QPointer<QFutureWatcher<PinExportResult>> m_watcher;
    Completion m_completion;
};

} // namespace SnapTray

#endif // PINEXPORTJOB_H
