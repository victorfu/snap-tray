// Prints a recording's pointer sidecar (#159): a summary, or with --events
// every move and button event in time order. Reads either the video path or
// the "<video>.cursor.jsonl" file itself; never changes anything.
#include "recording/CursorTimeline.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Inspect a SnapTray recording's pointer timeline."));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("events"), QStringLiteral("List every event in time order.")});
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("Recording or .cursor.jsonl sidecar."));
    parser.process(app);
    if (parser.positionalArguments().size() != 1) parser.showHelp(2);

    QString path = parser.positionalArguments().first();
    if (!path.endsWith(QLatin1String(SnapTray::CursorTimelineSidecar::kSuffix))) {
        path = SnapTray::CursorTimelineSidecar::pathFor(path);
    }
    QTextStream err(stderr);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        err << "cannot read " << path << ": " << file.errorString() << '\n';
        return 1;
    }
    QString error;
    const auto timeline = SnapTray::CursorTimeline::parse(file.readAll(), &error);
    if (!timeline) {
        err << path << ": " << error << '\n';
        return 1;
    }
    QTextStream(stdout) << timeline->report(parser.isSet(QStringLiteral("events")));
    return 0;
}
