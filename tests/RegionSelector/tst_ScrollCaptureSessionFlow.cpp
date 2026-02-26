#include <QtTest/QtTest>

#include <QApplication>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QThread>
#include <QSet>
#include <QCursor>

#include "capture/ICaptureEngine.h"
#include "scroll/ScrollCaptureSession.h"
#include "settings/RegionCaptureSettingsManager.h"
#include "settings/Settings.h"

namespace {

QImage makeContentImage(int width, int height)
{
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < height; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const int value = (y * 9 + x * 5) % 255;
            line[x] = qRgba(value, (value * 3) % 255, (value * 7) % 255, 255);
        }
    }
    return image;
}

QImage makeFrame(const QImage &content, int offsetY, int frameHeight)
{
    const int clampedY = qBound(0, offsetY, qMax(0, content.height() - frameHeight));
    return content.copy(0, clampedY, content.width(), frameHeight).convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

class FakeCaptureEngine final : public ICaptureEngine
{
public:
    explicit FakeCaptureEngine(const QVector<QImage> &frames, QObject *parent = nullptr)
        : ICaptureEngine(parent)
        , m_frames(frames)
    {
    }

    bool setRegion(const QRect &region, QScreen *screen) override
    {
        m_captureRegion = region;
        m_targetScreen = screen;
        return !region.isEmpty() && screen != nullptr;
    }

    bool start() override
    {
        m_running = true;
        m_startCalled = true;
        return true;
    }

    void stop() override
    {
        ++m_stopCallCount;
        m_running = false;
    }

    bool isRunning() const override
    {
        return m_running;
    }

    void setExcludedWindows(const QList<WId> &windowIds) override
    {
        ++m_excludedWindowCallCount;
        m_excludedWindowIds = windowIds;
    }

    QImage captureFrame() override
    {
        ++m_captureCallCount;
        if (m_frames.isEmpty()) {
            return QImage();
        }

        const int index = qMin(m_captureIndex, m_frames.size() - 1);
        ++m_captureIndex;
        return m_frames[index];
    }

    QString engineName() const override
    {
        return QStringLiteral("FakeCaptureEngine");
    }

    bool startCalled() const { return m_startCalled; }
    int stopCallCount() const { return m_stopCallCount; }
    int excludedWindowCallCount() const { return m_excludedWindowCallCount; }
    const QList<WId> &excludedWindowIds() const { return m_excludedWindowIds; }
    int captureCallCount() const { return m_captureCallCount; }

private:
    QVector<QImage> m_frames;
    int m_captureIndex = 0;
    bool m_running = false;
    bool m_startCalled = false;
    int m_stopCallCount = 0;
    int m_excludedWindowCallCount = 0;
    int m_captureCallCount = 0;
    QList<WId> m_excludedWindowIds;
};

class FakeScrollAutomationDriver final : public IScrollAutomationDriver
{
public:
    explicit FakeScrollAutomationDriver(ScrollAutomationMode mode,
                                        const QString &reason = QString(),
                                        bool forceSyntheticResult = true,
                                        bool targetLockedForInjected = true,
                                        QObject *parent = nullptr)
        : IScrollAutomationDriver(parent)
        , m_mode(mode)
        , m_reason(reason)
        , m_forceSyntheticResult(forceSyntheticResult)
        , m_targetLockedForInjected(targetLockedForInjected)
    {
    }

    ScrollProbeResult probeAt(const QPoint &globalPoint, QScreen *screen) override
    {
        Q_UNUSED(screen);
        ++m_probeCallCount;
        m_probePoints.append(globalPoint);
        ScrollProbeResult result;
        result.mode = m_mode;
        result.anchorPoint = globalPoint;
        result.focusRecommended = (m_mode == ScrollAutomationMode::AutoSynthetic);
        if (!m_reason.isEmpty()) {
            result.reason = m_reason;
        } else {
            if (m_mode == ScrollAutomationMode::Auto) {
                result.reason = QStringLiteral("auto");
            } else if (m_mode == ScrollAutomationMode::AutoSynthetic) {
                result.reason = QStringLiteral("auto synthetic");
            } else {
                result.reason = QStringLiteral("manual");
            }
        }
        return result;
    }

    ScrollStepResult step() override
    {
        ScrollStepResult result;
        ++m_stepCallCount;
        if (m_stepScriptIndex < m_stepStatuses.size()) {
            result.status = m_stepStatuses.at(m_stepScriptIndex);
            ++m_stepScriptIndex;
        } else {
            result.status = ScrollStepStatus::Stepped;
        }
        result.estimatedDeltaY = 40;
        result.inputInjected = (m_mode == ScrollAutomationMode::AutoSynthetic);
        result.targetLocked = result.inputInjected ? m_targetLockedForInjected : true;
        return result;
    }

    bool focusTarget() override
    {
        ++m_focusCallCount;
        return true;
    }

    bool forceSyntheticFallback() override
    {
        ++m_forceSyntheticCallCount;
        if (!m_forceSyntheticResult) {
            return false;
        }

        m_mode = ScrollAutomationMode::AutoSynthetic;
        return true;
    }

    void reset() override
    {
        ++m_resetCount;
    }

    int resetCount() const { return m_resetCount; }
    int focusCallCount() const { return m_focusCallCount; }
    int forceSyntheticFallbackCount() const { return m_forceSyntheticCallCount; }
    int stepCallCount() const { return m_stepCallCount; }
    int probeCallCount() const { return m_probeCallCount; }
    const QVector<QPoint> &probePoints() const { return m_probePoints; }
    void setStepStatuses(const QVector<ScrollStepStatus> &statuses)
    {
        m_stepStatuses = statuses;
        m_stepScriptIndex = 0;
    }

private:
    ScrollAutomationMode m_mode = ScrollAutomationMode::ManualOnly;
    QString m_reason;
    bool m_forceSyntheticResult = true;
    bool m_targetLockedForInjected = true;
    int m_probeCallCount = 0;
    int m_resetCount = 0;
    int m_focusCallCount = 0;
    int m_forceSyntheticCallCount = 0;
    int m_stepCallCount = 0;
    QVector<QPoint> m_probePoints;
    QVector<ScrollStepStatus> m_stepStatuses;
    int m_stepScriptIndex = 0;
};

class SlowProbeAutomationDriver final : public IScrollAutomationDriver
{
public:
    explicit SlowProbeAutomationDriver(QObject *parent = nullptr)
        : IScrollAutomationDriver(parent)
    {
    }

    ScrollProbeResult probeAt(const QPoint &, QScreen *) override
    {
        ++m_probeCallCount;
        QThread::msleep(450);
        ScrollProbeResult result;
        result.mode = ScrollAutomationMode::ManualOnly;
        result.reason = QStringLiteral("slow-probe");
        return result;
    }

    ScrollStepResult step() override
    {
        ScrollStepResult result;
        result.status = ScrollStepStatus::Stepped;
        return result;
    }

    void reset() override
    {
    }

    int probeCallCount() const { return m_probeCallCount; }

private:
    int m_probeCallCount = 0;
};

template <typename T>
T *findTopLevelChildByObjectName(const QString &name)
{
    const auto windows = QApplication::topLevelWidgets();
    for (QWidget *window : windows) {
        if (!window) {
            continue;
        }
        T *child = window->findChild<T *>(name);
        if (child) {
            return child;
        }
    }
    return nullptr;
}

QWidget *findTopLevelByObjectName(const QString &name)
{
    const auto windows = QApplication::topLevelWidgets();
    for (QWidget *window : windows) {
        if (!window) {
            continue;
        }
        if (window->objectName() == name) {
            return window;
        }
    }
    return nullptr;
}

void configureScrollSettings()
{
    auto &settings = RegionCaptureSettingsManager::instance();
    settings.setScrollAutomationEnabled(true);
    settings.saveScrollAutoStartMode(RegionCaptureSettingsManager::ScrollAutoStartMode::ManualFirst);
    settings.saveScrollInlinePreviewCollapsed(true);
    settings.saveScrollGoodThreshold(0.75);
    settings.saveScrollPartialThreshold(0.55);
    settings.saveScrollMinScrollAmount(8);
    settings.saveScrollAutoStepDelayMs(40);
    settings.saveScrollMaxFrames(120);
    settings.saveScrollMaxOutputPixels(120000000);
    settings.saveScrollAutoIgnoreBottomEdge(true);
    settings.saveScrollSettleStableFrames(2);
    settings.saveScrollSettleTimeoutMs(220);
    settings.saveScrollProbeGridDensity(3);
}

void clearScrollSettings()
{
    auto settings = SnapTray::getSettings();
    settings.remove("regionCapture");
    settings.sync();
}

} // namespace

