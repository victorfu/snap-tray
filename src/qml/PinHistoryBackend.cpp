#include "qml/PinHistoryBackend.h"

#include "ImageColorSpaceHelper.h"
#include "PinWindow.h"
#include "PinWindowManager.h"
#include "PlatformFeatures.h"
#include "pinwindow/PinWindowPlacement.h"
#include "pinwindow/RegionLayoutManager.h"
#include "qml/PinHistoryModel.h"
#include "settings/FileSettingsManager.h"
#include "utils/FilenameTemplateEngine.h"
#include "utils/ImageSaveUtils.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>
#include <QUrl>
#include <QWindow>

namespace {

QString pinHistoryText(const char* sourceText)
{
    return QCoreApplication::translate("PinHistoryWindow", sourceText);
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

QString suffixForNameFilter(const QString& nameFilter)
{
    const int openParen = nameFilter.indexOf(QLatin1Char('('));
    const int closeParen = nameFilter.indexOf(QLatin1Char(')'), openParen + 1);
    if (openParen < 0 || closeParen <= openParen) {
        return QString();
    }

    const QString patterns = nameFilter.mid(openParen + 1, closeParen - openParen - 1);
    const QStringList entries = patterns.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString& entry : entries) {
        if (!entry.startsWith(QStringLiteral("*."))) {
            continue;
        }

        const QString suffix = entry.mid(2);
        if (!suffix.isEmpty() && suffix != QStringLiteral("*")) {
            return suffix;
        }
    }

    return QString();
}

} // namespace

