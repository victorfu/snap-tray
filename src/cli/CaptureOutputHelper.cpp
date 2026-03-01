#include "cli/CaptureOutputHelper.h"

#include "ImageColorSpaceHelper.h"
#include "PlatformFeatures.h"
#include "settings/FileSettingsManager.h"
#include "utils/CoordinateHelper.h"
#include "utils/FilenameTemplateEngine.h"
#include "utils/ImageSaveUtils.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPixmap>
#include <QScreen>
#include <QThread>

namespace SnapTray {
namespace CLI {
namespace {

QString resolveCaptureType(const CaptureMetadata& metadata)
{
    return metadata.captureType.isEmpty() ? QStringLiteral("Screenshot") : metadata.captureType;
}

QImage prepareImageForRawOrSave(const QPixmap& screenshot, QScreen* sourceScreen)
{
    return tagImageWithScreenColorSpace(
        screenshot.toImage().convertToFormat(QImage::Format_RGB32), sourceScreen);
}

QImage prepareImageForClipboard(const QPixmap& screenshot, QScreen* sourceScreen)
{
    return tagImageWithScreenColorSpace(
        screenshot.toImage().convertToFormat(QImage::Format_ARGB32), sourceScreen);
}

QString resolveOutputFilePath(
    const QPixmap& screenshot, const CaptureOutputOptions& options, const CaptureMetadata& metadata)
{
    if (!options.outputFile.isEmpty()) {
        return options.outputFile;
    }

    QString savePath = options.savePath;
    if (savePath.isEmpty()) {
        savePath = FileSettingsManager::instance().loadScreenshotPath();
    }

    const qreal dpr = screenshot.devicePixelRatio() > 0.0 ? screenshot.devicePixelRatio() : 1.0;
    const QSize logicalSize = CoordinateHelper::toLogical(screenshot.size(), dpr);
    const int monitorIndex = metadata.sourceScreen
        ? QGuiApplication::screens().indexOf(metadata.sourceScreen)
        : -1;

    auto& fileSettings = FileSettingsManager::instance();
    FilenameTemplateEngine::Context context;
    context.type = resolveCaptureType(metadata);
    context.prefix = fileSettings.loadFilenamePrefix();
    context.width = logicalSize.width();
    context.height = logicalSize.height();
    context.monitor = monitorIndex >= 0 ? QString::number(monitorIndex) : QStringLiteral("unknown");
    context.windowTitle = QString();
    context.appName = QString();
    context.regionIndex = -1;
    context.ext = QStringLiteral("png");
    context.dateFormat = fileSettings.loadDateFormat();
    context.outputDir = savePath;

    return FilenameTemplateEngine::buildUniqueFilePath(
        savePath, fileSettings.loadFilenameTemplate(), context);
}

} // namespace

void applyOptionalDelay(int delayMs)
{
    if (delayMs > 0) {
        QThread::msleep(delayMs);
    }
}

CLIResult emitCaptureOutput(
    const QPixmap& screenshot, const CaptureOutputOptions& options, const CaptureMetadata& metadata)
{
    // Process pending events to ensure Qt subsystems are ready
    QCoreApplication::processEvents();

    if (options.toRaw) {
        QImage rawImage = prepareImageForRawOrSave(screenshot, metadata.sourceScreen);

        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        rawImage.save(&buffer, "PNG");
        return CLIResult::withData(data);
    }

    if (options.toClipboard) {
        QImage clipboardImage = prepareImageForClipboard(screenshot, metadata.sourceScreen);

        // Use native clipboard API for reliable persistence in CLI mode
        if (PlatformFeatures::instance().copyImageToClipboard(clipboardImage)) {
            return CLIResult::success("Screenshot copied to clipboard");
        }
        return CLIResult::error(CLIResult::Code::GeneralError, "Failed to copy to clipboard");
    }

    const QString filePath = resolveOutputFilePath(screenshot, options, metadata);

    // Ensure directory exists
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    QImage image = prepareImageForRawOrSave(screenshot, metadata.sourceScreen);
    ImageSaveUtils::Error saveError;
    if (!ImageSaveUtils::saveImageAtomically(image, filePath, QByteArrayLiteral("PNG"), &saveError)) {
        const QString detail = saveError.stage.isEmpty()
            ? (saveError.message.isEmpty() ? QStringLiteral("Unknown error") : saveError.message)
            : QStringLiteral("%1: %2").arg(saveError.stage, saveError.message);
        return CLIResult::error(
            CLIResult::Code::FileError,
            QStringLiteral("Failed to save screenshot to: %1 (%2)").arg(filePath, detail));
    }

    return CLIResult::success(QString("Screenshot saved to: %1").arg(filePath));
}

} // namespace CLI
} // namespace SnapTray