class tst_ScrollCaptureSessionFlow : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testAutoModeCompletesAfterNoChange();
    void testManualModeDoesNotAutoFinishOnNoChange();
    void testFirstTwoBadFramesStopSession();
    void testAutoFirstTwoBadFramesFallbackToHybrid();
    void testSyntheticAutoFallbackDropsToManual();
    void testFocusButtonVisibleInDegradedAutoMode();
    void testBuildProbePointsUsesGridSampling();
    void testAutoStateMachineWaitsForSettleBeforeCommit();
    void testAutoModeDoesNotCommitFirstTransientFrameAfterStep();
    void testAutoModeStillFinishesAfterEndReachedAndNoChange();
    void testAutoUiShowsSwitchToManualButtonAlways();
    void testAutoStopSwitchesToHybridWithoutFinishing();
    void testInterruptAutoSwitchesToManualWithoutFinishing();
    void testHybridModeNeverCallsStep();
    void testFocusButtonLabelChangesByMode();
    void testAutoProgressVisibleInAutoMode();
    void testAutoNoMotionFallsBackToHybridBeforeManual();
    void testForceSyntheticFallbackCalledOnNoMotionThreshold();
    void testProbeDelayDoesNotBlockCancel();
    void testLargeAutoDelayStillKeepsSessionResponsive();
    void testStartupNoFrameTimeoutFailsAndCleansUp();
    void testWatchdogRecoversWhenCaptureTimerStops();
    void testEscapeShortcutCancelsSession();
    void testAutoProbeFailureKeepsManualAlive();
    void testExcludedWindowsConfiguredOnStart();
    void testCancelCleansUpCaptureEngine();
    void testFinishEmitsCaptureReady();
    void testManualFirstDefaultKeepsManualUntilAutoAssist();
    void testNoFloatingPreviewWindowInCompactUi();
};

void tst_ScrollCaptureSessionFlow::init()
{
    clearScrollSettings();
    configureScrollSettings();
}

void tst_ScrollCaptureSessionFlow::cleanup()
{
    clearScrollSettings();
}

void tst_ScrollCaptureSessionFlow::testAutoModeCompletesAfterNoChange()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No primary screen available in this environment.");
    }

    const QImage content = makeContentImage(320, 2200);
    const QImage frame0 = makeFrame(content, 0, 220);
    const QImage frame1 = makeFrame(content, 44, 220);

    QVector<QImage> frames{frame0, frame1, frame1, frame1, frame1};
    FakeCaptureEngine captureEngine(frames);
    FakeScrollAutomationDriver driver(ScrollAutomationMode::Auto);

    ScrollCaptureSession::Dependencies deps;
    deps.captureEngine = &captureEngine;
    deps.automationDriver = &driver;
    deps.enableUi = false;

    QRect region(screen->geometry().topLeft() + QPoint(20, 20), QSize(320, 220));
    ScrollCaptureSession session(region, screen, nullptr, deps);

    QSignalSpy readySpy(&session, &ScrollCaptureSession::captureReady);
    QSignalSpy failedSpy(&session, &ScrollCaptureSession::failed);

    session.start();
    session.startAutoAssist();
    QTRY_COMPARE_WITH_TIMEOUT(session.mode(), ScrollCaptureSession::Mode::Auto, 1200);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.count(), 1, 3500);

    QCOMPARE(failedSpy.count(), 0);
    QVERIFY(!session.isRunning());
    QVERIFY(captureEngine.startCalled());
    QVERIFY(captureEngine.stopCallCount() >= 1);

    const QList<QVariant> args = readySpy.takeFirst();
    QVERIFY(!qvariant_cast<QPixmap>(args.at(0)).isNull());
}

void tst_ScrollCaptureSessionFlow::testManualModeDoesNotAutoFinishOnNoChange()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No primary screen available in this environment.");
    }

    const QImage content = makeContentImage(300, 2000);
    const QImage frame0 = makeFrame(content, 0, 200);
    const QImage frame1 = makeFrame(content, 40, 200);

    QVector<QImage> frames{frame0, frame1, frame1, frame1, frame1, frame1};
    FakeCaptureEngine captureEngine(frames);
    FakeScrollAutomationDriver driver(ScrollAutomationMode::ManualOnly);

    ScrollCaptureSession::Dependencies deps;
    deps.captureEngine = &captureEngine;
    deps.automationDriver = &driver;
    deps.enableUi = false;

    QRect region(screen->geometry().topLeft() + QPoint(30, 30), QSize(300, 200));
    ScrollCaptureSession session(region, screen, nullptr, deps);

    QSignalSpy readySpy(&session, &ScrollCaptureSession::captureReady);
    QSignalSpy failedSpy(&session, &ScrollCaptureSession::failed);

    session.start();
    QCOMPARE(session.mode(), ScrollCaptureSession::Mode::Manual);
    QCOMPARE(captureEngine.excludedWindowCallCount(), 1);
    QVERIFY(captureEngine.excludedWindowIds().isEmpty());

    QTest::qWait(280);
    QCOMPARE(readySpy.count(), 0);
    QVERIFY(session.isRunning());
    QCOMPARE(failedSpy.count(), 0);

    session.finish();
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.count(), 1, 1200);
    QVERIFY(!session.isRunning());
}

