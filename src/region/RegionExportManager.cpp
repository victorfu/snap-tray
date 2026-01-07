#include "region/RegionExportManager.h"
#include "annotations/AnnotationLayer.h"
#include "settings/FileSettingsManager.h"

#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>

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

QPixmap RegionExportManager::getSelectedRegion(const QRect &selectionRect, int cornerRadius)
{
    if (m_backgroundPixmap.isNull() || selectionRect.isEmpty()) {
        return QPixmap();
    }

    // Use device pixels coordinates for cropping
    QPixmap selectedRegion = m_backgroundPixmap.copy(
        static_cast<int>(selectionRect.x() * m_devicePixelRatio),
        static_cast<int>(selectionRect.y() * m_devicePixelRatio),
        static_cast<int>(selectionRect.width() * m_devicePixelRatio),
        static_cast<int>(selectionRect.height() * m_devicePixelRatio)
    );

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

    QGuiApplication::clipboard()->setPixmap(selectedRegion);
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
    QString dateFormat = fileSettings.loadDateFormat();
    QString prefix = fileSettings.loadFilenamePrefix();
    QString timestamp = QDateTime::currentDateTime().toString(dateFormat);

    QString filename;
    if (prefix.isEmpty()) {
        filename = QString("Screenshot_%1.png").arg(timestamp);
    } else {
        filename = QString("%1_Screenshot_%2.png").arg(prefix).arg(timestamp);
    }
    QString defaultName = QDir(savePath).filePath(filename);

    // Notify parent before showing dialog
    emit saveDialogOpening();

    QString filePath = QFileDialog::getSaveFileName(
        parentWidget,
        "Save Screenshot",
        defaultName,
        "PNG Image (*.png);;JPEG Image (*.jpg *.jpeg);;All Files (*)"
    );

    if (!filePath.isEmpty()) {
        bool success = selectedRegion.save(filePath);
        if (success) {
            qDebug() << "RegionExportManager: Saved to" << filePath;
            emit saveCompleted(selectedRegion, filePath);
        } else {
            qWarning() << "RegionExportManager: Failed to save to" << filePath;
        }
        emit saveDialogClosed(true);
        return success;
    }

    // User cancelled
    emit saveDialogClosed(false);
    return false;
}
