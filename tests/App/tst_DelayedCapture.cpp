#include <QtTest/QtTest>

#include <QApplication>
#include <QDir>
#include <QPixmap>
#include <QPointer>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>
#include <QSignalSpy>
#include <QtMath>

#include "CaptureManager.h"
#include "RegionSelector.h"
#include "capture/OwnWindowCaptureHider.h"
#include "qml/QmlDelayedCaptureIndicator.h"
#include "settings/RegionCaptureSettingsManager.h"

namespace {

// Countdown ticks are one second in production; tests shorten them.
constexpr int kTestTickMs = 20;
constexpr int kIndicatorRenderSettleMs = 100;
constexpr int kIndicatorSurfaceCatchUpMs = 2000;

QPixmap solidSnapshot(QScreen* screen)
{
    const qreal dpr = screen->devicePixelRatio();
    QPixmap pixmap(screen->geometry().size() * dpr);
    pixmap.fill(Qt::darkCyan);
    pixmap.setDevicePixelRatio(dpr);
    return pixmap;
}

QList<int> countdownValues(const QSignalSpy& spy)
{
    QList<int> values;
    for (const QList<QVariant>& arguments : spy) {
        values.append(arguments.at(0).toInt());
    }
    return values;
}

} // namespace

class tst_DelayedCapture : public QObject
{
    Q_OBJECT

private slots:
    void cleanup();

    void countdown_ticksHidesIndicatorThenOpensRegionSelector();
    void cancel_stopsCountdownWithoutGrabbing();
    void cancel_duringSettleDelayDoesNotGrab();
    void cancelCapture_alsoCancelsCountdown();
    void secondStart_whilePendingIsIgnored();
    void otherEntryPoints_rejectedWhilePending();
    void invalidDelay_fallsBackToDefault();
    void indicator_fitsContentAcrossScreens_data();
    void indicator_fitsContentAcrossScreens();

private:
    void closeSelector(CaptureManager& manager);
};

void tst_DelayedCapture::closeSelector(CaptureManager& manager)
{
    if (manager.m_regionSelector) {
        QPointer<RegionSelector> selector = manager.m_regionSelector;
        selector->close();
        QTRY_VERIFY(!selector);
    }
}

void tst_DelayedCapture::cleanup()
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void tst_DelayedCapture::countdown_ticksHidesIndicatorThenOpensRegionSelector()
{
    CaptureManager manager(nullptr);
    manager.m_delayedCaptureTickIntervalMs = kTestTickMs;

    int grabs = 0;
    bool indicatorVisibleAtGrab = true;
    bool pendingAtGrab = true;
    manager.m_snapshotOverride = [&](QScreen* screen) {
        ++grabs;
        indicatorVisibleAtGrab = manager.m_delayedCaptureIndicator
            && manager.m_delayedCaptureIndicator->isVisible();
        pendingAtGrab = manager.isDelayedCapturePending();
        return solidSnapshot(screen);
    };
    QSignalSpy started(&manager, &CaptureManager::captureStarted);
    QSignalSpy cancelled(&manager, &CaptureManager::captureCancelled);
    QSignalSpy state(&manager, &CaptureManager::delayedCaptureStateChanged);
    QSignalSpy countdown(&manager, &CaptureManager::delayedCaptureCountdownChanged);

    manager.startDelayedRegionCapture(3, false);

    // Counting down: the capture owns the screen but nothing is grabbed yet.
    QVERIFY(manager.isDelayedCapturePending());
    QVERIFY(manager.isActive());
    QCOMPARE(manager.delayedCaptureRemainingSeconds(), 3);
    QCOMPARE(grabs, 0);
    QCOMPARE(started.count(), 0);
    QVERIFY(manager.m_delayedCaptureIndicator);
    QVERIFY(manager.m_delayedCaptureIndicator->isVisible());
    QVERIFY(manager.m_delayedCaptureIndicator->window());
    // The indicator must never take input away from the UI being captured.
    QVERIFY(manager.m_delayedCaptureIndicator->window()->flags().testFlag(
        Qt::WindowTransparentForInput));
    QVERIFY(manager.m_delayedCaptureIndicator->window()->flags().testFlag(
        Qt::WindowDoesNotAcceptFocus));

    QTRY_COMPARE(grabs, 1);
    QVERIFY(!indicatorVisibleAtGrab);
    QVERIFY(!pendingAtGrab);
    QVERIFY(!manager.m_delayedCaptureIndicator->isVisible());
    QCOMPARE(countdownValues(countdown), (QList<int>{3, 2, 1, 0}));
    QCOMPARE(state.count(), 2);
    QCOMPARE(state.at(0).at(0).toBool(), true);
    QCOMPARE(state.at(1).at(0).toBool(), false);

    // The countdown hands over to an ordinary region capture.
    QCOMPARE(started.count(), 1);
    QCOMPARE(cancelled.count(), 0);
    QTRY_VERIFY(manager.m_regionSelector && manager.m_regionSelector->isVisible());
    QVERIFY(manager.isActive());
    QVERIFY(!manager.isDelayedCapturePending());

    closeSelector(manager);
}