void tst_ScrollCaptureSessionFlow::testFirstTwoBadFramesStopSession()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No primary screen available in this environment.");
    }

    auto &settings = RegionCaptureSettingsManager::instance();
    settings.saveScrollGoodThreshold(0.999999);
    settings.saveScrollPartialThreshold(0.999998);

    const QImage content = makeContentImage(300, 1900);
    const QImage frame0 = makeFrame(content, 0, 200);

    QImage bad1(300, 200, QImage::Format_ARGB32_Premultiplied);
    bad1.fill(QColor(17, 22, 31, 255));
    QImage bad2(300, 200, QImage::Format_ARGB32_Premultiplied);
    bad2.fill(QColor(150, 44, 31, 255));

    QVector<QImage> frames{frame0, bad1, bad2, bad2};
    FakeCaptureEngine captureEngine(frames);
    FakeScrollAutomationDriver driver(ScrollAutomationMode::ManualOnly);

    ScrollCaptureSession::Dependencies deps;
    deps.captureEngine = &captureEngine;
    deps.automationDriver = &driver;
    deps.enableUi = false;

    QRect region(screen->geometry().topLeft() + QPoint(50, 50), QSize(300, 200));
    ScrollCaptureSession session(region, screen, nullptr, deps);

    QSignalSpy failedSpy(&session, &ScrollCaptureSession::failed);
    QSignalSpy readySpy(&session, &ScrollCaptureSession::captureReady);

    session.start();
    QTRY_COMPARE_WITH_TIMEOUT(failedSpy.count(), 1, 2000);

    QCOMPARE(readySpy.count(), 0);
    QVERIFY(!session.isRunning());
}

void tst_ScrollCaptureSessionFlow::testAutoFirstTwoBadFramesFallbackToHybrid()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No primary screen available in this environment.");
    }

    auto &settings = RegionCaptureSettingsManager::instance();
    settings.saveScrollGoodThreshold(0.999999);
    settings.saveScrollPartialThreshold(0.999998);

    const QImage content = makeContentImage(300, 1900);
    const QImage frame0 = makeFrame(content, 0, 200);

    QImage bad1(300, 200, QImage::Format_ARGB32_Premultiplied);
    bad1.fill(QColor(17, 22, 31, 255));
    QImage bad2(300, 200, QImage::Format_ARGB32_Premultiplied);
    bad2.fill(QColor(150, 44, 31, 255));

    QVector<QImage> frames{frame0, bad1, bad2, bad2, bad2};
    FakeCaptureEngine captureEngine(frames);
    FakeScrollAutomationDriver driver(ScrollAutomationMode::Auto);

    ScrollCaptureSession::Dependencies deps;
    deps.captureEngine = &captureEngine;
    deps.automationDriver = &driver;
    deps.enableUi = false;

    QRect region(screen->geometry().topLeft() + QPoint(50, 50), QSize(300, 200));
    ScrollCaptureSession session(region, screen, nullptr, deps);

    QSignalSpy failedSpy(&session, &ScrollCaptureSession::failed);
    QSignalSpy readySpy(&session, &ScrollCaptureSession::captureReady);

    session.start();
    session.startAutoAssist();
    QTRY_COMPARE_WITH_TIMEOUT(session.mode(), ScrollCaptureSession::Mode::Auto, 1200);
    QTRY_COMPARE_WITH_TIMEOUT(session.mode(), ScrollCaptureSession::Mode::Hybrid, 2000);

    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(readySpy.count(), 0);
    QVERIFY(session.isRunning());
    session.cancel();
}

void tst_ScrollCaptureSessionFlow::testSyntheticAutoFallbackDropsToManual()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No primary screen available in this environment.");
    }

    const QImage content = makeContentImage(320, 2200);
    const QImage frame0 = makeFrame(content, 0, 220);

    QVector<QImage> frames{frame0, frame0, frame0, frame0, frame0, frame0, frame0, frame0};
    FakeCaptureEngine captureEngine(frames);
    FakeScrollAutomationDriver driver(ScrollAutomationMode::AutoSynthetic,
                                      QStringLiteral("Using synthetic wheel fallback"));

    ScrollCaptureSession::Dependencies deps;
    deps.captureEngine = &captureEngine;
    deps.automationDriver = &driver;
    deps.enableUi = false;

    QRect region(screen->geometry().topLeft() + QPoint(20, 20), QSize(320, 220));
    ScrollCaptureSession session(region, screen, nullptr, deps);

    QSignalSpy readySpy(&session, &ScrollCaptureSession::captureReady);
    QSignalSpy failedSpy(&session, &ScrollCaptureSession::failed);

    session.start();
    session.startAutoAssist();
    QTRY_COMPARE_WITH_TIMEOUT(session.mode(), ScrollCaptureSession::Mode::Auto, 1200);

    QTRY_VERIFY_WITH_TIMEOUT(session.mode() == ScrollCaptureSession::Mode::Hybrid, 2000);
    QCOMPARE(readySpy.count(), 0);
    QCOMPARE(failedSpy.count(), 0);
    QVERIFY(session.isRunning());
    QVERIFY(driver.focusCallCount() >= 1);

    session.cancel();
}

void tst_ScrollCaptureSessionFlow::testFocusButtonVisibleInDegradedAutoMode()
{
    if (QGuiApplication::platformName().contains(QStringLiteral("offscreen"), Qt::CaseInsensitive)) {
        QSKIP("Offscreen platform is unstable for this top-level UI visibility assertion.");
    }

    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No primary screen available in this environment.");
    }

    const QImage content = makeContentImage(320, 2200);
    const QImage frame0 = makeFrame(content, 0, 220);
    const QImage frame1 = makeFrame(content, 40, 220);
    QVector<QImage> frames{frame0, frame1, frame1, frame1, frame1};

    FakeCaptureEngine captureEngine(frames);
    FakeScrollAutomationDriver driver(ScrollAutomationMode::AutoSynthetic,
                                      QStringLiteral("synthetic"),
                                      true,
                                      false);

    ScrollCaptureSession::Dependencies deps;
    deps.captureEngine = &captureEngine;
    deps.automationDriver = &driver;
    deps.enableUi = true;

    QRect region(screen->geometry().topLeft() + QPoint(24, 24), QSize(320, 220));
    ScrollCaptureSession session(region, screen, nullptr, deps);
    session.start();
    session.startAutoAssist();
    QTRY_COMPARE_WITH_TIMEOUT(session.mode(), ScrollCaptureSession::Mode::Auto, 1200);

    QPushButton *focusButton = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((focusButton = findTopLevelChildByObjectName<QPushButton>(
                                  QStringLiteral("scrollFocusTargetButton"))) != nullptr, 1500);
    QTRY_VERIFY_WITH_TIMEOUT(focusButton->isVisible(), 1200);
    QCOMPARE(focusButton->text(), QStringLiteral("Refocus Target"));

    session.cancel();
}

