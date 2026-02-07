#include "SingleInstanceGuard.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription("SingleInstanceGuard probe helper");
    parser.addHelpOption();

    QCommandLineOption appIdOption("app-id", "Application identifier.", "app-id");
    QCommandLineOption holdMsOption("hold-ms", "How long to keep process alive (ms).", "hold-ms", "0");
    QCommandLineOption sendActivateOption("send-activate", "Send activation message after tryLock().");

    parser.addOption(appIdOption);
    parser.addOption(holdMsOption);
    parser.addOption(sendActivateOption);
    parser.process(app);

    QTextStream out(stdout);
    QTextStream err(stderr);

    const QString appId = parser.value(appIdOption);
    if (appId.isEmpty()) {
        err << "Missing --app-id" << Qt::endl;
        return 2;
    }

    bool holdMsOk = false;
    const int holdMs = parser.value(holdMsOption).toInt(&holdMsOk);
    if (!holdMsOk || holdMs < 0) {
        err << "Invalid --hold-ms value" << Qt::endl;
        return 3;
    }

    SingleInstanceGuard guard(appId);
    int activateCount = 0;
    QObject::connect(
        &guard, &SingleInstanceGuard::activateRequested, &app, [&activateCount]() { ++activateCount; });

    const bool locked = guard.tryLock();
    out << (locked ? "LOCKED" : "UNLOCKED") << Qt::endl;

    if (parser.isSet(sendActivateOption)) {
        guard.sendActivateMessage();
    }

    if (holdMs > 0) {
        QTimer::singleShot(holdMs, &app, &QCoreApplication::quit);
        app.exec();
    }

    out << "ACTIVATE_COUNT=" << activateCount << Qt::endl;
    return 0;
}