void tst_DelayedCapture::cancel_stopsCountdownWithoutGrabbing()
{
    CaptureManager manager(nullptr);
    manager.m_delayedCaptureTickIntervalMs = kTestTickMs;
    int grabs = 0;
    manager.m_snapshotOverride = [&](QScreen* screen) {
        ++grabs;
        return solidSnapshot(screen);
    };
    QSignalSpy started(&manager, &CaptureManager::captureStarted);
    QSignalSpy cancelled(&manager, &CaptureManager::captureCancelled);
    QSignalSpy state(&manager, &CaptureManager::delayedCaptureStateChanged);

    manager.startDelayedRegionCapture(10);
    QVERIFY(manager.isDelayedCapturePending());
    QVERIFY(manager.m_delayedCaptureIndicator->isVisible());

    manager.cancelDelayedCapture();
    QVERIFY(!manager.isDelayedCapturePending());
    QVERIFY(!manager.isActive());
    QCOMPARE(manager.delayedCaptureRemainingSeconds(), 0);
    QVERIFY(!manager.m_delayedCaptureIndicator->isVisible());
    QCOMPARE(cancelled.count(), 1);
    QCOMPARE(state.count(), 2);
    QCOMPARE(state.at(1).at(0).toBool(), false);

    // A second cancel is a no-op.
    manager.cancelDelayedCapture();
    QCOMPARE(cancelled.count(), 1);

    // Nothing fires later: the stopped countdown never grabs or opens the selector.
    QTest::qWait(kTestTickMs * 12 + snaptray::capture::OwnWindowCaptureHider::kSettleDelayMs);
    QCOMPARE(grabs, 0);
    QCOMPARE(started.count(), 0);
    QVERIFY(!manager.m_regionSelector);
}

void tst_DelayedCapture::cancel_duringSettleDelayDoesNotGrab()
{
    CaptureManager manager(nullptr);
    manager.m_delayedCaptureTickIntervalMs = kTestTickMs;
    int grabs = 0;
    manager.m_snapshotOverride = [&](QScreen* screen) {
        ++grabs;
        return solidSnapshot(screen);
    };
    QSignalSpy countdown(&manager, &CaptureManager::delayedCaptureCountdownChanged);
    QSignalSpy cancelled(&manager, &CaptureManager::captureCancelled);

    manager.startDelayedRegionCapture(1);
    // Wait for the final tick: indicator hidden, grab scheduled after the settle delay.
    QTRY_COMPARE(countdownValues(countdown), (QList<int>{1, 0}));
    QVERIFY(manager.isDelayedCapturePending());
    QVERIFY(!manager.m_delayedCaptureIndicator->isVisible());

    manager.cancelDelayedCapture();
    QVERIFY(!manager.isDelayedCapturePending());
    QCOMPARE(cancelled.count(), 1);

    QTest::qWait(snaptray::capture::OwnWindowCaptureHider::kSettleDelayMs * 2);
    QCOMPARE(grabs, 0);
    QVERIFY(!manager.m_regionSelector);
}

void tst_DelayedCapture::cancelCapture_alsoCancelsCountdown()
{
    CaptureManager manager(nullptr);
    manager.m_delayedCaptureTickIntervalMs = kTestTickMs;
    QSignalSpy cancelled(&manager, &CaptureManager::captureCancelled);

    manager.startDelayedRegionCapture(10);
    QVERIFY(manager.isActive());

    // The CLI preempts active captures through cancelCapture().
    manager.cancelCapture();
    QVERIFY(!manager.isDelayedCapturePending());
    QVERIFY(!manager.isActive());
    QCOMPARE(cancelled.count(), 1);
}

void tst_DelayedCapture::secondStart_whilePendingIsIgnored()
{
    CaptureManager manager(nullptr);
    manager.m_delayedCaptureTickIntervalMs = kTestTickMs * 50;
    QSignalSpy state(&manager, &CaptureManager::delayedCaptureStateChanged);

    manager.startDelayedRegionCapture(3);
    QCOMPARE(manager.delayedCaptureRemainingSeconds(), 3);

    manager.startDelayedRegionCapture(10);
    QCOMPARE(manager.delayedCaptureRemainingSeconds(), 3);
    QCOMPARE(state.count(), 1);

    manager.cancelDelayedCapture();
    QVERIFY(!manager.isDelayedCapturePending());
}

