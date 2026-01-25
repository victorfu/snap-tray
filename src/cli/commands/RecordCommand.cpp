#include "cli/commands/RecordCommand.h"

namespace SnapTray {
namespace CLI {

QString RecordCommand::name() const { return "record"; }

QString RecordCommand::description() const { return "Record screen"; }

void RecordCommand::setupOptions(QCommandLineParser& parser)
{
    parser.addPositionalArgument("action", "Action: start, stop, toggle, status", "[action]");
    parser.addOption({{"r", "region"}, "Recording region (x,y,width,height)", "rect"});
    parser.addOption({{"n", "screen"}, "Screen number", "num"});
    parser.addOption({{"p", "path"}, "Save directory", "dir"});
    parser.addOption({{"o", "output"}, "Output file path", "file"});
    parser.addOption({{"f", "format"}, "Output format (mp4|gif|webp)", "fmt", "mp4"});
    parser.addOption({"fps", "Frame rate", "rate", "30"});
    parser.addOption({"no-countdown", "Skip countdown"});
    parser.addOption({"audio", "Enable audio recording"});
    parser.addOption({"audio-source", "Audio source (mic|system|both)", "src", "system"});
}

CLIResult RecordCommand::execute(const QCommandLineParser& /*parser*/)
{
    // This command is executed via IPC, not locally
    return CLIResult::success("Recording started");
}

QJsonObject RecordCommand::buildIPCMessage(const QCommandLineParser& parser) const
{
    QJsonObject options;

    // Capture action (start/stop/toggle/status)
    QStringList positional = parser.positionalArguments();
    if (!positional.isEmpty()) {
        options["action"] = positional.first();
    }

    if (parser.isSet("region")) {
        options["region"] = parser.value("region");
    }
    if (parser.isSet("screen")) {
        options["screen"] = parser.value("screen").toInt();
    }
    if (parser.isSet("path")) {
        options["path"] = parser.value("path");
    }
    if (parser.isSet("output")) {
        options["output"] = parser.value("output");
    }
    if (parser.isSet("format")) {
        options["format"] = parser.value("format");
    }
    if (parser.isSet("fps")) {
        options["fps"] = parser.value("fps").toInt();
    }
    if (parser.isSet("no-countdown")) {
        options["noCountdown"] = true;
    }
    if (parser.isSet("audio")) {
        options["audio"] = true;
    }
    if (parser.isSet("audio-source")) {
        options["audioSource"] = parser.value("audio-source");
    }

    return options;
}

} // namespace CLI
} // namespace SnapTray
