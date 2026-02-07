#include "cli/commands/RecordCommand.h"

namespace SnapTray {
namespace CLI {

QString RecordCommand::name() const { return "record"; }

QString RecordCommand::description() const { return "Record screen"; }

void RecordCommand::setupOptions(QCommandLineParser& parser)
{
    parser.addPositionalArgument("action", "Action: start, stop, toggle", "[action]");
    parser.addOption({{"n", "screen"}, "Screen number", "num"});
}

CLIResult RecordCommand::execute(const QCommandLineParser& /*parser*/)
{
    // This command is executed via IPC, not locally
    return CLIResult::success("Recording started");
}

QJsonObject RecordCommand::buildIPCMessage(const QCommandLineParser& parser) const
{
    QJsonObject options;

    QStringList positional = parser.positionalArguments();
    if (!positional.isEmpty()) {
        options["action"] = positional.first();
    }

    if (parser.isSet("screen")) {
        options["screen"] = parser.value("screen").toInt();
    }

    return options;
}

} // namespace CLI
} // namespace SnapTray
