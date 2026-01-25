#ifndef SCREEN_COMMAND_H
#define SCREEN_COMMAND_H

#include "cli/CLICommand.h"

namespace SnapTray {
namespace CLI {

/**
 * @brief Capture specified screen (no GUI)
 */
class ScreenCommand : public CLICommand
{
public:
    QString name() const override;
    QString description() const override;
    void setupOptions(QCommandLineParser& parser) override;
    CLIResult execute(const QCommandLineParser& parser) override;
};

} // namespace CLI
} // namespace SnapTray

#endif // SCREEN_COMMAND_H
