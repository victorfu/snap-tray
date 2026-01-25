#ifndef REGION_COMMAND_H
#define REGION_COMMAND_H

#include "cli/CLICommand.h"

namespace SnapTray {
namespace CLI {

/**
 * @brief Capture specified region (no GUI)
 */
class RegionCommand : public CLICommand
{
public:
    QString name() const override;
    QString description() const override;
    void setupOptions(QCommandLineParser& parser) override;
    CLIResult execute(const QCommandLineParser& parser) override;
};

} // namespace CLI
} // namespace SnapTray

#endif // REGION_COMMAND_H
