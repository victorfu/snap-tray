#include "cli/commands/ScreenCommand.h"

#include "cli/CaptureOutputHelper.h"

#include <QGuiApplication>
#include <QPixmap>
#include <QScreen>
#include <QTextStream>

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

    // Delay before capture to preserve CLI behavior.
    applyOptionalDelay(delay);

    QScreen* screen = screens.at(screenNum);

    // Capture screen
    QPixmap screenshot = screen->grabWindow(0);

    if (screenshot.isNull()) {
        return CLIResult::error(CLIResult::Code::GeneralError, "Failed to capture screen");
    }

    CaptureOutputOptions options;
    options.savePath = savePath;
    options.outputFile = outputFile;
    options.toClipboard = toClipboard;
    options.toRaw = toRaw;

    CaptureMetadata metadata;
    metadata.sourceScreen = screen;

    return emitCaptureOutput(screenshot, options, metadata);
}

} // namespace CLI
} // namespace SnapTray