namespace SnapTray {

PinHistoryBackend::PinHistoryBackend(PinHistoryModel* model,
                                     PinWindowManager* pinWindowManager,
                                     QWindow* hostWindow,
                                     QObject* parent)
    : QObject(parent)
    , m_model(model)
    , m_pinWindowManager(pinWindowManager)
    , m_hostWindow(hostWindow)
{
}

void PinHistoryBackend::refresh()
{
    if (m_model) {
        m_model->refresh();
    }
}

void PinHistoryBackend::pin(int index)
{
    if (!m_model) {
        return;
    }

    const PinHistoryEntry entry = m_model->entryAt(index);
    if (!m_pinWindowManager) {
        showError(pinHistoryText("Pin Failed"),
                  pinHistoryText("Pin window manager is unavailable."));
        return;
    }

    QPixmap pixmap;
    if (!loadPixmap(entry, &pixmap)) {
        showError(pinHistoryText("Pin Failed"),
                  pinHistoryText("Failed to load cached screenshot."));
        return;
    }

    QScreen* screen = QGuiApplication::primaryScreen();
    const PinWindowPlacement placement = computeInitialPinWindowPlacement(
        pixmap,
        screen ? screen->availableGeometry() : QRect());
    PinWindow* pinWindow = m_pinWindowManager->createPinWindow(pixmap, placement.position, false);
    if (pinWindow && placement.zoomLevel < 1.0) {
        pinWindow->setZoomLevel(placement.zoomLevel);
    }

    if (pinWindow && !entry.metadataPath.isEmpty() && QFile::exists(entry.metadataPath)) {
        QFile metadataFile(entry.metadataPath);
        if (metadataFile.open(QIODevice::ReadOnly)) {
            const QByteArray data = metadataFile.readAll();
            const QVector<LayoutRegion> regions = RegionLayoutManager::deserializeRegions(data);
            if (!regions.isEmpty()) {
                pinWindow->setMultiRegionData(regions);
            }
        }
    }

    emit closeRequested();
}

void PinHistoryBackend::copy(int index)
{
    if (!m_model) {
        return;
    }

    QPixmap pixmap;
    if (!loadPixmap(m_model->entryAt(index), &pixmap)) {
        showError(pinHistoryText("Copy Failed"),
                  pinHistoryText("Failed to load cached screenshot."));
        return;
    }

    QScreen* exportScreen = QGuiApplication::primaryScreen();
    const QImage taggedImage = tagImageWithScreenColorSpace(pixmap.toImage(), exportScreen);
    if (!PlatformFeatures::instance().copyImageToClipboard(taggedImage)) {
        QGuiApplication::clipboard()->setImage(taggedImage);
    }

    emit closeRequested();
}

void PinHistoryBackend::saveAs(int index)
{
    if (!m_model) {
        return;
    }

    const PinHistoryEntry entry = m_model->entryAt(index);
    QPixmap pixmap;
    if (!loadPixmap(entry, &pixmap)) {
        showError(pinHistoryText("Save Failed"),
                  pinHistoryText("Failed to load cached screenshot."));
        return;
    }

    auto& fileSettings = FileSettingsManager::instance();
    const QString defaultDir = fileSettings.loadScreenshotPath();
    FilenameTemplateEngine::Context context;
    context.timestamp = entry.capturedAt.isValid() ? entry.capturedAt : QDateTime::currentDateTime();
    context.type = QStringLiteral("Pinned");
    context.prefix = fileSettings.loadFilenamePrefix();
    context.width = entry.imageSize.width();
    context.height = entry.imageSize.height();
    context.monitor = QStringLiteral("unknown");
    context.ext = QStringLiteral("png");
    context.dateFormat = fileSettings.loadDateFormat();
    context.outputDir = defaultDir;

    const QString defaultPath = FilenameTemplateEngine::buildUniqueFilePath(
        defaultDir, fileSettings.loadFilenameTemplate(), context, 1);
    const QString filtersText =
        pinHistoryText("PNG Image (*.png);;JPEG Image (*.jpg *.jpeg);;All Files (*)");
    const QFileInfo defaultInfo(defaultPath);
    const QStringList nameFilters = filtersText.split(QStringLiteral(";;"));

    QFileDialog dialog(nullptr, pinHistoryText("Save Screenshot"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setDirectory(defaultInfo.absolutePath());
    dialog.selectFile(defaultInfo.fileName());
    dialog.setNameFilters(nameFilters);
    dialog.selectNameFilter(nameFilters.value(0));
    dialog.setDefaultSuffix(suffixForNameFilter(dialog.selectedNameFilter()));
    QObject::connect(&dialog, &QFileDialog::filterSelected, &dialog, [&dialog](const QString& filter) {
        dialog.setDefaultSuffix(suffixForNameFilter(filter));
    });
    dialog.setWindowModality(Qt::WindowModal);
    dialog.winId();
    if (m_hostWindow && dialog.windowHandle()) {
        dialog.windowHandle()->setTransientParent(m_hostWindow);
    }
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QString filePath = dialog.selectedFiles().value(0);
    if (filePath.isEmpty()) {
        return;
    }
    if (QFileInfo(filePath).suffix().isEmpty()) {
        const QString selectedSuffix = suffixForNameFilter(dialog.selectedNameFilter());
        if (!selectedSuffix.isEmpty()) {
            filePath += QStringLiteral(".") + selectedSuffix;
        }
    }

    QScreen* exportScreen = QGuiApplication::primaryScreen();
    const QImage taggedImage = tagImageWithScreenColorSpace(pixmap.toImage(), exportScreen);
    ImageSaveUtils::Error saveError;
    if (!ImageSaveUtils::saveImageAtomically(taggedImage, filePath, QByteArray(), &saveError)) {
        showError(pinHistoryText("Save Failed"),
                  pinHistoryText("Failed to save screenshot: %1").arg(saveErrorDetail(saveError)));
        return;
    }

    emit closeRequested();
}

void PinHistoryBackend::deleteEntry(int index)
{
    if (!m_model) {
        return;
    }

    const PinHistoryEntry entry = m_model->entryAt(index);
    const bool deleted = PinHistoryStore::deleteEntry(entry);
    if (!deleted) {
        showError(pinHistoryText("Delete Failed"),
                  pinHistoryText("Failed to delete cached screenshot."));
    }

    m_model->refresh();
}

void PinHistoryBackend::openCacheFolder()
{
    const QString path = PinHistoryStore::historyFolderPath();
    QDir dir(path);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        showError(pinHistoryText("Open Failed"),
                  pinHistoryText("Failed to create cache folder."));
        return;
    }

    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        showError(pinHistoryText("Open Failed"),
                  pinHistoryText("Failed to open cache folder."));
    }
}

bool PinHistoryBackend::loadPixmap(const PinHistoryEntry& entry, QPixmap* pixmapOut) const
{
    if (!pixmapOut || entry.imagePath.isEmpty()) {
        return false;
    }

    const QPixmap pixmap(entry.imagePath);
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

void PinHistoryBackend::showError(const QString& title, const QString& message)
{
    emit toastRequested(1, title, message);
}

} // namespace SnapTray
