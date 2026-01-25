#ifndef CLI_COMMAND_H
#define CLI_COMMAND_H

#include "CLIResult.h"

#include <QCommandLineParser>
#include <QJsonObject>
#include <QString>
#include <memory>

namespace SnapTray {
namespace CLI {

/**
 * @brief Abstract base class for CLI commands
 */
class CLICommand
{
public:
    virtual ~CLICommand() = default;

    /**
     * @brief Command name (e.g., "gui", "full", "screen")
     */
    virtual QString name() const = 0;

    /**
     * @brief Command description for help text
     */
    virtual QString description() const = 0;

    /**
     * @brief Configure command options
     */
    virtual void setupOptions(QCommandLineParser& parser) = 0;

    /**
     * @brief Execute the command
     * @param parser Parsed command line
     * @return Execution result
     */
    virtual CLIResult execute(const QCommandLineParser& parser) = 0;

    /**
     * @brief Whether this command requires GUI (needs IPC to main instance)
     * @param parser Parsed command line (allows checking options)
     */
    virtual bool requiresGUI(const QCommandLineParser& /*parser*/) const { return false; }

    /**
     * @brief Whether to wait for response from main instance
     */
    virtual bool requiresResponse() const { return false; }

    /**
     * @brief Build IPC message for GUI commands
     */
    virtual QJsonObject buildIPCMessage(const QCommandLineParser& /*parser*/) const
    {
        return QJsonObject();
    }
};

using CLICommandPtr = std::unique_ptr<CLICommand>;

} // namespace CLI
} // namespace SnapTray

#endif // CLI_COMMAND_H