void tst_ScrollCaptureSessionFlow::testBuildProbePointsUsesGridSampling()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No primary screen available in this environment.");
    }

    const QImage content = makeContentImage(300, 1800);
    QVector<QImage> frames{
        makeFrame(content, 0, 200),
        makeFrame(content, 40, 200)
    };
    FakeCaptureEngine captureEngine(frames);
    FakeScrollAutomationDriver driver(ScrollAutomationMode::ManualOnly,
                                      QStringLiteral("probe-grid"));

    ScrollCaptureSession::Dependencies deps;
    deps.captureEngine = &captureEngine;
    deps.automationDriver = &driver;
    deps.enableUi = false;

    QRect region(screen->geometry().topLeft() + QPoint(20, 20), QSize(300, 200));
    QCursor::setPos(region.center());
    ScrollCaptureSession session(region, screen, nullptr, deps);
    session.start();
    session.startAutoAssist();

    QTRY_VERIFY_WITH_TIMEOUT(driver.probeCallCount() >= 9, 1200);

    const QVector<QPoint> points = driver.probePoints();
    QVERIFY(points.size() >= 9);

    QSet<QString> uniquePoints;
    for (const QPoint &point : points) {
        uniquePoints.insert(QStringLiteral("%1,%2").arg(point.x()).arg(point.y()));
        QVERIFY(region.contains(point));
        QVERIFY(point.x() > region.left());
        QVERIFY(point.x() < region.right());
        QVERIFY(point.y() > region.top());
        QVERIFY(point.y() < region.bottom());
    }
    QCOMPARE(uniquePoints.size(), points.size());

    session.cancel();
}

void tst_ScrollCaptureSessionFlow::testAutoStateMachineWaitsForSettleBeforeCommit()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No primary screen available in this environment.");
    }

    const QImage content = makeContentImage(320, 2600);
    const QImage frame0 = makeFrame(content, 0, 220);
    const QImage transient1 = makeFrame(content, 40, 220).flipped(Qt::Vertical);
    const QImage transient2 = makeFrame(content, 40, 220).rgbSwapped();
    const QImage settle = makeFrame(content, 40, 220);

    QVector<QImage> frames{
        frame0,
        transient1,
        transient2,
        settle,
        settle,
        settle,
        settle
    };
    FakeCaptureEngine captureEngine(frames);
    FakeScrollAutomationDriver driver(ScrollAutomationMode::Auto);

    ScrollCaptureSession::Dependencies deps;
    deps.captureEngine = &captureEngine;
    deps.automationDriver = &driver;
    deps.enableUi = false;

    QRect region(screen->geometry().topLeft() + QPoint(20, 20), QSize(320, 220));
    ScrollCaptureSession session(region, screen, nullptr, deps);
    QSignalSpy readySpy(&session, &ScrollCaptureSession::captureReady);
    QSignalSpy failedSpy(&session, &ScrollCaptureSession::failed);

    session.start();
    session.startAutoAssist();
    QTRY_COMPARE_WITH_TIMEOUT(session.mode(), ScrollCaptureSession::Mode::Auto, 1200);

    QTest::qWait(160);
    QCOMPARE(driver.stepCallCount(), 1);

    QTRY_VERIFY_WITH_TIMEOUT(driver.stepCallCount() >= 2, 1600);
    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(readySpy.count(), 0);
    QVERIFY(session.isRunning());

    session.cancel();
}

void tst_ScrollCaptureSessionFlow::testAutoModeDoesNotCommitFirstTransientFrameAfterStep()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No primary screen available in this environment.");
    }

    const QImage content = makeContentImage(320, 2800);
    const QImage frame0 = makeFrame(content, 0, 220);
    const QImage settle = makeFrame(content, 44, 220);

    QImage bad1(320, 220, QImage::Format_ARGB32_Premultiplied);
    QImage bad2(320, 220, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < bad1.height(); ++y) {
        QRgb *line1 = reinterpret_cast<QRgb *>(bad1.scanLine(y));
        QRgb *line2 = reinterpret_cast<QRgb *>(bad2.scanLine(y));
        for (int x = 0; x < bad1.width(); ++x) {
            const int v1 = (x * 31 + y * 17) % 255;
            const int v2 = (x * 13 + y * 47 + 79) % 255;
            line1[x] = qRgba(v1, (v1 * 5) % 255, (v1 * 11) % 255, 255);
            line2[x] = qRgba(v2, (v2 * 7) % 255, (v2 * 3) % 255, 255);
        }
    }

    QVector<QImage> frames{
        frame0,
        bad1,
        bad2,
        settle,
        settle,
        settle,
        settle,
        settle
    };
    FakeCaptureEngine captureEngine(frames);
    FakeScrollAutomationDriver driver(ScrollAutomationMode::Auto);

    ScrollCaptureSession::Dependencies deps;
    deps.captureEngine = &captureEngine;
    deps.automationDriver = &driver;
    deps.enableUi = false;

    QRect region(screen->geometry().topLeft() + QPoint(22, 22), QSize(320, 220));
    ScrollCaptureSession session(region, screen, nullptr, deps);
    QSignalSpy readySpy(&session, &ScrollCaptureSession::captureReady);
    QSignalSpy failedSpy(&session, &ScrollCaptureSession::failed);

    session.start();
    session.startAutoAssist();
    QTRY_COMPARE_WITH_TIMEOUT(session.mode(), ScrollCaptureSession::Mode::Auto, 1200);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.count(), 1, 4500);

    QCOMPARE(failedSpy.count(), 0);
    QVERIFY(!session.isRunning());
}

void tst_ScrollCaptureSessionFlow::testAutoModeStillFinishesAfterEndReachedAndNoChange()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No primary screen available in this environment.");
    }

    const QImage content = makeContentImage(320, 2600);
    const QImage frame0 = makeFrame(content, 0, 220);
    const QImage frame1 = makeFrame(content, 40, 220);

    QVector<QImage> frames{
        frame0,
        frame1,
        frame1,
        frame1,
        frame1,
        frame1,
        frame1,
        frame1
    };
    FakeCaptureEngine captureEngine(frames);
    FakeScrollAutomationDriver driver(ScrollAutomationMode::Auto);
    driver.setStepStatuses({
        ScrollStepStatus::Stepped,
        ScrollStepStatus::EndReached,
        ScrollStepStatus::EndReached,
        ScrollStepStatus::EndReached
    });

    ScrollCaptureSession::Dependencies deps;
    deps.captureEngine = &captureEngine;
    deps.automationDriver = &driver;
    deps.enableUi = false;

    QRect region(screen->geometry().topLeft() + QPoint(18, 18), QSize(320, 220));
    ScrollCaptureSession session(region, screen, nullptr, deps);
    QSignalSpy readySpy(&session, &ScrollCaptureSession::captureReady);
    QSignalSpy failedSpy(&session, &ScrollCaptureSession::failed);

    session.start();
    session.startAutoAssist();
    QTRY_COMPARE_WITH_TIMEOUT(session.mode(), ScrollCaptureSession::Mode::Auto, 1200);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.count(), 1, 4500);

    QCOMPARE(failedSpy.count(), 0);
    QVERIFY(!session.isRunning());
}

