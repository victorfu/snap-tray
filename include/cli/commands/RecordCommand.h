#ifndef RECORD_COMMAND_H
#define RECORD_COMMAND_H

#include "cli/CLICommand.h"

namespace SnapTray {
namespace CLI {

/**
 * @brief Screen recording
 */
class RecordCommand : public CLICommand
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

#endif // RECORD_COMMAND_H
