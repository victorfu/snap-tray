#include <QtTest>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTextStream>

namespace {

constexpr int kProcessStartTimeoutMs = 3000;
constexpr int kOutputTimeoutMs = 5000;
constexpr int kProcessFinishTimeoutMs = 5000;
constexpr int kActivationSafetyTimeoutMs = 10000;
constexpr int kActivationFinishTimeoutMs = kActivationSafetyTimeoutMs + 2000;

bool outputContainsLine(const QString& output, const QString& expectedLine)
{
    const QStringList lines = output.split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts);
    return lines.contains(expectedLine);
}

bool sendCommand(QProcess& process, const QByteArray& command, int timeoutMs = 2000)
{
    const QByteArray line = command + '\n';
    if (process.write(line) != line.size()) {
        return false;
    }

    return process.bytesToWrite() == 0 || process.waitForBytesWritten(timeoutMs);
}

bool exitedSuccessfully(const QProcess& process)
{
    return process.state() == QProcess::NotRunning
        && process.exitStatus() == QProcess::NormalExit
        && process.exitCode() == 0;
}

QString processDiagnostics(const QString& label, const QProcess& process, const QString& output)
{
    return QStringLiteral(
               "%1: state=%2 exitStatus=%3 exitCode=%4 error=\"%5\" output=\"%6\"")
        .arg(label)
        .arg(static_cast<int>(process.state()))
        .arg(static_cast<int>(process.exitStatus()))
        .arg(process.exitCode())
        .arg(process.errorString(), output.trimmed());
}

QString combinedDiagnostics(const QProcess& processA, const QString& outputA,
                            const QProcess& processB, const QString& outputB)
{
    return processDiagnostics(QStringLiteral("Process A"), processA, outputA)
        + QLatin1Char('\n')
        + processDiagnostics(QStringLiteral("Process B"), processB, outputB);
}

void printDiagnostics(const QString& diagnostics)
{
    QTextStream(stderr) << diagnostics << Qt::endl;
}

void stopProcessIfRunning(QProcess& process)
{
    if (process.state() == QProcess::NotRunning) {
        return;
    }

    process.terminate();
    if (!process.waitForFinished(1500)) {
        process.kill();
        process.waitForFinished(1500);
    }
}

class ScopedProcessStop
{
public:
    explicit ScopedProcessStop(QProcess& process)
        : m_process(process)
    {
    }

    ~ScopedProcessStop()
    {
        stopProcessIfRunning(m_process);
    }

private:
    QProcess& m_process;
};

} // namespace

class tst_SingleInstanceGuardRace : public QObject
{
    Q_OBJECT

private slots:
    void testConcurrentStart_ExactlyOnePrimary();
    void testSecondaryCanActivatePrimary();

private:
    static QString helperExecutablePath();
    static QString testAppId(const QString& suffix);
    static QString readProcessOutput(QProcess& process);
    static QString extractLockStateLine(const QString& output);
    static int extractActivateCount(const QString& output);
    static bool waitForOutputLine(
        QProcess& process, QString& capturedOutput, const QString& expectedLine, int timeoutMs);
    static QString waitForLockState(QProcess& process, QString& capturedOutput, int timeoutMs);
};

QString tst_SingleInstanceGuardRace::helperExecutablePath()
{
#ifdef Q_OS_WIN
    const QString helperName = "IPC_SingleInstanceGuardProbe.exe";
#else
    const QString helperName = "IPC_SingleInstanceGuardProbe";
#endif
    return QDir(QCoreApplication::applicationDirPath()).filePath(helperName);
}

QString tst_SingleInstanceGuardRace::testAppId(const QString& suffix)
{
    return QString("com.victorfu.snaptray.test.race.%1.%2.%3")
        .arg(QCoreApplication::applicationPid())
        .arg(QDateTime::currentMSecsSinceEpoch())
        .arg(suffix);
}

QString tst_SingleInstanceGuardRace::readProcessOutput(QProcess& process)
{
    return QString::fromLocal8Bit(process.readAllStandardOutput())
        + QString::fromLocal8Bit(process.readAllStandardError());
}

QString tst_SingleInstanceGuardRace::extractLockStateLine(const QString& output)
{
    const QStringList lines = output.split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        if (line == "LOCKED" || line == "UNLOCKED") {
            return line;
        }
    }
    return {};
}

int tst_SingleInstanceGuardRace::extractActivateCount(const QString& output)
{
    const QStringList lines = output.split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        if (line.startsWith("ACTIVATE_COUNT=")) {
            bool ok = false;
            const int count = line.mid(QString("ACTIVATE_COUNT=").size()).toInt(&ok);
            if (ok) {
                return count;
            }
        }
    }
    return -1;
}

bool tst_SingleInstanceGuardRace::waitForOutputLine(
    QProcess& process, QString& capturedOutput, const QString& expectedLine, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs) {
        process.waitForReadyRead(100);
        capturedOutput += readProcessOutput(process);

        if (outputContainsLine(capturedOutput, expectedLine)) {
            return true;
        }

        if (process.state() == QProcess::NotRunning) {
            break;
        }
    }

    capturedOutput += readProcessOutput(process);
    return outputContainsLine(capturedOutput, expectedLine);
}

