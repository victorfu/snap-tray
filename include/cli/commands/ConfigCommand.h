#ifndef CONFIG_COMMAND_H
#define CONFIG_COMMAND_H

#include "cli/CLICommand.h"

namespace SnapTray {
namespace CLI {

/**
 * @brief Open settings or get/set configuration
 */
class ConfigCommand : public CLICommand
{
public:
    QString name() const override;
    QString description() const override;
    void setupOptions(QCommandLineParser& parser) override;
    CLIResult execute(const QCommandLineParser& parser) override;
    bool requiresGUI(const QCommandLineParser& parser) const override;
    QJsonObject buildIPCMessage(const QCommandLineParser& parser) const override;
};

} // namespace CLI
} // namespace SnapTray

#endif // CONFIG_COMMAND_H
