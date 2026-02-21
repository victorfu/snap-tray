#include "cli/commands/PinCommand.h"

#include <QFileInfo>
#include <QImageReader>

namespace SnapTray {
namespace CLI {

QString PinCommand::name() const { return "pin"; }

QString PinCommand::description() const { return "Pin image to screen"; }

void PinCommand::setupOptions(QCommandLineParser& parser)
{
    parser.addOption({{"f", "file"}, "Image file path", "path"});
    parser.addOption({{"c", "clipboard"}, "Pin from clipboard"});
    parser.addOption({{"x", "pos-x"}, "Window X position", "x"});
    parser.addOption({{"y", "pos-y"}, "Window Y position", "y"});
    parser.addOption({"center", "Center window on screen"});
}

CLIResult PinCommand::execute(const QCommandLineParser& parser)
{
    // Validate arguments
    bool hasFile = parser.isSet("file");
    bool hasClipboard = parser.isSet("clipboard");

    if (!hasFile && !hasClipboard) {
        return CLIResult::error(
            CLIResult::Code::InvalidArguments, "Either --file or --clipboard is required");
    }

    if (hasFile && hasClipboard) {
        return CLIResult::error(
            CLIResult::Code::InvalidArguments, "Cannot use both --file and --clipboard");
    }

    // Validate file exists
    if (hasFile) {
        QString filePath = parser.value("file");
        QFileInfo fileInfo(filePath);
        if (!fileInfo.exists()) {
            return CLIResult::error(
                CLIResult::Code::FileError, QString("File not found: %1").arg(filePath));
        }

        QImageReader reader(fileInfo.absoluteFilePath());
        if (!reader.canRead()) {
            return CLIResult::error(
                CLIResult::Code::FileError,
                QString("Unsupported or invalid image file: %1").arg(filePath));
        }
    }

    if (parser.isSet("pos-x")) {
        const QString xValue = parser.value("pos-x");
        bool ok = false;
        xValue.toInt(&ok);
        if (!ok) {
            return CLIResult::error(
                CLIResult::Code::InvalidArguments,
                QString("Invalid x position: %1").arg(xValue));
        }
    }

    if (parser.isSet("pos-y")) {
        const QString yValue = parser.value("pos-y");
        bool ok = false;
        yValue.toInt(&ok);
        if (!ok) {
            return CLIResult::error(
                CLIResult::Code::InvalidArguments,
                QString("Invalid y position: %1").arg(yValue));
        }
    }

    // This command is executed via IPC
    return CLIResult::success("Pin window created");
}

QJsonObject PinCommand::buildIPCMessage(const QCommandLineParser& parser) const
{
    QJsonObject options;

    if (parser.isSet("file")) {
        // Convert to absolute path
        QString filePath = parser.value("file");
        QFileInfo fileInfo(filePath);
        options["file"] = fileInfo.absoluteFilePath();
    }
    if (parser.isSet("clipboard")) {
        options["clipboard"] = true;
    }
    if (parser.isSet("pos-x")) {
        bool ok = false;
        int xPos = parser.value("pos-x").toInt(&ok);
        if (ok) {
            options["x"] = xPos;
        }
    }
    if (parser.isSet("pos-y")) {
        bool ok = false;
        int yPos = parser.value("pos-y").toInt(&ok);
        if (ok) {
            options["y"] = yPos;
        }
    }
    if (parser.isSet("center")) {
        options["center"] = true;
    }

    return options;
}

} // namespace CLI
} // namespace SnapTray
