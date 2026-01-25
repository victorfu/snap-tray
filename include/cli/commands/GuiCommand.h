#ifndef GUI_COMMAND_H
#define GUI_COMMAND_H

#include "cli/CLICommand.h"

namespace SnapTray {
namespace CLI {

/**
 * @brief Open region capture GUI (same as pressing F2)
 */
class GuiCommand : public CLICommand
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

#endif // GUI_COMMAND_H
