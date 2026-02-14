#include "cli/commands/FullCommand.h"

#include "PlatformFeatures.h"
#include "settings/FileSettingsManager.h"
#include "utils/FilenameTemplateEngine.h"

#include <QBuffer>
#include <QColorSpace>
#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QImageWriter>
#include <QPixmap>
#include <QScreen>
#include <QThread>

namespace SnapTray {
namespace CLI {

QString FullCommand::name() const { return "full"; }

QString FullCommand::description() const { return "Capture full screen"; }

void FullCommand::setupOptions(QCommandLineParser& parser)
{
    parser.addOption({{"d", "delay"}, "Delay in milliseconds before capture", "ms", "0"});
    parser.addOption({{"p", "path"}, "Save directory", "dir"});
    parser.addOption({{"o", "output"}, "Output file path", "file"});
    parser.addOption({{"c", "clipboard"}, "Copy to clipboard instead of saving"});
    parser.addOption({{"n", "screen"}, "Screen number (0 = primary)", "num"});
    parser.addOption({"raw", "Output raw PNG to stdout"});
}

CLIResult FullCommand::execute(const QCommandLineParser& parser)
{
    // Parse arguments
    int delay = parser.value("delay").toInt();
    QString savePath = parser.value("path");
    QString outputFile = parser.value("output");
    bool toClipboard = parser.isSet("clipboard");
    bool toRaw = parser.isSet("raw");
    int screenNum = parser.isSet("screen") ? parser.value("screen").toInt() : -1;

    // Delay
    if (delay > 0) {
        QThread::msleep(delay);
    }

    // Get screen
    QList<QScreen*> screens = QGuiApplication::screens();
    QScreen* screen = nullptr;

    if (screenNum >= 0) {
        if (screenNum >= screens.size()) {
            return CLIResult::error(
                CLIResult::Code::InvalidArguments,
                QString("Invalid screen number: %1 (available: 0-%2)")
                    .arg(screenNum)
                    .arg(screens.size() - 1));
        }
        screen = screens.at(screenNum);
    }
    else {
        // Use screen at cursor position
        QPoint cursorPos = QCursor::pos();
        screen = QGuiApplication::screenAt(cursorPos);
        if (!screen) {
            screen = QGuiApplication::primaryScreen();
        }
    }

    if (!screen) {
        return CLIResult::error(CLIResult::Code::GeneralError, "No screen available");
    }

    // Capture screen
    QPixmap screenshot = screen->grabWindow(0);

    if (screenshot.isNull()) {
        return CLIResult::error(CLIResult::Code::GeneralError, "Failed to capture screen");
    }

    // Output to stdout (raw PNG)
    if (toRaw) {
        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        screenshot.save(&buffer, "PNG");
        return CLIResult::withData(data);
    }

    // Copy to clipboard
    if (toClipboard) {
        QImage image = screenshot.toImage().convertToFormat(QImage::Format_ARGB32);
        image.setColorSpace(QColorSpace()); // Strip color profile to avoid libpng errors

        // Use native clipboard API for reliable persistence in CLI mode
        if (PlatformFeatures::instance().copyImageToClipboard(image)) {
            return CLIResult::success("Screenshot copied to clipboard");
        }
        return CLIResult::error(CLIResult::Code::GeneralError, "Failed to copy to clipboard");
    }

    // Save to file
    QString filePath;
    if (!outputFile.isEmpty()) {
        filePath = outputFile;
    }
    else {
        if (savePath.isEmpty()) {
            savePath = FileSettingsManager::instance().loadScreenshotPath();
        }

        const qreal dpr = screenshot.devicePixelRatio() > 0.0 ? screenshot.devicePixelRatio() : 1.0;
        const QSize logicalSize = screenshot.size() / dpr;
        const int monitorIndex = QGuiApplication::screens().indexOf(screen);

        auto& fileSettings = FileSettingsManager::instance();
        FilenameTemplateEngine::Context context;
        context.type = QStringLiteral("Screenshot");
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

        filePath = FilenameTemplateEngine::buildUniqueFilePath(
            savePath, fileSettings.loadFilenameTemplate(), context);
    }

    // Ensure directory exists
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    // Convert to QImage and strip color profile (fixes libpng iCCP error on macOS)
    QImage cleanImage = screenshot.toImage().convertToFormat(QImage::Format_RGB32);
    cleanImage.setColorSpace(QColorSpace());

    // Process pending events to ensure Qt subsystems are ready
    QCoreApplication::processEvents();

    if (!cleanImage.save(filePath, "PNG")) {
        return CLIResult::error(
            CLIResult::Code::FileError, QString("Failed to save screenshot to: %1").arg(filePath));
    }

    return CLIResult::success(QString("Screenshot saved to: %1").arg(filePath));
}

} // namespace CLI
} // namespace SnapTray
