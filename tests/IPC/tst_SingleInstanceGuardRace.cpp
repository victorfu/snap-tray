#include <QtTest>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

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

    QProcess processA;
    processA.setProgram(helperPath);
    processA.setArguments(args);
    processA.start();

    QProcess processB;
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
    primaryProcess.setProgram(helperPath);
    primaryProcess.setArguments({"--app-id", appId, "--hold-ms", "1500"});
    primaryProcess.start();
    QVERIFY(primaryProcess.waitForStarted(2000));

    QString primaryOutput;
    const QString primaryState = waitForLockState(primaryProcess, primaryOutput, 3000);
    QCOMPARE(primaryState, QString("LOCKED"));

    QProcess secondaryProcess;
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
