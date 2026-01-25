#include "cli/commands/ConfigCommand.h"

#include "settings/Settings.h"

#include <QSettings>
#include <QTextStream>

namespace SnapTray {
namespace CLI {

QString ConfigCommand::name() const { return "config"; }

QString ConfigCommand::description() const { return "Open settings or manage configuration"; }

void ConfigCommand::setupOptions(QCommandLineParser& parser)
{
    parser.addOption({"get", "Get setting value", "key"});
    parser.addOption({"set", "Set setting value (use with positional arg)", "key"});
    parser.addOption({"list", "List all settings"});
    parser.addOption({"reset", "Reset to default values"});
    parser.addPositionalArgument("value", "Value to set (when using --set)");
}

bool ConfigCommand::requiresGUI(const QCommandLineParser& parser) const
{
    // Local operations don't need GUI
    if (parser.isSet("list") || parser.isSet("get") ||
        parser.isSet("set") || parser.isSet("reset")) {
        return false;
    }
    // Default: open settings UI requires GUI
    return true;
}

CLIResult ConfigCommand::execute(const QCommandLineParser& parser)
{
    QSettings settings = SnapTray::getSettings();

    // --list: List all settings
    if (parser.isSet("list")) {
        QString output;
        QTextStream out(&output);
        out << "Current settings:\n";

        QStringList keys = settings.allKeys();
        keys.sort();
        for (const QString& key : keys) {
            QVariant value = settings.value(key);
            out << QString("  %1 = %2\n").arg(key).arg(value.toString());
        }
        return CLIResult::success(output);
    }

    // --get: Get setting value
    if (parser.isSet("get")) {
        QString key = parser.value("get");
        if (!settings.contains(key)) {
            return CLIResult::error(
                CLIResult::Code::InvalidArguments, QString("Setting not found: %1").arg(key));
        }
        QVariant value = settings.value(key);
        return CLIResult::success(value.toString());
    }

    // --set: Set setting value
    if (parser.isSet("set")) {
        QString key = parser.value("set");
        QStringList positionalArgs = parser.positionalArguments();
        if (positionalArgs.isEmpty()) {
            return CLIResult::error(CLIResult::Code::InvalidArguments, "Value required for --set");
        }
        QString value = positionalArgs.first();
        settings.setValue(key, value);
        settings.sync();
        return CLIResult::success(QString("Set %1 = %2").arg(key, value));
    }

    // --reset: Reset to defaults
    if (parser.isSet("reset")) {
        settings.clear();
        settings.sync();
        return CLIResult::success("Settings reset to defaults");
    }

    // No options: open settings UI (via IPC)
    return CLIResult::success("Settings opened");
}

QJsonObject ConfigCommand::buildIPCMessage(const QCommandLineParser& /*parser*/) const
{
    QJsonObject options;
    options["action"] = "open";
    return options;
}

} // namespace CLI
} // namespace SnapTray
