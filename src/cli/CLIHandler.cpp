#include "cli/CLIHandler.h"

#include "cli/IPCProtocol.h"
#include "cli/commands/CanvasCommand.h"
#include "cli/commands/ConfigCommand.h"
#include "cli/commands/FullCommand.h"
#include "cli/commands/GuiCommand.h"
#include "cli/commands/PinCommand.h"
#include "cli/commands/RecordCommand.h"
#include "cli/commands/RegionCommand.h"
#include "cli/commands/ScreenCommand.h"
#include "version.h"

#include <QCommandLineParser>
#include <QTextStream>

namespace SnapTray {
namespace CLI {

CLIHandler::CLIHandler() { registerCommands(); }

CLIHandler::~CLIHandler() = default;

void CLIHandler::registerCommands()
{
    auto addCmd = [this](CLICommandPtr cmd) { m_commands[cmd->name()] = std::move(cmd); };

    addCmd(std::make_unique<GuiCommand>());
    addCmd(std::make_unique<FullCommand>());
    addCmd(std::make_unique<ScreenCommand>());
    addCmd(std::make_unique<RegionCommand>());
    addCmd(std::make_unique<CanvasCommand>());
    addCmd(std::make_unique<RecordCommand>());
    addCmd(std::make_unique<PinCommand>());
    addCmd(std::make_unique<ConfigCommand>());
}

bool CLIHandler::hasArguments(const QStringList& arguments)
{
    // Exclude program name, check if there are other arguments
    return arguments.size() > 1;
}

CLIResult CLIHandler::process(const QStringList& arguments)
{
    if (arguments.size() < 2) {
        return CLIResult::error(CLIResult::Code::InvalidArguments, getHelpText());
    }

    const QString& cmdOrOption = arguments.at(1);

    // Handle global options
    if (cmdOrOption == "--help" || cmdOrOption == "-h") {
        return CLIResult::success(getHelpText());
    }
    if (cmdOrOption == "--version" || cmdOrOption == "-v") {
        return CLIResult::success(getVersionText());
    }

    // Find command
    CLICommand* command = findCommand(cmdOrOption);
    if (!command) {
        return CLIResult::error(
            CLIResult::Code::InvalidArguments,
            QString("Unknown command: %1\n\n%2").arg(cmdOrOption, getHelpText()));
    }

    // Setup and parse command arguments
    QCommandLineParser parser;
    parser.setApplicationDescription(command->description());
    parser.addHelpOption();

    command->setupOptions(parser);

    // Remove command name, keep remaining arguments
    QStringList cmdArgs = arguments;
    cmdArgs.removeAt(1); // Remove command name

    if (!parser.parse(cmdArgs)) {
        return CLIResult::error(CLIResult::Code::InvalidArguments, parser.errorText());
    }

    if (parser.isSet("help")) {
        return CLIResult::success(parser.helpText());
    }

    // GUI commands execute via IPC
    if (command->requiresGUI(parser)) {
        IPCProtocol ipc;
        if (!ipc.isMainInstanceRunning()) {
            return CLIResult::error(
                CLIResult::Code::InstanceError,
                "SnapTray is not running. Please start SnapTray first.");
        }

        IPCMessage msg;
        msg.command = command->name();
        msg.options = command->buildIPCMessage(parser);

        auto response = ipc.sendCommand(msg, command->requiresResponse());

        if (!response.success && !response.error.isEmpty()) {
            return CLIResult::error(CLIResult::Code::GeneralError, response.error);
        }
        return CLIResult::success(response.message);
    }

    // Local execution
    return command->execute(parser);
}

CLICommand* CLIHandler::findCommand(const QString& name) const
{
    auto it = m_commands.find(name.toLower());
    return it != m_commands.end() ? it->second.get() : nullptr;
}

QString CLIHandler::getHelpText() const
{
    QString help;
    QTextStream out(&help);

    out << "SnapTray - Screenshot & Recording Tool\n\n";
    out << "Usage: snaptray <command> [options]\n\n";
    out << "Commands:\n";

    // Sort commands for consistent display
    QStringList names;
    for (const auto& [name, cmd] : m_commands) {
        names.append(name);
    }
    names.sort();

    for (const QString& name : names) {
        const auto& cmd = m_commands.at(name);
        out << QString("  %1  %2\n").arg(name, -10).arg(cmd->description());
    }

    out << "\nGlobal Options:\n";
    out << "  -h, --help     Display this help message\n";
    out << "  -v, --version  Display version information\n";
    out << "\nUse 'snaptray <command> --help' for more information about a command.\n";

    return help;
}

QString CLIHandler::getVersionText() { return QString("SnapTray version %1").arg(SNAPTRAY_VERSION); }

} // namespace CLI
} // namespace SnapTray