void tst_ScrollCaptureSessionFlow::testAutoUiShowsSwitchToManualButtonAlways()
{
    if (QGuiApplication::platformName().contains(QStringLiteral("offscreen"), Qt::CaseInsensitive)) {
        QSKIP("Offscreen platform is unstable for top-level UI visibility assertions.");
    }

    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No primary screen available in this environment.");
    }

    const QImage content = makeContentImage(320, 2400);
    const QImage frame0 = makeFrame(content, 0, 220);
    const QImage frame1 = makeFrame(content, 40, 220);
    QVector<QImage> frames{frame0, frame1, frame1, frame1, frame1};

    FakeCaptureEngine captureEngine(frames);
    FakeScrollAutomationDriver driver(ScrollAutomationMode::Auto);

    ScrollCaptureSession::Dependencies deps;
    deps.captureEngine = &captureEngine;
    deps.automationDriver = &driver;
    deps.enableUi = true;

    QRect region(screen->geometry().topLeft() + QPoint(24, 24), QSize(320, 220));
    ScrollCaptureSession session(region, screen, nullptr, deps);
    session.start();
    session.startAutoAssist();
    QTRY_COMPARE_WITH_TIMEOUT(session.mode(), ScrollCaptureSession::Mode::Auto, 1200);

    QPushButton *interruptButton = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((interruptButton = findTopLevelChildByObjectName<QPushButton>(
                                  QStringLiteral("scrollInterruptAutoButton"))) != nullptr, 1500);
    QVERIFY(interruptButton->isVisible());
    QVERIFY(interruptButton->isEnabled());
    QCOMPARE(interruptButton->text(), QStringLiteral("Stop Auto"));

    session.cancel();
}

void tst_ScrollCaptureSessionFlow::testAutoStopSwitchesToHybridWithoutFinishing()
{
    if (QGuiApplication::platformName().contains(QStringLiteral("offscreen"), Qt::CaseInsensitive)) {
        QSKIP("Offscreen platform is unstable for top-level UI visibility assertions.");
    }

    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No primary screen available in this environment.");
    }

    const QImage content = makeContentImage(320, 2600);
    const QImage frame0 = makeFrame(content, 0, 220);
    const QImage frame1 = makeFrame(content, 40, 220);
    const QImage frame2 = makeFrame(content, 80, 220);
    QVector<QImage> frames{frame0, frame1, frame2, frame2, frame2};

    FakeCaptureEngine captureEngine(frames);
    FakeScrollAutomationDriver driver(ScrollAutomationMode::Auto);

    ScrollCaptureSession::Dependencies deps;
    deps.captureEngine = &captureEngine;
    deps.automationDriver = &driver;
    deps.enableUi = true;

    QRect region(screen->geometry().topLeft() + QPoint(24, 24), QSize(320, 220));
    ScrollCaptureSession session(region, screen, nullptr, deps);

    QSignalSpy readySpy(&session, &ScrollCaptureSession::captureReady);
    QSignalSpy cancelledSpy(&session, &ScrollCaptureSession::cancelled);
    QSignalSpy failedSpy(&session, &ScrollCaptureSession::failed);

    session.start();
    session.startAutoAssist();
    QTRY_COMPARE_WITH_TIMEOUT(session.mode(), ScrollCaptureSession::Mode::Auto, 1200);

    QPushButton *interruptButton = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((interruptButton = findTopLevelChildByObjectName<QPushButton>(
                                  QStringLiteral("scrollInterruptAutoButton"))) != nullptr, 1500);
    QTest::mouseClick(interruptButton, Qt::LeftButton);

    QTRY_COMPARE_WITH_TIMEOUT(session.mode(), ScrollCaptureSession::Mode::Hybrid, 1200);
    QVERIFY(session.isRunning());
    QCOMPARE(readySpy.count(), 0);
    QCOMPARE(cancelledSpy.count(), 0);
    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(interruptButton->text(), QStringLiteral("Retry Auto"));

    session.cancel();
}

void tst_ScrollCaptureSessionFlow::testInterruptAutoSwitchesToManualWithoutFinishing()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No primary screen available in this environment.");
    }

    const QImage content = makeContentImage(320, 2400);
    const QImage frame0 = makeFrame(content, 0, 220);
    const QImage frame1 = makeFrame(content, 40, 220);
    const QImage frame2 = makeFrame(content, 80, 220);

    QVector<QImage> frames{frame0, frame1, frame2, frame2, frame2};
    FakeCaptureEngine captureEngine(frames);
    FakeScrollAutomationDriver driver(ScrollAutomationMode::Auto);

    ScrollCaptureSession::Dependencies deps;
    deps.captureEngine = &captureEngine;
    deps.automationDriver = &driver;
    deps.enableUi = false;

    QRect region(screen->geometry().topLeft() + QPoint(20, 20), QSize(320, 220));
    ScrollCaptureSession session(region, screen, nullptr, deps);

    QSignalSpy readySpy(&session, &ScrollCaptureSession::captureReady);
    QSignalSpy failedSpy(&session, &ScrollCaptureSession::failed);

    session.start();
    session.startAutoAssist();
    QTRY_COMPARE_WITH_TIMEOUT(session.mode(), ScrollCaptureSession::Mode::Auto, 1200);
    QVERIFY(session.isRunning());

    session.interruptAuto();
    QCOMPARE(session.mode(), ScrollCaptureSession::Mode::Hybrid);
    QVERIFY(session.isRunning());

    session.interruptAuto();
    QCOMPARE(session.mode(), ScrollCaptureSession::Mode::Manual);
    QVERIFY(session.isRunning());

    QTest::qWait(200);
    QCOMPARE(readySpy.count(), 0);
    QCOMPARE(failedSpy.count(), 0);
    QVERIFY(session.isRunning());

    session.cancel();
}

void tst_ScrollCaptureSessionFlow::testHybridModeNeverCallsStep()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No primary screen available in this environment.");
    }

    const QImage content = makeContentImage(320, 2600);
    const QImage frame0 = makeFrame(content, 0, 220);
    const QImage frame1 = makeFrame(content, 40, 220);
    const QImage frame2 = makeFrame(content, 80, 220);
    QVector<QImage> frames{frame0, frame1, frame2, frame2, frame2, frame2};

    FakeCaptureEngine captureEngine(frames);
    FakeScrollAutomationDriver driver(ScrollAutomationMode::Auto);

    ScrollCaptureSession::Dependencies deps;
    deps.captureEngine = &captureEngine;
    deps.automationDriver = &driver;
    deps.enableUi = false;

    QRect region(screen->geometry().topLeft() + QPoint(20, 20), QSize(320, 220));
    ScrollCaptureSession session(region, screen, nullptr, deps);

    session.start();
    session.startAutoAssist();
    QTRY_COMPARE_WITH_TIMEOUT(session.mode(), ScrollCaptureSession::Mode::Auto, 1200);
    QTRY_VERIFY_WITH_TIMEOUT(driver.stepCallCount() >= 1, 1200);

    session.interruptAuto();
    const int stepCallsAfterInterrupt = driver.stepCallCount();
    QTest::qWait(220);

    QCOMPARE(session.mode(), ScrollCaptureSession::Mode::Hybrid);
    QCOMPARE(driver.stepCallCount(), stepCallsAfterInterrupt);
    QVERIFY(session.isRunning());

    session.cancel();
}

