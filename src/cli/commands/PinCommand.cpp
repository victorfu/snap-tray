#include "cli/commands/PinCommand.h"

#include <QFileInfo>

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
        if (!QFileInfo::exists(filePath)) {
            return CLIResult::error(
                CLIResult::Code::FileError, QString("File not found: %1").arg(filePath));
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
        options["x"] = parser.value("pos-x").toInt();
    }
    if (parser.isSet("pos-y")) {
        options["y"] = parser.value("pos-y").toInt();
    }
    if (parser.isSet("center")) {
        options["center"] = true;
    }

    return options;
}

} // namespace CLI
} // namespace SnapTray
