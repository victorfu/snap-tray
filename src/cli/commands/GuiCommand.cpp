#include "cli/commands/GuiCommand.h"

namespace SnapTray {
namespace CLI {

QString GuiCommand::name() const { return "gui"; }

QString GuiCommand::description() const { return "Open region capture GUI"; }

void GuiCommand::setupOptions(QCommandLineParser& parser)
{
    parser.addOption({{"d", "delay"}, "Delay in milliseconds before capture", "ms", "0"});
    parser.addOption({{"p", "path"}, "Save directory (overrides settings)", "dir"});
    parser.addOption({{"c", "clipboard"}, "Copy to clipboard after capture"});
    parser.addOption({{"q", "quick-pin"}, "Quick pin mode (no toolbar)"});
}

CLIResult GuiCommand::execute(const QCommandLineParser& /*parser*/)
{
    // This command is executed via IPC, not locally
    return CLIResult::success("Region capture started");
}

QJsonObject GuiCommand::buildIPCMessage(const QCommandLineParser& parser) const
{
    QJsonObject options;

    if (parser.isSet("delay")) {
        options["delay"] = parser.value("delay").toInt();
    }
    if (parser.isSet("path")) {
        options["path"] = parser.value("path");
    }
    if (parser.isSet("clipboard")) {
        options["clipboard"] = true;
    }
    if (parser.isSet("quick-pin")) {
        options["quickPin"] = true;
    }

    return options;
}

} // namespace CLI
} // namespace SnapTray