void tst_ScrollCaptureSessionFlow::testFocusButtonLabelChangesByMode()
{
    if (QGuiApplication::platformName().contains(QStringLiteral("offscreen"), Qt::CaseInsensitive)) {
        QSKIP("Offscreen platform is unstable for top-level UI visibility assertions.");
    }

    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No primary screen available in this environment.");
    }

    const QImage content = makeContentImage(320, 2400);
    const QImage frame0 = makeFrame(content, 0, 220);
    const QImage frame1 = makeFrame(content, 40, 220);
    QVector<QImage> frames{frame0, frame1, frame1, frame1, frame1};

    FakeCaptureEngine captureEngine(frames);
    FakeScrollAutomationDriver driver(ScrollAutomationMode::AutoSynthetic,
                                      QStringLiteral("synthetic"),
                                      true,
                                      false);

    ScrollCaptureSession::Dependencies deps;
    deps.captureEngine = &captureEngine;
    deps.automationDriver = &driver;
    deps.enableUi = true;

    QRect region(screen->geometry().topLeft() + QPoint(24, 24), QSize(320, 220));
    ScrollCaptureSession session(region, screen, nullptr, deps);
    session.start();
    session.startAutoAssist();
    QTRY_COMPARE_WITH_TIMEOUT(session.mode(), ScrollCaptureSession::Mode::Auto, 1200);

    QPushButton *focusButton = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((focusButton = findTopLevelChildByObjectName<QPushButton>(
                                  QStringLiteral("scrollFocusTargetButton"))) != nullptr, 1500);
    QTRY_VERIFY_WITH_TIMEOUT(focusButton->isVisible(), 1200);
    QCOMPARE(focusButton->text(), QStringLiteral("Refocus Target"));

    session.interruptAuto();
    QTRY_COMPARE_WITH_TIMEOUT(session.mode(), ScrollCaptureSession::Mode::Hybrid, 1200);
    QTRY_VERIFY_WITH_TIMEOUT(focusButton->isVisible(), 1200);
    QCOMPARE(focusButton->text(), QStringLiteral("Refocus Target"));

    session.interruptAuto();
    QTRY_COMPARE_WITH_TIMEOUT(session.mode(), ScrollCaptureSession::Mode::Manual, 1200);
    QTRY_VERIFY_WITH_TIMEOUT(focusButton->isVisible(), 1200);
    QCOMPARE(focusButton->text(), QStringLiteral("Focus Target"));

    session.cancel();
}

void tst_ScrollCaptureSessionFlow::testAutoProgressVisibleInAutoMode()
{
    if (QGuiApplication::platformName().contains(QStringLiteral("offscreen"), Qt::CaseInsensitive)) {
        QSKIP("Offscreen platform is unstable for top-level UI visibility assertions.");
    }

    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No primary screen available in this environment.");
    }

    const QImage content = makeContentImage(320, 2400);
    const QImage frame0 = makeFrame(content, 0, 220);
    const QImage frame1 = makeFrame(content, 40, 220);
    QVector<QImage> frames{frame0, frame1, frame1, frame1, frame1};

    FakeCaptureEngine captureEngine(frames);
    FakeScrollAutomationDriver driver(ScrollAutomationMode::AutoSynthetic,
                                      QStringLiteral("synthetic"),
                                      true,
                                      false);

    ScrollCaptureSession::Dependencies deps;
    deps.captureEngine = &captureEngine;
    deps.automationDriver = &driver;
    deps.enableUi = true;

    QRect region(screen->geometry().topLeft() + QPoint(24, 24), QSize(320, 220));
    ScrollCaptureSession session(region, screen, nullptr, deps);
    session.start();
    session.startAutoAssist();
    QTRY_COMPARE_WITH_TIMEOUT(session.mode(), ScrollCaptureSession::Mode::Auto, 1200);

    QLabel *progressLabel = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((progressLabel = findTopLevelChildByObjectName<QLabel>(
                                  QStringLiteral("scrollProgressLabel"))) != nullptr, 1500);
    QTRY_VERIFY_WITH_TIMEOUT(!progressLabel->text().isEmpty(), 1200);
    QVERIFY(!progressLabel->isVisible());
    QVERIFY(progressLabel->text().contains(QStringLiteral("driver"), Qt::CaseInsensitive));

    session.cancel();
}

void tst_ScrollCaptureSessionFlow::testAutoNoMotionFallsBackToHybridBeforeManual()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No primary screen available in this environment.");
    }

    const QImage content = makeContentImage(320, 2200);
    const QImage frame0 = makeFrame(content, 0, 220);
    QVector<QImage> frames{frame0, frame0, frame0, frame0, frame0, frame0, frame0, frame0, frame0, frame0};

    FakeCaptureEngine captureEngine(frames);
    FakeScrollAutomationDriver driver(ScrollAutomationMode::Auto, QString(), true);

    ScrollCaptureSession::Dependencies deps;
    deps.captureEngine = &captureEngine;
    deps.automationDriver = &driver;
    deps.enableUi = false;

    QRect region(screen->geometry().topLeft() + QPoint(20, 20), QSize(320, 220));
    ScrollCaptureSession session(region, screen, nullptr, deps);

    QSignalSpy readySpy(&session, &ScrollCaptureSession::captureReady);
    QSignalSpy failedSpy(&session, &ScrollCaptureSession::failed);

    session.start();
    session.startAutoAssist();
    QTRY_COMPARE_WITH_TIMEOUT(session.mode(), ScrollCaptureSession::Mode::Auto, 1200);

    QTRY_VERIFY_WITH_TIMEOUT(session.mode() == ScrollCaptureSession::Mode::Hybrid, 2500);
    QVERIFY(driver.focusCallCount() >= 1);
    QVERIFY(driver.forceSyntheticFallbackCount() >= 1);
    QCOMPARE(readySpy.count(), 0);
    QCOMPARE(failedSpy.count(), 0);
    QVERIFY(session.isRunning());

    session.cancel();
}

void tst_ScrollCaptureSessionFlow::testForceSyntheticFallbackCalledOnNoMotionThreshold()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No primary screen available in this environment.");
    }

    const QImage content = makeContentImage(320, 2200);
    const QImage frame0 = makeFrame(content, 0, 220);
    QVector<QImage> frames{frame0, frame0, frame0, frame0, frame0, frame0, frame0, frame0, frame0, frame0};

    FakeCaptureEngine captureEngine(frames);
    FakeScrollAutomationDriver driver(ScrollAutomationMode::Auto, QString(), false);

    ScrollCaptureSession::Dependencies deps;
    deps.captureEngine = &captureEngine;
    deps.automationDriver = &driver;
    deps.enableUi = false;

    QRect region(screen->geometry().topLeft() + QPoint(20, 20), QSize(320, 220));
    ScrollCaptureSession session(region, screen, nullptr, deps);

    session.start();
    session.startAutoAssist();
    QTRY_COMPARE_WITH_TIMEOUT(session.mode(), ScrollCaptureSession::Mode::Auto, 1200);

    QTRY_VERIFY_WITH_TIMEOUT(session.mode() == ScrollCaptureSession::Mode::Hybrid, 2500);
    QVERIFY(driver.forceSyntheticFallbackCount() >= 1);
    QVERIFY(session.isRunning());

    session.cancel();
}

