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

CLIResult RecordCommand::execute(const QCommandLineParser& parser)
{
    if (parser.isSet("screen")) {
        const QString screenValue = parser.value("screen");
        bool ok = false;
        int screenNum = screenValue.toInt(&ok);
        if (!ok || screenNum < 0) {
            return CLIResult::error(
                CLIResult::Code::InvalidArguments,
                QString("Invalid screen number: %1").arg(screenValue));
        }
    }

    const QStringList positional = parser.positionalArguments();
    if (positional.size() > 1) {
        return CLIResult::error(
            CLIResult::Code::InvalidArguments,
            QString("Too many arguments for record command: %1").arg(positional.join(' ')));
    }

    const QString action = positional.isEmpty()
        ? QString()
        : positional.first().trimmed().toLower();
    const bool validAction =
        action.isEmpty() || action == "start" || action == "stop" || action == "toggle";
    if (!validAction) {
        return CLIResult::error(
            CLIResult::Code::InvalidArguments,
            QString("Invalid record action: %1 (allowed: start, stop, toggle)").arg(action));
    }

    // This command is executed via IPC, not locally
    return CLIResult::success("Recording started");
}

QJsonObject RecordCommand::buildIPCMessage(const QCommandLineParser& parser) const
{
    QJsonObject options;

    QStringList positional = parser.positionalArguments();
    if (!positional.isEmpty()) {
        const QString action = positional.first().trimmed().toLower();
        if (action == "start" || action == "stop" || action == "toggle") {
            options["action"] = action;
        }
    }

    if (parser.isSet("screen")) {
        bool ok = false;
        int screenNum = parser.value("screen").toInt(&ok);
        if (ok) {
            options["screen"] = screenNum;
        }
    }

    return options;
}

} // namespace CLI
} // namespace SnapTray
