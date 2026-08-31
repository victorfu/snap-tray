#include "SingleInstanceGuard.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>

namespace {

constexpr int kDefaultTimeoutMs = 10000;

bool waitForCommand(QTextStream& in, QTextStream& out, QTextStream& err,
                    const QString& marker, const QString& expectedCommand)
{
    out << marker << Qt::endl;
    const QString command = in.readLine();
    if (command == expectedCommand) {
        return true;
    }

    err << "Expected command '" << expectedCommand << "', got '" << command << "'" << Qt::endl;
    return false;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription("SingleInstanceGuard probe helper");
    parser.addHelpOption();

    QCommandLineOption appIdOption("app-id", "Application identifier.", "app-id");
    QCommandLineOption sendActivateOption("send-activate", "Send activation message after tryLock().");
    QCommandLineOption waitForStartOption(
        "wait-for-start", "Wait for a START line on stdin before attempting to acquire the lock.");
    QCommandLineOption waitForStopOption(
        "wait-for-stop", "Wait for a STOP line on stdin before releasing the lock.");
    QCommandLineOption exitOnActivateOption(
        "exit-on-activate", "Run the event loop and exit after receiving an activation message.");
    QCommandLineOption timeoutMsOption(
        "timeout-ms", "Safety timeout for --exit-on-activate.", "timeout-ms",
        QString::number(kDefaultTimeoutMs));

    parser.addOption(appIdOption);
    parser.addOption(sendActivateOption);
    parser.addOption(waitForStartOption);
    parser.addOption(waitForStopOption);
    parser.addOption(exitOnActivateOption);
    parser.addOption(timeoutMsOption);
    parser.process(app);

    QTextStream in(stdin);
    QTextStream out(stdout);
    QTextStream err(stderr);

    const QString appId = parser.value(appIdOption);
    if (appId.isEmpty()) {
        err << "Missing --app-id" << Qt::endl;
        return 2;
    }

    bool timeoutMsOk = false;
    const int timeoutMs = parser.value(timeoutMsOption).toInt(&timeoutMsOk);
    if (!timeoutMsOk || timeoutMs <= 0) {
        err << "Invalid --timeout-ms value" << Qt::endl;
        return 3;
    }

    if (parser.isSet(waitForStartOption)
        && !waitForCommand(in, out, err, QStringLiteral("WAITING_FOR_START"),
                           QStringLiteral("START"))) {
        return 4;
    }

    SingleInstanceGuard guard(appId);
    int activateCount = 0;
    bool activationTimedOut = false;
    QObject::connect(
        &guard, &SingleInstanceGuard::activateRequested, &app, [&]() {
            ++activateCount;
            out << "ACTIVATED" << Qt::endl;
            if (parser.isSet(exitOnActivateOption)) {
                QTimer::singleShot(0, &app, &QCoreApplication::quit);
            }
        });

    const bool locked = guard.tryLock();
    out << (locked ? "LOCKED" : "UNLOCKED") << Qt::endl;

    if (parser.isSet(sendActivateOption)) {
        guard.sendActivateMessage();
    }

    if (parser.isSet(exitOnActivateOption)) {
        if (!locked) {
            err << "--exit-on-activate requires the primary lock" << Qt::endl;
            return 5;
        }

        QTimer::singleShot(timeoutMs, &app, [&]() {
            activationTimedOut = true;
            app.quit();
        });
        app.exec();
    }

    if (parser.isSet(waitForStopOption)
        && !waitForCommand(in, out, err, QStringLiteral("WAITING_FOR_STOP"),
                           QStringLiteral("STOP"))) {
        return 6;
    }

    out << "ACTIVATE_COUNT=" << activateCount << Qt::endl;

    if (activationTimedOut) {
        err << "Timed out waiting for activation" << Qt::endl;
        return 7;
    }

    return 0;
}