void tst_ScrollCaptureSessionFlow::testProbeDelayDoesNotBlockCancel()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No primary screen available in this environment.");
    }

    const QImage content = makeContentImage(300, 1800);
    QVector<QImage> frames{makeFrame(content, 0, 200), makeFrame(content, 40, 200)};
    FakeCaptureEngine captureEngine(frames);
    SlowProbeAutomationDriver driver;

    ScrollCaptureSession::Dependencies deps;
    deps.captureEngine = &captureEngine;
    deps.automationDriver = &driver;
    deps.enableUi = false;

    QRect region(screen->geometry().topLeft() + QPoint(20, 20), QSize(300, 200));
    ScrollCaptureSession session(region, screen, nullptr, deps);
    QSignalSpy cancelledSpy(&session, &ScrollCaptureSession::cancelled);

    session.start();
    session.cancel();

    QTRY_COMPARE_WITH_TIMEOUT(cancelledSpy.count(), 1, 1200);
    QVERIFY(!session.isRunning());
    QCOMPARE(driver.probeCallCount(), 0);
}

void tst_ScrollCaptureSessionFlow::testLargeAutoDelayStillKeepsSessionResponsive()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No primary screen available in this environment.");
    }

    RegionCaptureSettingsManager::instance().saveScrollAutoStepDelayMs(1800);

    const QImage content = makeContentImage(320, 2600);
    QVector<QImage> frames{
        makeFrame(content, 0, 220),
        makeFrame(content, 40, 220),
        makeFrame(content, 80, 220),
        makeFrame(content, 120, 220),
        makeFrame(content, 160, 220)
    };
    FakeCaptureEngine captureEngine(frames);
    FakeScrollAutomationDriver driver(ScrollAutomationMode::Auto);

    ScrollCaptureSession::Dependencies deps;
    deps.captureEngine = &captureEngine;
    deps.automationDriver = &driver;
    deps.enableUi = false;

    QRect region(screen->geometry().topLeft() + QPoint(30, 30), QSize(320, 220));
    ScrollCaptureSession session(region, screen, nullptr, deps);
    session.start();
    session.startAutoAssist();
    QTRY_VERIFY_WITH_TIMEOUT(session.mode() == ScrollCaptureSession::Mode::Auto, 1200);
    QTest::qWait(320);

    QVERIFY(session.isRunning());
    QVERIFY(captureEngine.captureCallCount() >= 3);
    QVERIFY(driver.stepCallCount() <= 2);

    session.cancel();
}

void tst_ScrollCaptureSessionFlow::testStartupNoFrameTimeoutFailsAndCleansUp()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No primary screen available in this environment.");
    }

    QVector<QImage> frames;
    FakeCaptureEngine captureEngine(frames);
    FakeScrollAutomationDriver driver(ScrollAutomationMode::ManualOnly);

    ScrollCaptureSession::Dependencies deps;
    deps.captureEngine = &captureEngine;
    deps.automationDriver = &driver;
    deps.enableUi = false;

    QRect region(screen->geometry().topLeft() + QPoint(20, 20), QSize(300, 200));
    ScrollCaptureSession session(region, screen, nullptr, deps);

    QSignalSpy failedSpy(&session, &ScrollCaptureSession::failed);
    session.start();
    QTRY_COMPARE_WITH_TIMEOUT(failedSpy.count(), 1, 8000);

    QVERIFY(!session.isRunning());
    QVERIFY(captureEngine.stopCallCount() >= 1);
}

void tst_ScrollCaptureSessionFlow::testWatchdogRecoversWhenCaptureTimerStops()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No primary screen available in this environment.");
    }

    const QImage content = makeContentImage(320, 2200);
    QVector<QImage> frames{
        makeFrame(content, 0, 220),
        makeFrame(content, 40, 220),
        makeFrame(content, 80, 220),
        makeFrame(content, 120, 220)
    };
    FakeCaptureEngine captureEngine(frames);
    FakeScrollAutomationDriver driver(ScrollAutomationMode::ManualOnly);

    ScrollCaptureSession::Dependencies deps;
    deps.captureEngine = &captureEngine;
    deps.automationDriver = &driver;
    deps.enableUi = false;

    QRect region(screen->geometry().topLeft() + QPoint(24, 24), QSize(320, 220));
    ScrollCaptureSession session(region, screen, nullptr, deps);
    QSignalSpy failedSpy(&session, &ScrollCaptureSession::failed);

    session.start();
    QTRY_VERIFY_WITH_TIMEOUT(session.isRunning(), 1000);

    QTimer *captureTimer = session.findChild<QTimer *>(QStringLiteral("scrollCaptureTickTimer"));
    QVERIFY(captureTimer != nullptr);
    captureTimer->stop();

    QTRY_VERIFY_WITH_TIMEOUT(captureTimer->isActive(), 2200);
    QCOMPARE(failedSpy.count(), 0);
    QVERIFY(session.isRunning());

    session.cancel();
}

void tst_ScrollCaptureSessionFlow::testEscapeShortcutCancelsSession()
{
    if (QGuiApplication::platformName().contains(QStringLiteral("offscreen"), Qt::CaseInsensitive)) {
        QSKIP("Offscreen platform is unstable for top-level UI visibility assertions.");
    }

    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No primary screen available in this environment.");
    }

    const QImage content = makeContentImage(320, 2200);
    QVector<QImage> frames{
        makeFrame(content, 0, 220),
        makeFrame(content, 40, 220),
        makeFrame(content, 80, 220)
    };
    FakeCaptureEngine captureEngine(frames);
    FakeScrollAutomationDriver driver(ScrollAutomationMode::ManualOnly);

    ScrollCaptureSession::Dependencies deps;
    deps.captureEngine = &captureEngine;
    deps.automationDriver = &driver;
    deps.enableUi = true;

    QRect region(screen->geometry().topLeft() + QPoint(30, 30), QSize(320, 220));
    ScrollCaptureSession session(region, screen, nullptr, deps);
    QSignalSpy cancelledSpy(&session, &ScrollCaptureSession::cancelled);

    session.start();
    QWidget *controlBar = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((controlBar = findTopLevelByObjectName(
                                  QStringLiteral("scrollCaptureControlBar"))) != nullptr, 1500);
    QVERIFY(controlBar->isVisible());

    QTest::keyClick(controlBar, Qt::Key_Escape);
    QTRY_COMPARE_WITH_TIMEOUT(cancelledSpy.count(), 1, 1500);
    QVERIFY(!session.isRunning());
}

