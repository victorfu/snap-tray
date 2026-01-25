#include "cli/commands/CanvasCommand.h"

namespace SnapTray {
namespace CLI {

QString CanvasCommand::name() const { return "canvas"; }

QString CanvasCommand::description() const { return "Open Screen Canvas mode"; }

void CanvasCommand::setupOptions(QCommandLineParser& parser)
{
    parser.addOption({{"n", "screen"}, "Screen number (default: current)", "num"});
}

CLIResult CanvasCommand::execute(const QCommandLineParser& /*parser*/)
{
    // This command is executed via IPC, not locally
    return CLIResult::success("Screen canvas started");
}

QJsonObject CanvasCommand::buildIPCMessage(const QCommandLineParser& parser) const
{
    QJsonObject options;

    if (parser.isSet("screen")) {
        options["screen"] = parser.value("screen").toInt();
    }

    return options;
}

} // namespace CLI
} // namespace SnapTray
