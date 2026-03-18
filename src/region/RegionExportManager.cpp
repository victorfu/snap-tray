#include "region/RegionExportManager.h"
#include "ImageColorSpaceHelper.h"
#include "annotations/AnnotationLayer.h"
#include "settings/FileSettingsManager.h"
#include "utils/CoordinateHelper.h"
#include "utils/FilenameTemplateEngine.h"
#include "utils/ImageSaveUtils.h"
#include "utils/NativeFileDialogUtils.h"

#include <QDateTime>
#include <QDebug>
#include <QImage>
#include <QFutureWatcher>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QtConcurrent/QtConcurrentRun>

namespace {

struct SaveTaskResult {
    QString filePath;
    QString renderWarning;
    ImageSaveUtils::Error saveError;
    bool success = false;
};

QString saveErrorDetail(const ImageSaveUtils::Error& saveError)
{
    return saveError.stage.isEmpty()
        ? (saveError.message.isEmpty() ? QStringLiteral("Unknown error") : saveError.message)
        : QStringLiteral("%1: %2").arg(saveError.stage, saveError.message);
}

} // namespace

RegionExportManager::RegionExportManager(QObject *parent)
    : QObject(parent)
{
}

RegionExportManager::~RegionExportManager()
{
    if (m_saveWatcher) {
        m_saveWatcher->waitForFinished();
    }
}

void RegionExportManager::setBackgroundPixmap(const QPixmap &pixmap)
{
    m_backgroundPixmap = pixmap;
}

void RegionExportManager::setDevicePixelRatio(qreal ratio)
{
    m_devicePixelRatio = ratio;
}

void RegionExportManager::setAnnotationLayer(AnnotationLayer *layer)
{
    m_annotationLayer = layer;
}

void RegionExportManager::setMonitorIdentifier(const QString& monitor)
{
    m_monitorIdentifier = monitor;
}

void RegionExportManager::setWindowMetadata(const QString& windowTitle, const QString& appName)
{
    m_windowTitle = windowTitle;
    m_appName = appName;
}

void RegionExportManager::setRegionIndex(int regionIndex)
{
    m_regionIndex = regionIndex;
}

void RegionExportManager::setSourceScreen(QScreen* screen)
{
    m_sourceScreen = screen;
}

QPixmap RegionExportManager::getSelectedRegion(const QRect &selectionRect, int cornerRadius)
{
    if (m_backgroundPixmap.isNull() || selectionRect.isEmpty()) {
        return QPixmap();
    }

    // Use edge-aligned device-pixel coordinates for cropping.
    const QRect physicalRect = CoordinateHelper::toPhysicalCoveringRect(selectionRect, m_devicePixelRatio);
    const QRect clampedPhysicalRect = physicalRect.intersected(m_backgroundPixmap.rect());
    if (clampedPhysicalRect.isEmpty()) {
        return QPixmap();
    }
    QPixmap selectedRegion = m_backgroundPixmap.copy(clampedPhysicalRect);

    // Set DPR BEFORE painting so Qt handles scaling automatically
    // This ensures annotation sizes (radius, pen width, font) stay in logical units
    // while positions are automatically converted to device pixels by Qt
    selectedRegion.setDevicePixelRatio(m_devicePixelRatio);

    // Draw annotations onto the selected region
    if (m_annotationLayer && !m_annotationLayer->isEmpty()) {
        QPainter painter(&selectedRegion);
        painter.setRenderHint(QPainter::Antialiasing, true);

        // Only translate - no manual scale() needed
        // Qt automatically handles DPR conversion because we set devicePixelRatio above
        // Annotation at screen position P will appear at (P - sel.topLeft) in logical coords
        painter.translate(-selectionRect.x(), -selectionRect.y());

        qDebug() << "RegionExportManager: Drawing annotations: sel=" << selectionRect
                 << "DPR=" << m_devicePixelRatio
                 << "pixmap=" << selectedRegion.size();
        m_annotationLayer->draw(painter);
    }

    // Apply rounded corners mask if radius > 0
    if (cornerRadius > 0) {
        return applyRoundedCorners(selectedRegion, cornerRadius, selectionRect);
    }

    return selectedRegion;
}

RegionExportManager::PreparedExport RegionExportManager::prepareExport(
    const QRect& selectionRect,
    int cornerRadius)
{
    PreparedExport prepared;
    prepared.pixmap = getSelectedRegion(selectionRect, cornerRadius);
    if (prepared.pixmap.isNull()) {
        return prepared;
    }

    prepared.image = normalizeImageForExport(prepared.pixmap.toImage(), m_sourceScreen.data());
    return prepared;
}

