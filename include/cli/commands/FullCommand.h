#ifndef FULL_COMMAND_H
#define FULL_COMMAND_H

#include "cli/CLICommand.h"

namespace SnapTray {
namespace CLI {

/**
 * @brief Full screen capture (no GUI)
 */
class FullCommand : public CLICommand
{
public:
    QString name() const override;
    QString description() const override;
    void setupOptions(QCommandLineParser& parser) override;
    CLIResult execute(const QCommandLineParser& parser) override;
};

} // namespace CLI
} // namespace SnapTray

#endif // FULL_COMMAND_H
