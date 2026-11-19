#include "qml/HistoryBackend.h"

#include "ImageColorSpaceHelper.h"
#include "PinWindow.h"
#include "history/HistoryReplayDocument.h"
#include "PinWindowManager.h"
#include "PlatformFeatures.h"
#include "pinwindow/PinWindowPlacement.h"
#include "qml/HistoryModel.h"
#include "qml/ImageExportDialogController.h"
#include "qml/QmlToast.h"
#include "settings/FileSettingsManager.h"
#include "utils/FilenameTemplateEngine.h"
#include "utils/ImageSaveUtils.h"
#include "utils/NativeFileDialogUtils.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QGuiApplication>
#include <QScreen>
#include <QUrl>

namespace {

QString historyText(const char* sourceText)
{
    return QCoreApplication::translate("HistoryWindow", sourceText);
}

QString saveErrorDetail(const ImageSaveUtils::Error& error)
{
    if (error.stage.isEmpty()) {
        return error.message.isEmpty() ? QStringLiteral("Unknown error") : error.message;
    }
    if (error.message.isEmpty()) {
        return error.stage;
    }
    return QStringLiteral("%1: %2").arg(error.stage, error.message);
}

} // namespace

namespace SnapTray {

HistoryBackend::HistoryBackend(HistoryModel* model,
                               PinWindowManager* pinWindowManager,
                               std::function<bool(const QString&)> startReplay,
                               QWindow* hostWindow,
                               QObject* parent)
    : QObject(parent)
    , m_model(model)
    , m_pinWindowManager(pinWindowManager)
    , m_startReplay(std::move(startReplay))
    , m_hostWindow(hostWindow)
{
}

void HistoryBackend::refresh()
{
    if (m_model) {
        m_model->refresh();
    }
}

void HistoryBackend::edit(int index)
{
    if (!m_model || !m_startReplay) {
        return;
    }

    const HistoryEntry entry = m_model->entryAt(index);
    if (!entry.replayAvailable) {
        showError(historyText("Edit Failed"),
                  entry.isFromNewerVersion()
                      ? historyText("This history item was saved by a newer version of SnapTray.")
                      : historyText("This history item cannot be replayed."));
        return;
    }

    if (!m_startReplay(entry.id)) {
        // Tell a damaged item apart from a capture that could not start now.
        const auto current = HistoryStore::loadEntry(entry.id);
        const bool damaged = current && !loadHistoryReplayDocument(*current);
        showError(historyText("Edit Failed"),
                  damaged
                      ? historyText("This history item is missing data or damaged and can't be reopened.")
                      : historyText("Failed to start history replay."));
        return;
    }

    emit closeRequested();
}

void HistoryBackend::pin(int index)
{
    if (!m_model || !m_pinWindowManager) {
        return;
    }

    const HistoryEntry entry = m_model->entryAt(index);
    QPixmap pixmap;
    if (!loadPixmap(entry, &pixmap)) {
        showError(historyText("Pin Failed"),
                  historyText("Failed to load history image."));
        return;
    }

    QScreen* screen = QGuiApplication::primaryScreen();
    const PinWindowPlacement placement = computeInitialPinWindowPlacement(
        pixmap,
        screen ? screen->availableGeometry() : QRect());
    // Zoom before the first show so an oversized pin is created on the right screen's DPI.
    m_pinWindowManager->createPinWindow(pixmap, placement.position, true, placement.zoomLevel);

    emit closeRequested();
}

void HistoryBackend::copy(int index)
{
    if (!m_model) {
        return;
    }

    QPixmap pixmap;
    if (!loadPixmap(m_model->entryAt(index), &pixmap)) {
        showError(historyText("Copy Failed"),
                  historyText("Failed to load history image."));
        return;
    }

    QScreen* exportScreen = QGuiApplication::primaryScreen();
    const QImage clipboardImage = normalizeImageForExport(pixmap.toImage(), exportScreen);
    PlatformFeatures::instance().copyImageToClipboardForGuiAsync(clipboardImage, qApp,
        [](PlatformFeatures::ClipboardCopyResult result) {
            if (result == PlatformFeatures::ClipboardCopyResult::Failed
                || result == PlatformFeatures::ClipboardCopyResult::Incomplete)
                SnapTray::QmlToast::screenToast().showToast(SnapTray::QmlToast::Level::Error,
                    PlatformFeatures::clipboardErrorMessage(result));
        });

    emit closeRequested();
}

void HistoryBackend::saveAs(int index)
{
    if (!m_model) {
        return;
    }

    const HistoryEntry entry = m_model->entryAt(index);
    QPixmap pixmap;
    if (!loadPixmap(entry, &pixmap)) {
        showError(historyText("Save Failed"),
                  historyText("Failed to load history image."));
        return;
    }

    auto& fileSettings = FileSettingsManager::instance();
    const bool rememberDirectoryOnSuccess = fileSettings.loadUseLastScreenshotSaveLocation();
    const QString defaultDir =
        fileSettings.resolveManualScreenshotSaveDirectory(rememberDirectoryOnSuccess);
    FilenameTemplateEngine::Context context;
    context.timestamp = entry.createdAt.isValid() ? entry.createdAt : QDateTime::currentDateTime();
    context.type = QStringLiteral("History");
    context.prefix = fileSettings.loadFilenamePrefix();
    context.width = pixmap.deviceIndependentSize().width();
    context.height = pixmap.deviceIndependentSize().height();
    context.monitor = QStringLiteral("history");
    context.ext = QStringLiteral("png");
    context.dateFormat = fileSettings.loadDateFormat();
    context.outputDir = defaultDir;

    const QString defaultPath = FilenameTemplateEngine::buildUniqueFilePath(
        defaultDir, fileSettings.loadFilenameTemplate(), context, 1);
    const QString filePath = NativeFileDialogUtils::getSaveFileName(
        nullptr,
        historyText("Save History Image"),
        defaultPath,
        historyText("PNG Image (*.png);;JPEG Image (*.jpg *.jpeg);;All Files (*)"),
        m_hostWindow.data());
    if (filePath.isEmpty()) {
        return;
    }

    QScreen* exportScreen = QGuiApplication::primaryScreen();
    const QImage taggedImage = tagImageWithScreenColorSpace(pixmap.toImage(), exportScreen);
    ImageSaveUtils::Error saveError;
    if (!ImageSaveUtils::saveImageAtomically(taggedImage, filePath, QByteArray(), &saveError)) {
        showError(historyText("Save Failed"),
                  historyText("Failed to save history image: %1").arg(saveErrorDetail(saveError)));
        return;
    }

    if (rememberDirectoryOnSuccess) {
        fileSettings.rememberManualScreenshotSaveDirectory(filePath);
    }
    emit closeRequested();
}

void HistoryBackend::exportAs(int index)
{
    if (!m_model) {
        return;
    }

    const HistoryEntry entry = m_model->entryAt(index);
    QPixmap pixmap;
    if (!loadPixmap(entry, &pixmap)) {
        showError(historyText("Save Failed"),
                  historyText("Failed to load history image."));
        return;
    }

    ImageExportDialogController::Request request;
    request.image = tagImageWithScreenColorSpace(pixmap.toImage(), QGuiApplication::primaryScreen());
    request.nameContext.timestamp =
        entry.createdAt.isValid() ? entry.createdAt : QDateTime::currentDateTime();
    request.nameContext.type = QStringLiteral("History");
    request.nameContext.prefix = FileSettingsManager::instance().loadFilenamePrefix();
    request.nameContext.monitor = QStringLiteral("history");
    request.parentWindow = m_hostWindow.data();
    request.screen = m_hostWindow ? m_hostWindow->screen() : nullptr;

    if (!m_imageExport) {
        m_imageExport = new ImageExportDialogController(this);
        connect(m_imageExport, &ImageExportDialogController::exported, this,
                [](const QString& filePath) {
                    QmlToast::screenToast().showToast(
                        QmlToast::Level::Success,
                        QCoreApplication::translate("MainApplication", "Screenshot Saved"),
                        QCoreApplication::translate("MainApplication", "Saved to: %1").arg(filePath));
                });
        // The dialog belongs to the Library: it goes when the Library does.
        if (m_hostWindow) {
            connect(m_hostWindow.data(), &QWindow::visibleChanged, m_imageExport,
                    [this](bool visible) {
                        if (!visible && m_imageExport) m_imageExport->close();
                    });
        }
    }
    m_imageExport->open(request);
}

void HistoryBackend::deleteEntry(int index)
{
    if (!m_model) {
        return;
    }

    // An Export dialog may be showing this very item.
    if (m_imageExport) {
        m_imageExport->close();
    }

    const HistoryEntry entry = m_model->entryAt(index);
    if (!HistoryStore::deleteEntry(entry)) {
        showError(historyText("Delete Failed"),
                  historyText("Failed to delete history item."));
    }

    m_model->refresh();
}

void HistoryBackend::openHistoryFolder()
{
    const QString path = HistoryStore::historyRootPath();
    QDir dir(path);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        showError(historyText("Open Failed"),
                  historyText("Failed to create history folder."));
        return;
    }

    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        showError(historyText("Open Failed"),
                  historyText("Failed to open history folder."));
    }
}

bool HistoryBackend::loadPixmap(const HistoryEntry& entry, QPixmap* pixmapOut) const
{
    if (!pixmapOut || entry.resultPath.isEmpty()) {
        return false;
    }

    const QPixmap pixmap(entry.resultPath);
    if (pixmap.isNull()) {
        return false;
    }

    QPixmap restored = pixmap;
    if (entry.devicePixelRatio > 0.0) {
        restored.setDevicePixelRatio(entry.devicePixelRatio);
    }
    *pixmapOut = restored;
    return true;
}

void HistoryBackend::showError(const QString& title, const QString& message)
{
    emit toastRequested(1, title, message);
}

} // namespace SnapTray
