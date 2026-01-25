#include "cli/commands/RegionCommand.h"

#include "PlatformFeatures.h"
#include "settings/FileSettingsManager.h"

#include <QBuffer>
#include <QColorSpace>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPixmap>
#include <QScreen>
#include <QThread>

namespace SnapTray {
namespace CLI {

QString RegionCommand::name() const { return "region"; }

QString RegionCommand::description() const { return "Capture specified region"; }

void RegionCommand::setupOptions(QCommandLineParser& parser)
{
    parser.addOption({{"r", "region"}, "Region definition (x,y,width,height)", "rect"});
    parser.addOption({{"n", "screen"}, "Screen number (default: primary)", "num", "0"});
    parser.addOption({{"d", "delay"}, "Delay in milliseconds before capture", "ms", "0"});
    parser.addOption({{"p", "path"}, "Save directory", "dir"});
    parser.addOption({{"o", "output"}, "Output file path", "file"});
    parser.addOption({{"c", "clipboard"}, "Copy to clipboard"});
    parser.addOption({"cursor", "Include mouse cursor"});
    parser.addOption({"raw", "Output raw PNG to stdout"});
}

CLIResult RegionCommand::execute(const QCommandLineParser& parser)
{
    // Region is required
    if (!parser.isSet("region")) {
        return CLIResult::error(
            CLIResult::Code::InvalidArguments,
            "Region required. Use --region x,y,width,height");
    }

    // Parse region
    QString regionStr = parser.value("region");
    QStringList parts = regionStr.split(',');
    if (parts.size() != 4) {
        return CLIResult::error(
            CLIResult::Code::InvalidArguments,
            "Invalid region format. Use: x,y,width,height");
    }

    bool ok;
    int x = parts[0].toInt(&ok);
    if (!ok) {
        return CLIResult::error(CLIResult::Code::InvalidArguments, "Invalid x coordinate");
    }
    int y = parts[1].toInt(&ok);
    if (!ok) {
        return CLIResult::error(CLIResult::Code::InvalidArguments, "Invalid y coordinate");
    }
    int width = parts[2].toInt(&ok);
    if (!ok || width <= 0) {
        return CLIResult::error(CLIResult::Code::InvalidArguments, "Invalid width");
    }
    int height = parts[3].toInt(&ok);
    if (!ok || height <= 0) {
        return CLIResult::error(CLIResult::Code::InvalidArguments, "Invalid height");
    }

    QRect region(x, y, width, height);

    // Parse other arguments
    int screenNum = parser.value("screen").toInt();
    int delay = parser.value("delay").toInt();
    QString savePath = parser.value("path");
    QString outputFile = parser.value("output");
    bool toClipboard = parser.isSet("clipboard");
    bool toRaw = parser.isSet("raw");

    QList<QScreen*> screens = QGuiApplication::screens();
    if (screenNum < 0 || screenNum >= screens.size()) {
        return CLIResult::error(
            CLIResult::Code::InvalidArguments,
            QString("Invalid screen number: %1 (available: 0-%2)")
                .arg(screenNum)
                .arg(screens.size() - 1));
    }

    // Delay
    if (delay > 0) {
        QThread::msleep(delay);
    }

    QScreen* screen = screens.at(screenNum);

    // Capture full screen then crop
    QPixmap fullScreenshot = screen->grabWindow(0);
    if (fullScreenshot.isNull()) {
        return CLIResult::error(CLIResult::Code::GeneralError, "Failed to capture screen");
    }

    // Validate region against screen bounds
    QRect screenRect(0, 0, fullScreenshot.width(), fullScreenshot.height());
    if (!screenRect.contains(region)) {
        return CLIResult::error(
            CLIResult::Code::InvalidArguments,
            QString("Region (%1,%2,%3,%4) exceeds screen bounds (%5x%6)")
                .arg(region.x())
                .arg(region.y())
                .arg(region.width())
                .arg(region.height())
                .arg(screenRect.width())
                .arg(screenRect.height()));
    }

    // Crop to region
    QPixmap screenshot = fullScreenshot.copy(region);

    if (screenshot.isNull()) {
        return CLIResult::error(CLIResult::Code::GeneralError, "Failed to crop region");
    }

    // Process pending events to ensure Qt subsystems are ready
    QCoreApplication::processEvents();

    // Output to stdout (raw PNG)
    if (toRaw) {
        QImage rawImage = screenshot.toImage().convertToFormat(QImage::Format_RGB32);
        rawImage.setColorSpace(QColorSpace());

        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        rawImage.save(&buffer, "PNG");
        return CLIResult::withData(data);
    }

    // Copy to clipboard
    if (toClipboard) {
        QImage clipboardImage = screenshot.toImage().convertToFormat(QImage::Format_ARGB32);
        clipboardImage.setColorSpace(QColorSpace()); // Strip color profile to avoid libpng errors

        // Use native clipboard API for reliable persistence in CLI mode
        if (PlatformFeatures::instance().copyImageToClipboard(clipboardImage)) {
            return CLIResult::success("Screenshot copied to clipboard");
        }
        return CLIResult::error(CLIResult::Code::GeneralError, "Failed to copy to clipboard");
    }

    // Save to file
    // Convert to QImage and strip color profile (fixes libpng iCCP error on macOS)
    QImage image = screenshot.toImage().convertToFormat(QImage::Format_RGB32);
    image.setColorSpace(QColorSpace());
    QString filePath;
    if (!outputFile.isEmpty()) {
        filePath = outputFile;
    }
    else {
        if (savePath.isEmpty()) {
            savePath = FileSettingsManager::instance().loadScreenshotPath();
        }

        QString prefix = FileSettingsManager::instance().loadFilenamePrefix();
        QString dateFormat = FileSettingsManager::instance().loadDateFormat();
        QString timestamp = QDateTime::currentDateTime().toString(dateFormat);
        QString filename = prefix.isEmpty() ? QString("Screenshot_%1.png").arg(timestamp)
                                            : QString("%1_%2.png").arg(prefix, timestamp);

        filePath = QDir(savePath).filePath(filename);
    }

    // Ensure directory exists
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    if (!image.save(filePath, "PNG")) {
        return CLIResult::error(
            CLIResult::Code::FileError, QString("Failed to save screenshot to: %1").arg(filePath));
    }

    return CLIResult::success(QString("Screenshot saved to: %1").arg(filePath));
}

} // namespace CLI
} // namespace SnapTray