QString tst_SingleInstanceGuardRace::waitForLockState(
    QProcess& process, QString& capturedOutput, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs) {
        process.waitForReadyRead(100);
        capturedOutput += readProcessOutput(process);

        const QString state = extractLockStateLine(capturedOutput);
        if (!state.isEmpty()) {
            return state;
        }

        if (process.state() == QProcess::NotRunning) {
            break;
        }
    }

    capturedOutput += readProcessOutput(process);
    return extractLockStateLine(capturedOutput);
}

void tst_SingleInstanceGuardRace::testConcurrentStart_ExactlyOnePrimary()
{
    const QString helperPath = helperExecutablePath();
    QVERIFY2(QFileInfo::exists(helperPath), qPrintable(QString("Helper not found: %1").arg(helperPath)));

    const QString appId = testAppId("concurrent");

    // Preflight: ensure environment can acquire the primary lock at all.
    {
        QProcess preflightProcess;
        ScopedProcessStop preflightCleanup(preflightProcess);
        preflightProcess.setProgram(helperPath);
        preflightProcess.setArguments({"--app-id", testAppId("concurrent-preflight")});
        preflightProcess.start();
        QVERIFY2(preflightProcess.waitForStarted(kProcessStartTimeoutMs),
                 qPrintable(processDiagnostics(
                     QStringLiteral("Preflight"), preflightProcess, readProcessOutput(preflightProcess))));
        QVERIFY2(preflightProcess.waitForFinished(kProcessFinishTimeoutMs),
                 qPrintable(processDiagnostics(
                     QStringLiteral("Preflight"), preflightProcess, readProcessOutput(preflightProcess))));
        QString preflightOutput;
        preflightOutput += readProcessOutput(preflightProcess);
        const QString preflightState = extractLockStateLine(preflightOutput);
        if (preflightState != "LOCKED") {
            QSKIP(qPrintable(QStringLiteral(
                                 "Single-instance lock is unavailable in this environment. %1")
                                 .arg(processDiagnostics(
                                     QStringLiteral("Preflight"), preflightProcess, preflightOutput))));
        }
        QVERIFY2(exitedSuccessfully(preflightProcess),
                 qPrintable(processDiagnostics(
                     QStringLiteral("Preflight"), preflightProcess, preflightOutput)));
    }

    const QStringList args = {
        "--app-id", appId, "--wait-for-start", "--wait-for-stop"
    };

    QProcess processA;
    ScopedProcessStop cleanupA(processA);
    processA.setProgram(helperPath);
    processA.setArguments(args);
    processA.start();

    QProcess processB;
    ScopedProcessStop cleanupB(processB);
    processB.setProgram(helperPath);
    processB.setArguments(args);
    processB.start();

    QString outputA;
    QString outputB;

    const bool processAStarted = processA.waitForStarted(kProcessStartTimeoutMs);
    outputA += readProcessOutput(processA);
    QVERIFY2(processAStarted,
             qPrintable(processDiagnostics(QStringLiteral("Process A"), processA, outputA)));

    const bool processBStarted = processB.waitForStarted(kProcessStartTimeoutMs);
    outputB += readProcessOutput(processB);
    QVERIFY2(processBStarted,
             qPrintable(processDiagnostics(QStringLiteral("Process B"), processB, outputB)));

    const bool processAWaiting = waitForOutputLine(
        processA, outputA, QStringLiteral("WAITING_FOR_START"), kOutputTimeoutMs);
    const bool processBWaiting = waitForOutputLine(
        processB, outputB, QStringLiteral("WAITING_FOR_START"), kOutputTimeoutMs);
    if (!processAWaiting || !processBWaiting) {
        printDiagnostics(combinedDiagnostics(processA, outputA, processB, outputB));
    }
    QVERIFY2(processAWaiting && processBWaiting,
             qPrintable(combinedDiagnostics(processA, outputA, processB, outputB)));

    const bool startSentToA = sendCommand(processA, QByteArrayLiteral("START"));
    const bool startSentToB = sendCommand(processB, QByteArrayLiteral("START"));
    if (!startSentToA || !startSentToB) {
        printDiagnostics(combinedDiagnostics(processA, outputA, processB, outputB));
    }
    QVERIFY2(startSentToA && startSentToB,
             qPrintable(combinedDiagnostics(processA, outputA, processB, outputB)));

    const QString stateA = waitForLockState(processA, outputA, kOutputTimeoutMs);
    const QString stateB = waitForLockState(processB, outputB, kOutputTimeoutMs);

    if (stateA.isEmpty() || stateB.isEmpty()) {
        printDiagnostics(combinedDiagnostics(processA, outputA, processB, outputB));
    }
    QVERIFY2(!stateA.isEmpty() && !stateB.isEmpty(),
             qPrintable(combinedDiagnostics(processA, outputA, processB, outputB)));

    const int lockedCount = (stateA == "LOCKED" ? 1 : 0) + (stateB == "LOCKED" ? 1 : 0);
    if (lockedCount != 1) {
        printDiagnostics(combinedDiagnostics(processA, outputA, processB, outputB));
    }
    QCOMPARE(lockedCount, 1);

    const bool stopSentToA = sendCommand(processA, QByteArrayLiteral("STOP"));
    const bool stopSentToB = sendCommand(processB, QByteArrayLiteral("STOP"));
    if (!stopSentToA || !stopSentToB) {
        printDiagnostics(combinedDiagnostics(processA, outputA, processB, outputB));
    }
    QVERIFY2(stopSentToA && stopSentToB,
             qPrintable(combinedDiagnostics(processA, outputA, processB, outputB)));

    const bool processAFinished = processA.waitForFinished(kProcessFinishTimeoutMs);
    const bool processBFinished = processB.waitForFinished(kProcessFinishTimeoutMs);
    outputA += readProcessOutput(processA);
    outputB += readProcessOutput(processB);
    if (!processAFinished || !processBFinished
        || !exitedSuccessfully(processA) || !exitedSuccessfully(processB)) {
        printDiagnostics(combinedDiagnostics(processA, outputA, processB, outputB));
    }
    QVERIFY2(processAFinished && processBFinished
                 && exitedSuccessfully(processA) && exitedSuccessfully(processB),
             qPrintable(combinedDiagnostics(processA, outputA, processB, outputB)));
}

