#include <QtTest/QtTest>

#ifdef Q_OS_WIN

#include "capture/DXGICaptureEngine.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QMutex>
#include <QScreen>

namespace {

QMutex g_warningMutex;
QStringList g_warningMessages;
QtMessageHandler g_previousHandler = nullptr;

bool isThreadLifecycleWarning(const QString &message)
{
    static const QStringList patterns = {
        QStringLiteral("QObject::moveToThread"),
        QStringLiteral("deleteLater"),
        QStringLiteral("QThread: Destroyed while thread is still running"),
        QStringLiteral("QObject::~QObject: Timers cannot be stopped from another thread"),
        QStringLiteral("QObject::killTimer: Timers cannot be stopped from another thread")
    };

    for (const QString &pattern : patterns) {
        if (message.contains(pattern, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

void warningCaptureHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        QMutexLocker locker(&g_warningMutex);
        g_warningMessages.push_back(message);
    }

    if (g_previousHandler) {
        g_previousHandler(type, context, message);
    }
}

class WarningCaptureScope
{
public:
    WarningCaptureScope()
    {
        QMutexLocker locker(&g_warningMutex);
        g_warningMessages.clear();
        g_previousHandler = qInstallMessageHandler(warningCaptureHandler);
    }

    ~WarningCaptureScope()
    {
        qInstallMessageHandler(g_previousHandler);
        g_previousHandler = nullptr;
    }

    QStringList threadLifecycleWarnings() const
    {
        QMutexLocker locker(&g_warningMutex);
        QStringList filtered;
        for (const QString &message : g_warningMessages) {
            if (isThreadLifecycleWarning(message)) {
                filtered.push_back(message);
            }
        }
        return filtered;
    }
};

} // namespace

class TestDXGICaptureEngineThreadLifecycleWin : public QObject
{
    Q_OBJECT

private slots:
    void testStartStopFromMainThread_NoThreadAffinityWarnings();
    void testRepeatedStartStop_NoThreadAffinityWarningsOrHang();

private:
    bool configureEngine(DXGICaptureEngine &engine) const;
};

bool TestDXGICaptureEngineThreadLifecycleWin::configureEngine(DXGICaptureEngine &engine) const
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        return false;
    }

    const QRect screenGeom = screen->geometry();
    if (screenGeom.isEmpty()) {
        return false;
    }

    const int width = qMax(32, qMin(160, screenGeom.width()));
    const int height = qMax(32, qMin(120, screenGeom.height()));
    const QRect region(screenGeom.topLeft(), QSize(width, height));

    engine.setFrameRate(10);
    return engine.setRegion(region, screen);
}

void TestDXGICaptureEngineThreadLifecycleWin::testStartStopFromMainThread_NoThreadAffinityWarnings()
{
    DXGICaptureEngine engine;
    if (!configureEngine(engine)) {
        QSKIP("No valid screen available for DXGI lifecycle test.");
    }

    WarningCaptureScope warningScope;

    QVERIFY2(engine.start(), "DXGICaptureEngine::start() failed.");
    QTest::qWait(80);
    engine.stop();

    const QStringList warnings = warningScope.threadLifecycleWarnings();
    QVERIFY2(warnings.isEmpty(), qPrintable(QStringLiteral(
        "Unexpected thread lifecycle warnings:\n%1").arg(warnings.join(QLatin1Char('\n')))));
}

void TestDXGICaptureEngineThreadLifecycleWin::testRepeatedStartStop_NoThreadAffinityWarningsOrHang()
{
    DXGICaptureEngine engine;
    if (!configureEngine(engine)) {
        QSKIP("No valid screen available for DXGI lifecycle test.");
    }

    WarningCaptureScope warningScope;

    constexpr int kIterations = 40;
    constexpr qint64 kMaxElapsedMs = 30000;
    QElapsedTimer elapsedTimer;
    elapsedTimer.start();

    for (int i = 0; i < kIterations; ++i) {
        QVERIFY2(engine.start(), qPrintable(QStringLiteral(
            "DXGICaptureEngine::start() failed at iteration %1").arg(i + 1)));
        QTest::qWait(20);
        engine.stop();
    }

    const QStringList warnings = warningScope.threadLifecycleWarnings();
    QVERIFY2(warnings.isEmpty(), qPrintable(QStringLiteral(
        "Unexpected thread lifecycle warnings:\n%1").arg(warnings.join(QLatin1Char('\n')))));
    QVERIFY2(elapsedTimer.elapsed() < kMaxElapsedMs, qPrintable(QStringLiteral(
        "Repeated start/stop took too long (%1 ms). Potential hang detected.")
        .arg(elapsedTimer.elapsed())));
}

QTEST_MAIN(TestDXGICaptureEngineThreadLifecycleWin)
#include "tst_DXGICaptureEngineThreadLifecycle_win.moc"

#else

class TestDXGICaptureEngineThreadLifecycleWin : public QObject
{
    Q_OBJECT
};

QTEST_MAIN(TestDXGICaptureEngineThreadLifecycleWin)
#include "tst_DXGICaptureEngineThreadLifecycle_win.moc"

#endif // Q_OS_WIN
