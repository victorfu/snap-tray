#ifndef CLI_HANDLER_H
#define CLI_HANDLER_H

#include "CLICommand.h"
#include "CLIResult.h"

#include <QString>
#include <QStringList>
#include <map>
#include <memory>

namespace SnapTray {
namespace CLI {

/**
 * @brief CLI handler for parsing and executing commands
 *
 * Supports two execution modes:
 * 1. Local execution (full, screen, region - no GUI needed)
 * 2. IPC execution (gui, canvas, record - sends to main instance)
 */
class CLIHandler
{
public:
    CLIHandler();
    ~CLIHandler();

    /**
     * @brief Parse and execute command line
     * @param arguments Command line arguments (including program name)
     * @return Execution result
     */
    CLIResult process(const QStringList& arguments);

    /**
     * @brief Check if there are command line arguments
     */
    static bool hasArguments(const QStringList& arguments);

    /**
     * @brief Get main help text
     */
    QString getHelpText() const;

    /**
     * @brief Get version text
     */
    static QString getVersionText();

private:
    void registerCommands();
    CLICommand* findCommand(const QString& name) const;

    std::map<QString, CLICommandPtr> m_commands;
};

} // namespace CLI
} // namespace SnapTray

#endif // CLI_HANDLER_H
