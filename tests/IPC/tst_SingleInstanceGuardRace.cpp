#include <QtTest>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLocalSocket>
#include <QProcess>
#include <QRegularExpression>

namespace {

QString serverNameForAppId(const QString& appId)
{
    return QString("snaptray-%1")
        .arg(QString(QCryptographicHash::hash(appId.toUtf8(), QCryptographicHash::Md5).toHex()));
}

bool canConnectToServer(const QString& serverName, int timeoutMs = 200)
{
    QLocalSocket socket;
    socket.connectToServer(serverName);
    const bool connected = socket.waitForConnected(timeoutMs);
    if (connected) {
        socket.disconnectFromServer();
        socket.waitForDisconnected(timeoutMs);
    }
    return connected;
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
    const QStringList args = {"--app-id", appId, "--hold-ms", "800"};

    // Preflight: ensure environment can acquire the primary lock at all.
    {
        QProcess preflightProcess;
        ScopedProcessStop preflightCleanup(preflightProcess);
        preflightProcess.setProgram(helperPath);
        preflightProcess.setArguments({"--app-id", testAppId("concurrent-preflight"), "--hold-ms", "250"});
        preflightProcess.start();
        QVERIFY(preflightProcess.waitForStarted(2000));
        QString preflightOutput;
        const QString preflightState = waitForLockState(preflightProcess, preflightOutput, 3000);
        if (preflightState != "LOCKED") {
            QSKIP(qPrintable(QString("Single-instance lock is unavailable in this environment. Output: %1")
                                 .arg(preflightOutput)));
        }
        QVERIFY(preflightProcess.waitForFinished(3000));
    }

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

    QVERIFY(processA.waitForStarted(2000));
    QVERIFY(processB.waitForStarted(2000));
    QVERIFY(processA.waitForFinished(5000));
    QVERIFY(processB.waitForFinished(5000));

    const QString outputA = readProcessOutput(processA);
    const QString outputB = readProcessOutput(processB);

    const QString stateA = extractLockStateLine(outputA);
    const QString stateB = extractLockStateLine(outputB);

    QVERIFY2(!stateA.isEmpty(), qPrintable(QString("Process A missing lock state. Output: %1").arg(outputA)));
    QVERIFY2(!stateB.isEmpty(), qPrintable(QString("Process B missing lock state. Output: %1").arg(outputB)));

    const int lockedCount = (stateA == "LOCKED" ? 1 : 0) + (stateB == "LOCKED" ? 1 : 0);
    QCOMPARE(lockedCount, 1);
}

void tst_SingleInstanceGuardRace::testSecondaryCanActivatePrimary()
{
    const QString helperPath = helperExecutablePath();
    QVERIFY2(QFileInfo::exists(helperPath), qPrintable(QString("Helper not found: %1").arg(helperPath)));

    const QString appId = testAppId("activate");

    QProcess primaryProcess;
    ScopedProcessStop primaryCleanup(primaryProcess);
    primaryProcess.setProgram(helperPath);
    primaryProcess.setArguments({"--app-id", appId, "--hold-ms", "1500"});
    primaryProcess.start();
    QVERIFY(primaryProcess.waitForStarted(2000));

    QString primaryOutput;
    const QString primaryState = waitForLockState(primaryProcess, primaryOutput, 3000);
    if (primaryState != "LOCKED") {
        QSKIP(qPrintable(QString("Primary lock acquisition is unavailable in this environment. Output: %1")
                             .arg(primaryOutput)));
    }

    const QString serverName = serverNameForAppId(appId);
    if (!canConnectToServer(serverName)) {
        QSKIP(qPrintable(QString("IPC endpoint is unavailable in this environment (%1).")
                             .arg(serverName)));
    }

    QProcess secondaryProcess;
    ScopedProcessStop secondaryCleanup(secondaryProcess);
    secondaryProcess.setProgram(helperPath);
    secondaryProcess.setArguments({"--app-id", appId, "--send-activate"});
    secondaryProcess.start();
    QVERIFY(secondaryProcess.waitForStarted(2000));
    QVERIFY(secondaryProcess.waitForFinished(3000));

    QVERIFY(primaryProcess.waitForFinished(5000));
    primaryOutput += readProcessOutput(primaryProcess);
    const QString secondaryOutput = readProcessOutput(secondaryProcess);
    const QString secondaryState = extractLockStateLine(secondaryOutput);
    QCOMPARE(secondaryState, QString("UNLOCKED"));

    const int activateCount = extractActivateCount(primaryOutput);
    QCOMPARE(activateCount, 1);
}

QTEST_MAIN(tst_SingleInstanceGuardRace)
#include "tst_SingleInstanceGuardRace.moc"
