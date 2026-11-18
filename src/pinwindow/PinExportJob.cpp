#include "pinwindow/PinExportJob.h"

#include "ImageColorSpaceHelper.h"
#include "beautify/BeautifyRenderer.h"

#include <QtConcurrent/QtConcurrentRun>

#include <utility>

namespace SnapTray {

PinExportResult runPinExport(const PinExportRequest& request)
{
    using Destination = PinExportRequest::Destination;

    PinExportResult result;
    if (request.destination == Destination::ExactFile) {
        result.filePath = request.filePath;
    }

    result.image = request.beautify
        ? BeautifyRenderer::applyToImage(request.source, *request.beautify)
        : request.source;
    if (result.image.isNull()) {
        result.renderFailed = true;
        return result;
    }

    switch (request.destination) {
    case Destination::Clipboard: {
        // As normalizeImageForExport, with the colour space resolved earlier.
        QImage normalized = result.image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        normalized.setDevicePixelRatio(1.0);
        result.clipboardImage = tagImageWithColorSpace(normalized, request.screenColorSpace);
        result.success = !result.clipboardImage.isNull();
        return result;
    }
    case Destination::UniqueFile: {
        const auto saved = ImageSaveUtils::saveImageUnique(
            tagImageWithColorSpace(result.image, request.screenColorSpace), request.uniqueSave);
        result.success = saved.success;
        result.filePath = saved.filePath;
        result.renderWarning = saved.renderWarning;
        result.error = saved.error;
        return result;
    }
    case Destination::ExactFile:
        result.success = ImageSaveUtils::saveImageAtomically(
            tagImageWithColorSpace(result.image, request.screenColorSpace),
            request.filePath, QByteArray(), &result.error);
        return result;
    }
    return result;
}

PinExportRunner::PinExportRunner(QObject* parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
    m_pool.setServiceLevel(QThread::QualityOfService::High);
}

PinExportRunner::~PinExportRunner()
{
    if (m_watcher) {
        m_watcher->disconnect(this);
        m_watcher->waitForFinished();
    }
}

bool PinExportRunner::isBusy() const
{
    return !m_watcher.isNull();
}

bool PinExportRunner::start(PinExportRequest request, Completion onFinished)
{
    if (isBusy()) {
        return false;
    }

    auto* watcher = new QFutureWatcher<PinExportResult>(this);
    m_watcher = watcher;
    m_completion = std::move(onFinished);
    connect(watcher, &QFutureWatcher<PinExportResult>::finished, this,
        [this, watcher]() { deliver(watcher); });
    watcher->setFuture(QtConcurrent::run(
        &m_pool, [request = std::move(request)]() { return runPinExport(request); }));
    return true;
}

void PinExportRunner::finishNow()
{
    if (!m_watcher) {
        return;
    }
    QFutureWatcher<PinExportResult>* watcher = m_watcher;
    // The queued finished signal must not deliver the same result again.
    watcher->disconnect(this);
    watcher->waitForFinished();
    deliver(watcher);
}

void PinExportRunner::deliver(QFutureWatcher<PinExportResult>* watcher)
{
    if (watcher != m_watcher) {
        return;
    }
    const PinExportResult result = watcher->result();
    m_watcher.clear();
    watcher->deleteLater();
    // Cleared before calling, so the completion may start the next job.
    const Completion completion = std::exchange(m_completion, Completion());
    if (completion) {
        completion(result);
    }
}

} // namespace SnapTray
