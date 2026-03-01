#include "region/RegionExportManager.h"
#include "ImageColorSpaceHelper.h"
#include "PlatformFeatures.h"
#include "annotations/AnnotationLayer.h"
#include "settings/FileSettingsManager.h"
#include "utils/CoordinateHelper.h"
#include "utils/FilenameTemplateEngine.h"
#include "utils/ImageSaveUtils.h"

#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>

RegionExportManager::RegionExportManager(QObject *parent)
    : QObject(parent)
{
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

void RegionExportManager::copyToClipboard(const QRect &selectionRect, int cornerRadius)
{
    QPixmap selectedRegion = getSelectedRegion(selectionRect, cornerRadius);
    if (selectedRegion.isNull()) {
        qWarning() << "RegionExportManager: Cannot copy - no valid selection";
        return;
    }

    const QImage taggedImage =
        tagImageWithScreenColorSpace(selectedRegion.toImage(), m_sourceScreen.data());
    if (!PlatformFeatures::instance().copyImageToClipboard(taggedImage)) {
        QGuiApplication::clipboard()->setImage(taggedImage);
    }
    qDebug() << "RegionExportManager: Copied to clipboard";

    emit copyCompleted(selectedRegion);
}

bool RegionExportManager::saveToFile(const QRect &selectionRect, int cornerRadius, QWidget *parentWidget)
{
    QPixmap selectedRegion = getSelectedRegion(selectionRect, cornerRadius);
    if (selectedRegion.isNull()) {
        qWarning() << "RegionExportManager: Cannot save - no valid selection";
        return false;
    }

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
        QString renderError;
        QString filePath = FilenameTemplateEngine::buildUniqueFilePath(
            savePath, templateValue, context, 100, &renderError);

        ImageSaveUtils::Error saveError;
        const QImage taggedImage =
            tagImageWithScreenColorSpace(selectedRegion.toImage(), m_sourceScreen.data());
        bool success = ImageSaveUtils::saveImageAtomically(
            taggedImage, filePath, QByteArray(), &saveError);
        if (success) {
            qDebug() << "RegionExportManager: Auto-saved to" << filePath;
            emit saveCompleted(selectedRegion, filePath);
        } else {
            qWarning() << "RegionExportManager: Failed to auto-save to" << filePath;
            if (!saveError.stage.isEmpty() || !saveError.message.isEmpty()) {
                qWarning() << "RegionExportManager: save error:" << saveError.stage << saveError.message;
            }
            if (!renderError.isEmpty()) {
                qWarning() << "RegionExportManager: template warning:" << renderError;
            }
            const QString detail = saveError.stage.isEmpty()
                ? (saveError.message.isEmpty() ? QStringLiteral("Unknown error") : saveError.message)
                : QStringLiteral("%1: %2").arg(saveError.stage, saveError.message);
            emit saveFailed(filePath, tr("Failed to save screenshot: %1").arg(detail));
        }
        return success;
    }

    // Show save dialog
    const QString defaultName = FilenameTemplateEngine::buildUniqueFilePath(
        savePath, templateValue, context, 1);

    // Notify parent before showing dialog
    emit saveDialogOpening();

    QString filePath = QFileDialog::getSaveFileName(
        parentWidget,
        tr("Save Screenshot"),
        defaultName,
        tr("PNG Image (*.png);;JPEG Image (*.jpg *.jpeg);;All Files (*)")
    );

    if (!filePath.isEmpty()) {
        ImageSaveUtils::Error saveError;
        const QImage taggedImage =
            tagImageWithScreenColorSpace(selectedRegion.toImage(), m_sourceScreen.data());
        bool success = ImageSaveUtils::saveImageAtomically(
            taggedImage, filePath, QByteArray(), &saveError);
        if (success) {
            qDebug() << "RegionExportManager: Saved to" << filePath;
            emit saveCompleted(selectedRegion, filePath);
        } else {
            qWarning() << "RegionExportManager: Failed to save to" << filePath;
            if (!saveError.stage.isEmpty() || !saveError.message.isEmpty()) {
                qWarning() << "RegionExportManager: save error:" << saveError.stage << saveError.message;
            }
        }
        emit saveDialogClosed(true);
        return success;
    }

    // User cancelled
    emit saveDialogClosed(false);
    return false;
}
