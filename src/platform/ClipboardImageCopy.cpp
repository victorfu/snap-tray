#include "PlatformFeatures.h"

#include "platform/ClipboardImageFile.h"
#include "settings/FileSettingsManager.h"
#include <QClipboard>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QMimeData>
#include <QPointer>
#include <QQueue>
#include <QScopedValueRollback>
#include <QtConcurrentRun>

namespace {
using Mode = FileSettingsManager::ClipboardImageMode;
using Result = PlatformFeatures::ClipboardCopyResult;

// Serialize file preparation through publication. A stale file releases its
// quota before the next worker starts, including when its caller has closed.
class FileCopyQueue {
public:
    void submit(std::function<void()> job)
    {
        if (m_busy) {
            m_waiting.enqueue(std::move(job));
            return;
        }
        m_busy = true;
        job();
    }

    void finished()
    {
        if (m_waiting.isEmpty()) {
            m_busy = false;
            return;
        }
        QMetaObject::invokeMethod(qApp, m_waiting.dequeue(), Qt::QueuedConnection);
    }

private:
    bool m_busy = false;
    QQueue<std::function<void()>> m_waiting;
};

FileCopyQueue fileCopies;

bool setClipboardPixels(const QImage& image)
{
    auto* clipboard = QGuiApplication::clipboard();
    if (!clipboard || image.isNull())
        return false;
    clipboard->setImage(image);
    return true;
}
}

QString PlatformFeatures::clipboardErrorMessage(ClipboardCopyResult result)
{
    return result == Result::Incomplete
        ? QCoreApplication::translate("Clipboard", "Copy incomplete. The clipboard may have changed. Try copying again.")
        : QCoreApplication::translate("PinWindow", "Copy failed");
}

void PlatformFeatures::cleanClipboardImageFiles()
{
    SnapTray::cleanClipboardImageStores([]() -> std::optional<QList<QUrl>> {
        auto* clipboard = QGuiApplication::clipboard();
        const auto* data = clipboard ? clipboard->mimeData() : nullptr;
        auto urls = SnapTray::referencedClipboardFiles(data);
        if (!urls && clipboardIsDefinitelyEmpty())
            return QList<QUrl>{};
        return urls;
    });
}

bool PlatformFeatures::copyImageToClipboardPersistently(const QImage& image, QString* error) const
{
    const Mode mode = FileSettingsManager::instance().clipboardImageMode();
    if (mode == Mode::ImageOnly)
        return copyImageOnlyPersistently(image);
    cleanClipboardImageFiles();
    auto file = SnapTray::prepareClipboardImageFile(image, SnapTray::clipboardImageDirectory());
    const auto result = file ? publishClipboardImageFile(image, *file, mode == Mode::ImageAndFile, true)
                             : Result::Failed;
    if (error && result != Result::Success)
        *error = clipboardErrorMessage(result);
    return result == Result::Success;
}

bool PlatformFeatures::copyImageToClipboardForGui(const QImage& image) const
{
    const auto mode = FileSettingsManager::instance().clipboardImageMode();
    if (mode == Mode::ImageOnly)
        return setClipboardPixels(image);
    cleanClipboardImageFiles();
    auto file = SnapTray::prepareClipboardImageFile(image, SnapTray::clipboardImageDirectory());
    return file && publishClipboardImageFile(image, *file, mode == Mode::ImageAndFile, false) == Result::Success;
}

void PlatformFeatures::copyImageToClipboardForGuiAsync(const QImage& image, QObject* context,
    ClipboardCopyCompletion completion, quint64 reservedRequest) const
{
    const auto request = reservedRequest ? reservedRequest : reserveClipboardRequest();
    QObject* target = context ? context : QCoreApplication::instance();
    if (!target) {
        if (completion)
            completion(Result::Failed);
        return;
    }
    const Mode mode = FileSettingsManager::instance().clipboardImageMode();
    QMetaObject::invokeMethod(target, [this, image, target, request, reservedRequest, mode, completion] {
        if ((reservedRequest || mode != Mode::ImageOnly) && !isLatestClipboardRequest(request)) {
            if (completion)
                completion(Result::Superseded);
            return;
        }
        if (mode == Mode::ImageOnly) {
            const QScopedValueRollback<bool> ownWrite(s_writingImageCopy, true);
            const auto result = setClipboardPixels(image) ? Result::Success : Result::Failed;
            s_clipboardRevision = nativeClipboardRevision();
            if (completion)
                completion(result);
            return;
        }

        fileCopies.submit([this, image, target = QPointer<QObject>(target), mode, request, completion] {
            if (!target || !isLatestClipboardRequest(request)) {
                if (target && completion)
                    completion(Result::Superseded);
                fileCopies.finished();
                return;
            }
            cleanClipboardImageFiles();
            using File = std::shared_ptr<SnapTray::ClipboardImageFile>;
            auto* watcher = new QFutureWatcher<File>(qApp);
            QObject::connect(watcher, &QFutureWatcher<File>::finished, qApp,
                [this, watcher, image, target, mode, request, completion] {
                    auto file = watcher->future().takeResult();
                    watcher->deleteLater();
                    auto result = Result::Superseded;
                    if (target && isLatestClipboardRequest(request)) {
                        const QScopedValueRollback<bool> ownWrite(s_writingImageCopy, true);
                        result = file ? publishClipboardImageFile(image, *file, mode == Mode::ImageAndFile, false)
                                      : Result::Failed;
                        s_clipboardRevision = nativeClipboardRevision();
                    }
                    file.reset();
                    if (target && completion)
                        completion(result);
                    fileCopies.finished();
                });
            watcher->setFuture(QtConcurrent::run([image, directory = SnapTray::clipboardImageDirectory()] {
                return SnapTray::prepareClipboardImageFile(image, directory);
            }));
        });
    }, Qt::QueuedConnection);
}
