#include "cli/commands/CanvasCommand.h"

namespace SnapTray {
namespace CLI {

QString CanvasCommand::name() const { return "canvas"; }

QString CanvasCommand::description() const { return "Open Screen Canvas mode"; }

void CanvasCommand::setupOptions(QCommandLineParser& /*parser*/)
{
}

CLIResult CanvasCommand::execute(const QCommandLineParser& /*parser*/)
{
    // This command is executed via IPC, not locally
    return CLIResult::success("Screen canvas started");
}

QJsonObject CanvasCommand::buildIPCMessage(const QCommandLineParser& /*parser*/) const
{
    return {};
}

} // namespace CLI
} // namespace SnapTray
