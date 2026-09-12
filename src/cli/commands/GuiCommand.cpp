#include "cli/commands/GuiCommand.h"

namespace SnapTray {
namespace CLI {

QString GuiCommand::name() const { return "gui"; }

QString GuiCommand::description() const { return "Open region capture GUI"; }

void GuiCommand::setupOptions(QCommandLineParser& parser)
{
    parser.addOption({{"d", "delay"}, "Delay in milliseconds before capture", "ms", "0"});
}

CLIResult GuiCommand::execute(const QCommandLineParser& parser)
{
    const QString value = parser.value("delay");
    bool ok = false;
    const int delay = value.toInt(&ok);
    if (!ok || delay < 0) {
        return CLIResult::error(CLIResult::Code::InvalidArguments,
                                QString("Invalid delay value: %1").arg(value));
    }
    // Validate before the CLI handler dispatches IPC.
    return CLIResult::success("Region capture started");
}

QJsonObject GuiCommand::buildIPCMessage(const QCommandLineParser& parser) const
{
    QJsonObject options;

    if (parser.isSet("delay")) {
        bool ok = false;
        const int delay = parser.value("delay").toInt(&ok);
        if (ok && delay >= 0) options["delay"] = delay;
    }

    return options;
}

} // namespace CLI
} // namespace SnapTray
