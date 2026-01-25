#ifndef CANVAS_COMMAND_H
#define CANVAS_COMMAND_H

#include "cli/CLICommand.h"

namespace SnapTray {
namespace CLI {

/**
 * @brief Open Screen Canvas mode
 */
class CanvasCommand : public CLICommand
{
public:
    QString name() const override;
    QString description() const override;
    void setupOptions(QCommandLineParser& parser) override;
    CLIResult execute(const QCommandLineParser& parser) override;
    bool requiresGUI(const QCommandLineParser&) const override { return true; }
    QJsonObject buildIPCMessage(const QCommandLineParser& parser) const override;
};

} // namespace CLI
} // namespace SnapTray

#endif // CANVAS_COMMAND_H
