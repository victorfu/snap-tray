#include "cli/commands/ScreenCommand.h"

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
#include <QTextStream>
#include <QThread>

namespace SnapTray {
namespace CLI {

QString ScreenCommand::name() const { return "screen"; }

QString ScreenCommand::description() const { return "Capture specified screen"; }

void ScreenCommand::setupOptions(QCommandLineParser& parser)
{
    parser.addOption({{"n", "screen"}, "Screen number (required, 0 = primary)", "num"});
    parser.addOption({{"d", "delay"}, "Delay in milliseconds before capture", "ms", "0"});
    parser.addOption({{"p", "path"}, "Save directory", "dir"});
    parser.addOption({{"o", "output"}, "Output file path", "file"});
    parser.addOption({{"c", "clipboard"}, "Copy to clipboard"});
    parser.addOption({"cursor", "Include mouse cursor"});
    parser.addOption({"raw", "Output raw PNG to stdout"});
    parser.addOption({"list", "List all available screens"});
}

CLIResult ScreenCommand::execute(const QCommandLineParser& parser)
{
    QList<QScreen*> screens = QGuiApplication::screens();

    // List screens
    if (parser.isSet("list")) {
        QString output;
        QTextStream out(&output);
        out << "Available screens:\n";
        for (int i = 0; i < screens.size(); ++i) {
            QScreen* s = screens.at(i);
            QRect geo = s->geometry();
            out << QString("  %1: %2 (%3x%4 at %5,%6)")
                       .arg(i)
                       .arg(s->name())
                       .arg(geo.width())
                       .arg(geo.height())
                       .arg(geo.x())
                       .arg(geo.y());
            if (s == QGuiApplication::primaryScreen()) {
                out << " [Primary]";
            }
            out << "\n";
        }
        return CLIResult::success(output);
    }

    // Screen number is required for capture
    // Support both: "screen 0" (positional) and "screen -n 0" (option)
    int screenNum = -1;
    bool hasScreenNumber = false;
    if (parser.isSet("screen")) {
        hasScreenNumber = true;
        const QString screenValue = parser.value("screen");
        bool ok = false;
        screenNum = screenValue.toInt(&ok);
        if (!ok) {
            return CLIResult::error(
                CLIResult::Code::InvalidArguments,
                QString("Invalid screen number: %1").arg(screenValue));
        }
    }
    else {
        QStringList positionalArgs = parser.positionalArguments();
        // After command name is removed, positionalArgs[0] would be the screen number
        if (!positionalArgs.isEmpty()) {
            hasScreenNumber = true;
            const QString screenValue = positionalArgs.at(0);
            bool ok = false;
            screenNum = screenValue.toInt(&ok);
            if (!ok) {
                return CLIResult::error(
                    CLIResult::Code::InvalidArguments,
                    QString("Invalid screen number: %1").arg(screenValue));
            }
        }
    }

    if (!hasScreenNumber) {
        return CLIResult::error(
            CLIResult::Code::InvalidArguments,
            "Screen number required. Use --list to see available screens.\n"
            "Usage: snaptray screen <number> [options] or snaptray screen -n <number> [options]");
    }

    if (screenNum < 0 || screenNum >= screens.size()) {
        return CLIResult::error(
            CLIResult::Code::InvalidArguments,
            QString("Invalid screen number: %1 (available: 0-%2)")
                .arg(screenNum)
                .arg(screens.size() - 1));
    }

    // Parse other arguments
    const QString delayValue = parser.value("delay");
    bool delayOk = false;
    int delay = delayValue.toInt(&delayOk);
    if (!delayOk) {
        return CLIResult::error(
            CLIResult::Code::InvalidArguments,
            QString("Invalid delay value: %1").arg(delayValue));
    }
    QString savePath = parser.value("path");
    QString outputFile = parser.value("output");
    bool toClipboard = parser.isSet("clipboard");
    bool toRaw = parser.isSet("raw");

    // Delay
    if (delay > 0) {
        QThread::msleep(delay);
    }

    QScreen* screen = screens.at(screenNum);

    // Capture screen
    QPixmap screenshot = screen->grabWindow(0);

    if (screenshot.isNull()) {
        return CLIResult::error(CLIResult::Code::GeneralError, "Failed to capture screen");
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