void tst_DelayedCapture::otherEntryPoints_rejectedWhilePending()
{
    CaptureManager manager(nullptr);
    manager.m_delayedCaptureTickIntervalMs = kTestTickMs * 50;
    int grabs = 0;
    manager.m_snapshotOverride = [&](QScreen* screen) {
        ++grabs;
        return solidSnapshot(screen);
    };
    QSignalSpy started(&manager, &CaptureManager::captureStarted);

    manager.startDelayedRegionCapture(10);
    QVERIFY(manager.isDelayedCapturePending());

    manager.startRegionCapture(true);
    manager.startQuickPinCapture();
    manager.startFullScreenCapture();
    manager.startRegionCaptureWithPreset(QRect(0, 0, 10, 10), QGuiApplication::primaryScreen());

    QCOMPARE(started.count(), 0);
    QCOMPARE(grabs, 0);
    QVERIFY(!manager.isFullScreenCaptureInProgress());
    QVERIFY(!manager.m_regionSelector);
    QVERIFY(manager.isDelayedCapturePending());

    manager.cancelDelayedCapture();
}

void tst_DelayedCapture::invalidDelay_fallsBackToDefault()
{
    CaptureManager manager(nullptr);
    manager.m_delayedCaptureTickIntervalMs = kTestTickMs * 50;

    manager.startDelayedRegionCapture(0);
    QVERIFY(manager.isDelayedCapturePending());
    QCOMPARE(manager.delayedCaptureRemainingSeconds(),
             RegionCaptureSettingsManager::kDefaultDelayedCaptureSeconds);
    manager.cancelDelayedCapture();

    manager.startDelayedRegionCapture(RegionCaptureSettingsManager::kMaxDelayedCaptureSeconds + 1);
    QVERIFY(manager.isDelayedCapturePending());
    QCOMPARE(manager.delayedCaptureRemainingSeconds(),
             RegionCaptureSettingsManager::kDefaultDelayedCaptureSeconds);
    manager.cancelDelayedCapture();
}

void tst_DelayedCapture::indicator_fitsContentAcrossScreens_data()
{
    QTest::addColumn<int>("firstScreen");
    const auto screens = QGuiApplication::screens();
    for (int index = 0; index < screens.size(); ++index) {
        const QString name = QStringLiteral("%1-%2").arg(index).arg(screens.at(index)->name());
        QTest::newRow(qPrintable(name)) << index;
    }
}

void tst_DelayedCapture::indicator_fitsContentAcrossScreens()
{
    QFETCH(int, firstScreen);
    SnapTray::QmlDelayedCaptureIndicator indicator;
    auto screens = QGuiApplication::screens();
    QVERIFY(!screens.isEmpty());
    screens.move(firstScreen, 0);
    // Reuse the same native window in both directions across mixed-DPI screens.
    screens.append(screens.first());
    for (QScreen* screen : screens) {
        indicator.show(screen, 5);
        auto* view = qobject_cast<QQuickView*>(indicator.window());
        QVERIFY(view);
        QVERIFY(QTest::qWaitForWindowExposed(view));
        QCOMPARE(view->screen(), screen);
        auto* root = view->rootObject();
        QVERIFY(root);

        // Single-digit ticks keep the implicit size unchanged: this used to leave
        // the native window at the previous screen's pixel size. Also cover 10 -> 9.
        for (int seconds : {5, 4, 10, 9}) {
            indicator.setRemainingSeconds(seconds);
            QTest::qWait(kIndicatorRenderSettleMs);

            // A native surface smaller than the QML pill clips its right/bottom edges.
            QVERIFY(view->width() >= qFloor(root->implicitWidth()));
            QVERIFY(view->height() >= qFloor(root->implicitHeight()));
            QVERIFY(view->width() <= qCeil(root->implicitWidth()));
            QVERIFY(view->height() <= qCeil(root->implicitHeight()));
            // The rendered surface must follow the window to its new pixel
            // size. A virtual display (CI's) can hand back the previous frame
            // for a moment after a resize, so wait for it to catch up; one
            // that stays at the old size still fails.
            const qreal dpr = view->devicePixelRatio();
            const QSize expected(qFloor(view->width() * dpr), qFloor(view->height() * dpr));
            QImage image;
            QTRY_VERIFY2_WITH_TIMEOUT((image = view->grabWindow(),
                                       image.width() >= expected.width() && image.height() >= expected.height()),
                                      qPrintable(QStringLiteral("grabbed %1x%2, window needs %3x%4")
                                                     .arg(image.width()).arg(image.height())
                                                     .arg(expected.width()).arg(expected.height())),
                                      kIndicatorSurfaceCatchUpMs);
            const QString artifactDir = qEnvironmentVariable("SNAPTRAY_TEST_ARTIFACT_DIR");
            if (!artifactDir.isEmpty()) {
                QVERIFY(QDir().mkpath(artifactDir));
                QVERIFY(image.save(QDir(artifactDir).filePath(
                    QStringLiteral("indicator-%1-%2-%3.png")
                        .arg(firstScreen).arg(QGuiApplication::screens().indexOf(screen)).arg(seconds))));
            }
        }
        indicator.hide();
    }
}

QTEST_MAIN(tst_DelayedCapture)
#include "tst_DelayedCapture.moc"
