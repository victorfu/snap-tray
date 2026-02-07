#include "cli/commands/GuiCommand.h"

namespace SnapTray {
namespace CLI {

QString GuiCommand::name() const { return "gui"; }

QString GuiCommand::description() const { return "Open region capture GUI"; }

void GuiCommand::setupOptions(QCommandLineParser& parser)
{
    parser.addOption({{"d", "delay"}, "Delay in milliseconds before capture", "ms", "0"});
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

    return options;
}

} // namespace CLI
} // namespace SnapTray