void tst_SingleInstanceGuardRace::testSecondaryCanActivatePrimary()
{
    const QString helperPath = helperExecutablePath();
    QVERIFY2(QFileInfo::exists(helperPath), qPrintable(QString("Helper not found: %1").arg(helperPath)));

    const QString appId = testAppId("activate");

    QProcess primaryProcess;
    ScopedProcessStop primaryCleanup(primaryProcess);
    primaryProcess.setProgram(helperPath);
    primaryProcess.setArguments({
        "--app-id", appId,
        "--exit-on-activate",
        "--timeout-ms", QString::number(kActivationSafetyTimeoutMs)
    });
    primaryProcess.start();
    QVERIFY2(primaryProcess.waitForStarted(kProcessStartTimeoutMs),
             qPrintable(processDiagnostics(
                 QStringLiteral("Primary"), primaryProcess, readProcessOutput(primaryProcess))));

    QString primaryOutput;
    const QString primaryState = waitForLockState(primaryProcess, primaryOutput, kOutputTimeoutMs);
    if (primaryState != "LOCKED") {
        QSKIP(qPrintable(QStringLiteral(
                             "Primary lock acquisition is unavailable in this environment. %1")
                             .arg(processDiagnostics(
                                 QStringLiteral("Primary"), primaryProcess, primaryOutput))));
    }

    QProcess secondaryProcess;
    ScopedProcessStop secondaryCleanup(secondaryProcess);
    secondaryProcess.setProgram(helperPath);
    secondaryProcess.setArguments({"--app-id", appId, "--send-activate"});
    secondaryProcess.start();
    QVERIFY2(secondaryProcess.waitForStarted(kProcessStartTimeoutMs),
             qPrintable(processDiagnostics(
                 QStringLiteral("Secondary"), secondaryProcess, readProcessOutput(secondaryProcess))));

    const bool secondaryFinished = secondaryProcess.waitForFinished(kProcessFinishTimeoutMs);
    QString secondaryOutput = readProcessOutput(secondaryProcess);
    if (!secondaryFinished || !exitedSuccessfully(secondaryProcess)) {
        printDiagnostics(processDiagnostics(
            QStringLiteral("Secondary"), secondaryProcess, secondaryOutput));
    }
    QVERIFY2(secondaryFinished && exitedSuccessfully(secondaryProcess),
             qPrintable(processDiagnostics(
                 QStringLiteral("Secondary"), secondaryProcess, secondaryOutput)));

    const bool primaryFinished = primaryProcess.waitForFinished(kActivationFinishTimeoutMs);
    primaryOutput += readProcessOutput(primaryProcess);
    if (!primaryFinished || !exitedSuccessfully(primaryProcess)) {
        printDiagnostics(processDiagnostics(
            QStringLiteral("Primary"), primaryProcess, primaryOutput));
    }
    QVERIFY2(primaryFinished && exitedSuccessfully(primaryProcess),
             qPrintable(processDiagnostics(
                 QStringLiteral("Primary"), primaryProcess, primaryOutput)));

    const QString secondaryState = extractLockStateLine(secondaryOutput);
    QVERIFY2(secondaryState == QStringLiteral("UNLOCKED"),
             qPrintable(processDiagnostics(
                 QStringLiteral("Secondary"), secondaryProcess, secondaryOutput)));

    const int activateCount = extractActivateCount(primaryOutput);
    QVERIFY2(activateCount == 1,
             qPrintable(processDiagnostics(
                 QStringLiteral("Primary"), primaryProcess, primaryOutput)));
}

QTEST_GUILESS_MAIN(tst_SingleInstanceGuardRace)
#include "tst_SingleInstanceGuardRace.moc"