void tst_ScrollCaptureSessionFlow::testAutoProbeFailureKeepsManualAlive()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No primary screen available in this environment.");
    }

    const QImage content = makeContentImage(320, 2200);
    QVector<QImage> frames{
        makeFrame(content, 0, 220),
        makeFrame(content, 0, 220),
        makeFrame(content, 0, 220)
    };
    FakeCaptureEngine captureEngine(frames);
    FakeScrollAutomationDriver driver(ScrollAutomationMode::ManualOnly,
                                      QStringLiteral("Auto probe timeout"));

    ScrollCaptureSession::Dependencies deps;
    deps.captureEngine = &captureEngine;
    deps.automationDriver = &driver;
    deps.enableUi = false;

    QRect region(screen->geometry().topLeft() + QPoint(20, 20), QSize(320, 220));
    ScrollCaptureSession session(region, screen, nullptr, deps);
    QSignalSpy failedSpy(&session, &ScrollCaptureSession::failed);

    session.start();
    QTest::qWait(220);

    QCOMPARE(session.mode(), ScrollCaptureSession::Mode::Manual);
    QVERIFY(session.isRunning());
    QCOMPARE(failedSpy.count(), 0);

    session.cancel();
}

void tst_ScrollCaptureSessionFlow::testExcludedWindowsConfiguredOnStart()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No primary screen available in this environment.");
    }

    const QImage content = makeContentImage(260, 1700);
    QVector<QImage> frames{
        makeFrame(content, 0, 180),
        makeFrame(content, 36, 180)
    };

    FakeCaptureEngine captureEngine(frames);
    FakeScrollAutomationDriver driver(ScrollAutomationMode::ManualOnly);

    ScrollCaptureSession::Dependencies deps;
    deps.captureEngine = &captureEngine;
    deps.automationDriver = &driver;
    deps.enableUi = false;

    QRect region(screen->geometry().topLeft() + QPoint(20, 20), QSize(260, 180));
    ScrollCaptureSession session(region, screen, nullptr, deps);
    session.start();

    QCOMPARE(captureEngine.excludedWindowCallCount(), 1);
    QVERIFY(captureEngine.excludedWindowIds().isEmpty());
    session.cancel();
}

void tst_ScrollCaptureSessionFlow::testCancelCleansUpCaptureEngine()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No primary screen available in this environment.");
    }

    const QImage content = makeContentImage(280, 1800);
    QVector<QImage> frames{
        makeFrame(content, 0, 200),
        makeFrame(content, 32, 200),
        makeFrame(content, 64, 200)
    };

    FakeCaptureEngine captureEngine(frames);
    FakeScrollAutomationDriver driver(ScrollAutomationMode::ManualOnly);

    ScrollCaptureSession::Dependencies deps;
    deps.captureEngine = &captureEngine;
    deps.automationDriver = &driver;
    deps.enableUi = false;

    QRect region(screen->geometry().topLeft() + QPoint(40, 40), QSize(280, 200));
    ScrollCaptureSession session(region, screen, nullptr, deps);

    QSignalSpy cancelledSpy(&session, &ScrollCaptureSession::cancelled);
    session.start();
    QVERIFY(session.isRunning());

    session.cancel();
    QTRY_COMPARE_WITH_TIMEOUT(cancelledSpy.count(), 1, 1000);

    QVERIFY(!session.isRunning());
    QVERIFY(captureEngine.stopCallCount() >= 1);
    QVERIFY(driver.resetCount() >= 1);
}

void tst_ScrollCaptureSessionFlow::testFinishEmitsCaptureReady()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No primary screen available in this environment.");
    }

    const QImage content = makeContentImage(260, 1700);
    QVector<QImage> frames{
        makeFrame(content, 0, 180),
        makeFrame(content, 36, 180)
    };

    FakeCaptureEngine captureEngine(frames);
    FakeScrollAutomationDriver driver(ScrollAutomationMode::ManualOnly);

    ScrollCaptureSession::Dependencies deps;
    deps.captureEngine = &captureEngine;
    deps.automationDriver = &driver;
    deps.enableUi = false;

    QRect region(screen->geometry().topLeft() + QPoint(12, 12), QSize(260, 180));
    ScrollCaptureSession session(region, screen, nullptr, deps);

    QSignalSpy readySpy(&session, &ScrollCaptureSession::captureReady);
    QSignalSpy failedSpy(&session, &ScrollCaptureSession::failed);

    session.start();
    session.finish();

    QTRY_COMPARE_WITH_TIMEOUT(readySpy.count(), 1, 1200);
    QCOMPARE(failedSpy.count(), 0);

    const QList<QVariant> args = readySpy.takeFirst();
    QCOMPARE(qvariant_cast<QPoint>(args.at(1)), region.topLeft());
    QCOMPARE(qvariant_cast<QRect>(args.at(2)), region);
}

void tst_ScrollCaptureSessionFlow::testManualFirstDefaultKeepsManualUntilAutoAssist()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No primary screen available in this environment.");
    }

    const QImage content = makeContentImage(320, 2200);
    QVector<QImage> frames{
        makeFrame(content, 0, 220),
        makeFrame(content, 40, 220),
        makeFrame(content, 80, 220)
    };
    FakeCaptureEngine captureEngine(frames);
    FakeScrollAutomationDriver driver(ScrollAutomationMode::Auto);

    ScrollCaptureSession::Dependencies deps;
    deps.captureEngine = &captureEngine;
    deps.automationDriver = &driver;
    deps.enableUi = false;

    QRect region(screen->geometry().topLeft() + QPoint(16, 16), QSize(320, 220));
    ScrollCaptureSession session(region, screen, nullptr, deps);

    session.start();
    QCOMPARE(session.mode(), ScrollCaptureSession::Mode::Manual);
    QCOMPARE(driver.stepCallCount(), 0);

    session.startAutoAssist();
    QTRY_COMPARE_WITH_TIMEOUT(session.mode(), ScrollCaptureSession::Mode::Auto, 1200);
    session.cancel();
}

void tst_ScrollCaptureSessionFlow::testNoFloatingPreviewWindowInCompactUi()
{
    if (QGuiApplication::platformName().contains(QStringLiteral("offscreen"), Qt::CaseInsensitive)) {
        QSKIP("Offscreen platform is unstable for top-level UI visibility assertions.");
    }

    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        QSKIP("No primary screen available in this environment.");
    }

    const QImage content = makeContentImage(320, 2200);
    QVector<QImage> frames{
        makeFrame(content, 0, 220),
        makeFrame(content, 40, 220),
        makeFrame(content, 80, 220)
    };
    FakeCaptureEngine captureEngine(frames);
    FakeScrollAutomationDriver driver(ScrollAutomationMode::ManualOnly);

    ScrollCaptureSession::Dependencies deps;
    deps.captureEngine = &captureEngine;
    deps.automationDriver = &driver;
    deps.enableUi = true;

    QRect region(screen->geometry().topLeft() + QPoint(28, 28), QSize(320, 220));
    ScrollCaptureSession session(region, screen, nullptr, deps);
    session.start();

    QWidget *controlBar = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((controlBar = findTopLevelByObjectName(
                                  QStringLiteral("scrollCaptureControlBar"))) != nullptr, 1500);
    QVERIFY(controlBar->isVisible());
    QVERIFY(findTopLevelByObjectName(QStringLiteral("scrollCapturePreviewWindow")) == nullptr);

    session.cancel();
}

QTEST_MAIN(tst_ScrollCaptureSessionFlow)
#include "tst_ScrollCaptureSessionFlow.moc"
