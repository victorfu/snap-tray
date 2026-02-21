#include "cli/commands/FullCommand.h"

#include "cli/CaptureOutputHelper.h"

#include <QCursor>
#include <QGuiApplication>
#include <QPixmap>
#include <QScreen>

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
    int screenNum = -1;
    if (parser.isSet("screen")) {
        const QString screenValue = parser.value("screen");
        bool screenOk = false;
        screenNum = screenValue.toInt(&screenOk);
        if (!screenOk) {
            return CLIResult::error(
                CLIResult::Code::InvalidArguments,
                QString("Invalid screen number: %1").arg(screenValue));
        }
    }

    // Delay before selecting screen/capturing to preserve CLI behavior.
    applyOptionalDelay(delay);

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