QPixmap RegionExportManager::applyRoundedCorners(const QPixmap &source, int radius, const QRect &logicalRect)
{
    // Use an explicit alpha mask to guarantee transparent corners across platforms
    QImage sourceImage = source.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    sourceImage.setDevicePixelRatio(m_devicePixelRatio);

    QImage alphaMask(sourceImage.size(), QImage::Format_ARGB32_Premultiplied);
    alphaMask.setDevicePixelRatio(m_devicePixelRatio);
    alphaMask.fill(Qt::transparent);

    QPainter maskPainter(&alphaMask);
    maskPainter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath clipPath;
    clipPath.addRoundedRect(QRectF(0, 0, logicalRect.width(), logicalRect.height()), radius, radius);
    maskPainter.fillPath(clipPath, Qt::white);
    maskPainter.end();

    QPainter imagePainter(&sourceImage);
    imagePainter.setRenderHint(QPainter::Antialiasing, true);
    imagePainter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    imagePainter.drawImage(0, 0, alphaMask);
    imagePainter.end();

    QPixmap maskedPixmap = QPixmap::fromImage(sourceImage, Qt::NoOpaqueDetection);
    maskedPixmap.setDevicePixelRatio(m_devicePixelRatio);
    return maskedPixmap;
}

RegionExportManager::SaveRequest RegionExportManager::createSaveRequest(
    const QRect& selectionRect,
    QWidget* parentWidget) const
{
    SaveRequest request;

    // Get file settings
    auto &fileSettings = FileSettingsManager::instance();
    QString savePath = fileSettings.loadScreenshotPath();
    FilenameTemplateEngine::Context context;
    context.type = QStringLiteral("Screenshot");
    context.prefix = fileSettings.loadFilenamePrefix();
    context.width = selectionRect.width();
    context.height = selectionRect.height();
    context.monitor = m_monitorIdentifier;
    context.windowTitle = m_windowTitle;
    context.appName = m_appName;
    context.regionIndex = m_regionIndex;
    context.ext = QStringLiteral("png");
    context.dateFormat = fileSettings.loadDateFormat();
    context.outputDir = savePath;
    const QString templateValue = fileSettings.loadFilenameTemplate();

    // Check auto-save setting
    if (fileSettings.loadAutoSaveScreenshots()) {
        request.filePath = FilenameTemplateEngine::buildUniqueFilePath(
            savePath, templateValue, context, 100, &request.renderWarning);
        request.autoSave = true;
        if (request.filePath.isEmpty()) {
            request.error = request.renderWarning.isEmpty()
                ? tr("Failed to determine screenshot output path")
                : request.renderWarning;
        }
        return request;
    }

    // Show save dialog
    const QString defaultName = FilenameTemplateEngine::buildUniqueFilePath(
        savePath, templateValue, context, 1);
    request.filePath = NativeFileDialogUtils::getSaveFileName(
        parentWidget,
        tr("Save Screenshot"),
        defaultName,
        tr("PNG Image (*.png);;JPEG Image (*.jpg *.jpeg);;All Files (*)"));
    request.cancelled = request.filePath.isEmpty();
    return request;
}

void RegionExportManager::savePreparedExportAsync(PreparedExport prepared,
                                                  const QString& filePath,
                                                  const QString& renderWarning)
{
    if (!prepared.isValid()) {
        emit saveFailed(filePath, tr("Failed to save screenshot: unable to process selection"));
        return;
    }

    if (filePath.isEmpty()) {
        emit saveFailed(QString(), tr("Failed to save screenshot: output path is empty"));
        return;
    }

    if (m_saveWatcher) {
        qWarning() << "RegionExportManager: save already in progress";
        emit saveFailed(filePath, tr("Failed to save screenshot: another save is already in progress"));
        return;
    }

    auto* watcher = new QFutureWatcher<SaveTaskResult>(this);
    m_saveWatcher = watcher;
    const QImage image = prepared.image;
    connect(watcher, &QFutureWatcher<SaveTaskResult>::finished, this,
        [this, watcher, prepared = std::move(prepared)]() mutable {
            const SaveTaskResult result = watcher->result();
            if (m_saveWatcher == watcher) {
                m_saveWatcher.clear();
            }
            watcher->deleteLater();

            if (result.success) {
                qDebug() << "RegionExportManager: Saved to" << result.filePath;
                emit saveCompleted(prepared.pixmap, prepared.image, result.filePath);
                return;
            }

            qWarning() << "RegionExportManager: Failed to save to" << result.filePath;
            if (!result.saveError.stage.isEmpty() || !result.saveError.message.isEmpty()) {
                qWarning() << "RegionExportManager: save error:"
                           << result.saveError.stage << result.saveError.message;
            }
            if (!result.renderWarning.isEmpty()) {
                qWarning() << "RegionExportManager: template warning:" << result.renderWarning;
            }
            emit saveFailed(
                result.filePath,
                tr("Failed to save screenshot: %1").arg(saveErrorDetail(result.saveError)));
        });

    watcher->setFuture(QtConcurrent::run([image, filePath, renderWarning]() {
        SaveTaskResult result;
        result.filePath = filePath;
        result.renderWarning = renderWarning;
        result.success = ImageSaveUtils::saveImageAtomically(
            image, filePath, QByteArray(), &result.saveError);
        return result;
    }));
}
