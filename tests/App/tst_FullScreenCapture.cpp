#include <QtTest/QtTest>

#include <QApplication>
#include <QCursor>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QImage>
#include <QMenu>
#include <QLabel>
#include <utility>
#include "capture/PostCaptureCard.h"
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QScreen>
#include <QTemporaryDir>
#include <QWidget>
#include <QWindow>

#include <cmath>
#include <memory>
#include <optional>

#include "CaptureManager.h"
#include "RegionSelector.h"
#include "PinWindowManager.h"
#include "PinWindow.h"
#include "../RegionSelector/RegionSelectorTestAccess.h"
#include "capture/OwnWindowCaptureHider.h"
#include "annotations/AnnotationLayer.h"
#include "history/AnnotationSerializer.h"
#include "history/HistoryRecorder.h"
#include "history/HistoryStore.h"
#include "settings/FileSettingsManager.h"
#include "settings/RegionCaptureSettingsManager.h"

using FullScreenCaptureAction = RegionCaptureSettingsManager::FullScreenCaptureAction;

namespace {

// Offscreen screens use logical geometry plus a per-screen device pixel ratio.
// "Native" is what the platform grab returns for that screen: Windows rounds
// fractional logical sizes, so 2560 px at 150% reports a 1707 px wide screen.
struct TestScreen {
    const char* name;
    QRect logicalGeometry;
    qreal dpr;
    QSize nativeSize;
};

const TestScreen kTestScreens[] = {
    {"Primary100", QRect(0, 0, 1920, 1080), 1.0, QSize(1920, 1080)},
    {"Right150", QRect(1920, -120, 1707, 960), 1.5, QSize(2560, 1440)},
    {"Far125", QRect(3627, 0, 1536, 864), 1.25, QSize(1920, 1080)},
    {"Left200", QRect(-1440, 0, 1440, 900), 2.0, QSize(2880, 1800)},
    {"Above275", QRect(0, -785, 1396, 785), 2.75, QSize(3840, 2160)},
};

const QColor kTopLeftMarker(255, 0, 0);
const QColor kBottomRightMarker(0, 0, 255);
const QColor kFill(40, 160, 90);

QByteArray offscreenConfigJson()
{
    QByteArray json = "{\"screens\":[";
    bool first = true;
    for (const TestScreen& screen : kTestScreens) {
        if (!first) {
            json += ',';
        }
        first = false;
        json += QStringLiteral(
            "{\"name\":\"%1\",\"x\":%2,\"y\":%3,\"width\":%4,\"height\":%5,"
            "\"logicalDpi\":96,\"logicalBaseDpi\":96,\"dpr\":%6}")
            .arg(QString::fromLatin1(screen.name))
            .arg(screen.logicalGeometry.x())
            .arg(screen.logicalGeometry.y())
            .arg(screen.logicalGeometry.width())
            .arg(screen.logicalGeometry.height())
            .arg(screen.dpr)
            .toUtf8();
    }
    json += "]}";
    return json;
}

const TestScreen* testScreenFor(const QScreen* screen)
{
    for (const TestScreen& testScreen : kTestScreens) {
        if (screen && screen->name() == QLatin1String(testScreen.name)) {
            return &testScreen;
        }
    }
    return nullptr;
}

QScreen* screenNamed(const QString& name)
{
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->name() == name) {
            return screen;
        }
    }
    return nullptr;
}

// Mimics the platform grab: native-sized pixels tagged with the screen DPR.
QPixmap nativeSnapshot(QScreen* screen)
{
    const TestScreen* testScreen = testScreenFor(screen);
    if (!testScreen) {
        return {};
    }

    QImage image(testScreen->nativeSize, QImage::Format_RGB32);
    image.fill(kFill);
    image.setPixelColor(0, 0, kTopLeftMarker);
    image.setPixelColor(image.width() - 1, image.height() - 1, kBottomRightMarker);

    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(testScreen->dpr);
    return pixmap;
}

void verifyNativeImage(const QImage& image, const TestScreen& screen)
{
    QCOMPARE(image.size(), screen.nativeSize);
    QCOMPARE(image.pixelColor(0, 0), kTopLeftMarker);
    QCOMPARE(image.pixelColor(image.width() - 1, image.height() - 1), kBottomRightMarker);
    QCOMPARE(image.pixelColor(image.width() / 2, image.height() / 2), kFill);
}

} // namespace

class tst_FullScreenCapture : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();
    void cleanupTestCase();

    void regionCardReopensItsCapture();
    void cardActionsUseOriginalRegionMetadata();
    void cardRetainsImageAndRetiresCallbacks();
    void automaticSaveWithoutCardKeepsOriginalPixels_data();
    void automaticSaveWithoutCardKeepsOriginalPixels();
    void cardPausesAndRetriesFailedSave();
    void lateExportAfterDismissalKeepsCardHidden_data();
    void lateExportAfterDismissalKeepsCardHidden();
    void nativeCardFocusEscapeAndCaptureExclusion();
    void offscreenScreensMatchConfiguration();
    void copy_outputsNativePixelsPerScreen_data();
    void copy_outputsNativePixelsPerScreen();
    void save_writesNativePixelsPerScreen_data();
    void save_writesNativePixelsPerScreen();
    void edit_opensSelectorWithFullScreenPreset_data();
    void edit_opensSelectorWithFullScreenPreset();
    void ownWindows_areTransparentDuringGrabAndRestoredAfter();
    void ownWindows_restoredWhenGrabFails();
    void ownWindowHider_skipsHiddenWindowsAndKeepsCustomOpacity();
    void secondRequestWhilePending_isIgnored();
    void otherCaptureEntryPoints_rejectedWhilePending();
    void otherCaptureEntryPoints_rejectedDuringExport();
    void cancelCapture_whilePending_restoresWindowsWithoutGrabbing();
    void windowShownDuringSettleDelay_isHiddenBeforeGrab();
    void recentlyClosedMenu_waitsBeforeGrab();
    void windowShownAfterCaptureStarts_isHiddenBeforeGrab();
    void windowsKeepAppearing_cancelsInsteadOfGrabbing();
    void libraryReopen_refusesDamagedEntryAndOpensIntactOne_data();
    void libraryReopen_refusesDamagedEntryAndOpensIntactOne();

private:
    void configure(CaptureManager& manager, std::optional<QImage>* copiedImage);
    QTemporaryDir m_historyDirectory;
    QTemporaryDir m_saveDirectory;
    QByteArray m_previousHistoryDirectory;
};

void tst_FullScreenCapture::initTestCase()
{
    m_previousHistoryDirectory = qgetenv("SNAPTRAY_HISTORY_DIR");
    QVERIFY(m_historyDirectory.isValid());
    QVERIFY(m_saveDirectory.isValid());
    qputenv("SNAPTRAY_HISTORY_DIR", m_historyDirectory.path().toLocal8Bit());
}

void tst_FullScreenCapture::init()
{
    FileSettingsManager::instance().saveAutoSaveScreenshots(true);
    FileSettingsManager::instance().saveScreenshotPath(m_saveDirectory.path());
}

void tst_FullScreenCapture::cleanup()
{
    auto& prefs = RegionCaptureSettingsManager::instance();
    prefs.setPostCaptureSave(false);
    prefs.setPostCaptureCard(false);
    prefs.setPostCaptureCopy(false);
    QCoreApplication::sendPostedEvents(qApp, QEvent::MetaCall);
    QVERIFY(SnapTray::HistoryRecorder::instance().waitForIdleForTests());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void tst_FullScreenCapture::cleanupTestCase()
{
    if (m_previousHistoryDirectory.isNull()) {
        qunsetenv("SNAPTRAY_HISTORY_DIR");
    } else {
        qputenv("SNAPTRAY_HISTORY_DIR", m_previousHistoryDirectory);
    }
}

void tst_FullScreenCapture::configure(CaptureManager& manager, std::optional<QImage>* copiedImage)
{
    manager.m_snapshotOverride = nativeSnapshot;
    manager.m_clipboardWriterOverride =
        [copiedImage](const QImage& image, PlatformFeatures::ClipboardCopyCompletion done) {
            if (copiedImage) {
                *copiedImage = image;
            }
            done(PlatformFeatures::ClipboardCopyResult::Success);
        };
}

void tst_FullScreenCapture::offscreenScreensMatchConfiguration()
{
    QCOMPARE(QGuiApplication::screens().size(), int(std::size(kTestScreens)));
    for (const TestScreen& testScreen : kTestScreens) {
        QScreen* screen = screenNamed(QString::fromLatin1(testScreen.name));
        QVERIFY2(screen, testScreen.name);
        QCOMPARE(screen->geometry(), testScreen.logicalGeometry);
        QCOMPARE(screen->devicePixelRatio(), testScreen.dpr);
    }
}

void tst_FullScreenCapture::copy_outputsNativePixelsPerScreen_data()
{
    QTest::addColumn<QString>("screenName");
    for (const TestScreen& screen : kTestScreens) {
        QTest::newRow(screen.name) << QString::fromLatin1(screen.name);
    }
}

void tst_FullScreenCapture::copy_outputsNativePixelsPerScreen()
{
    QFETCH(QString, screenName);
    RegionCaptureSettingsManager::instance().setFullScreenCaptureAction(
        FullScreenCaptureAction::CopyToClipboard);

    CaptureManager manager(nullptr);
    std::optional<QImage> copiedImage;
    configure(manager, &copiedImage);
    QSignalSpy copied(&manager, &CaptureManager::copyCompleted);

    QScreen* screen = screenNamed(screenName);
    QVERIFY(screen);
    manager.startFullScreenCaptureOnScreen(screen);

    QTRY_COMPARE(copied.count(), 1);
    QVERIFY(copiedImage.has_value());
    verifyNativeImage(*copiedImage, *testScreenFor(screen));
    QVERIFY(!manager.isFullScreenCaptureInProgress());
}

void tst_FullScreenCapture::save_writesNativePixelsPerScreen_data()
{
    copy_outputsNativePixelsPerScreen_data();
}

void tst_FullScreenCapture::save_writesNativePixelsPerScreen()
{
    QFETCH(QString, screenName);
    RegionCaptureSettingsManager::instance().setFullScreenCaptureAction(
        FullScreenCaptureAction::Save);

    CaptureManager manager(nullptr);
    configure(manager, nullptr);
    QSignalSpy saved(&manager, &CaptureManager::saveCompleted);
    QSignalSpy failed(&manager, &CaptureManager::saveFailed);

    QScreen* screen = screenNamed(screenName);
    QVERIFY(screen);
    manager.startFullScreenCaptureOnScreen(screen);

    QTRY_VERIFY(saved.count() == 1 || failed.count() == 1);
    QCOMPARE(failed.count(), 0);
    const QString filePath = saved.first().at(1).toString();
    QVERIFY(QFile::exists(filePath));
    QVERIFY(filePath.startsWith(m_saveDirectory.path()));

    const QImage savedImage(filePath);
    verifyNativeImage(savedImage, *testScreenFor(screen));
}

void tst_FullScreenCapture::edit_opensSelectorWithFullScreenPreset_data()
{
    copy_outputsNativePixelsPerScreen_data();
}

void tst_FullScreenCapture::edit_opensSelectorWithFullScreenPreset()
{
#ifdef Q_OS_MACOS
    // RegionSelector and its floating toolbars call Cocoa APIs on their native
    // windows, which the offscreen platform used for the multi-monitor layout
    // does not provide. The editor hand-off is platform independent and is
    // covered on Linux and Windows.
    QSKIP("RegionSelector needs the Cocoa platform; this suite runs offscreen.");
#endif
    QFETCH(QString, screenName);
    RegionCaptureSettingsManager::instance().setFullScreenCaptureAction(
        FullScreenCaptureAction::Edit);

    CaptureManager manager(nullptr);
    configure(manager, nullptr);

    QScreen* screen = screenNamed(screenName);
    QVERIFY(screen);
    // RegionSelector follows the cursor across screens on a 50 ms timer.
    // Keep the pointer on the screen whose preset this test is inspecting.
    const QPoint previousCursor = QCursor::pos();
    auto restoreCursor = qScopeGuard([previousCursor] { QCursor::setPos(previousCursor); });
    QCursor::setPos(screen->geometry().center());
    manager.startFullScreenCaptureOnScreen(screen);

    QTRY_VERIFY(manager.m_regionSelector);
    QPointer<RegionSelector> selector = manager.m_regionSelector;
    auto closeSelector = qScopeGuard([&selector] { if (selector) selector->close(); });
    // Exercise the screen-switch timer as well as initial preset creation.
    QTest::qWait(100);
    QVERIFY(selector);
    QVERIFY(selector->isSelectionComplete());
    QCOMPARE(selector->backgroundPixmap().size(), testScreenFor(screen)->nativeSize);
    QCOMPARE(RegionSelectorTestAccess::selectionRect(*selector),
             QRect(QPoint(0, 0), screen->geometry().size()));

    // The editor exports the same native pixels as the direct paths.
    const QPixmap exported = RegionSelectorTestAccess::exportSelection(*selector);
    verifyNativeImage(exported.toImage(), *testScreenFor(screen));

    selector->close();
    QTRY_VERIFY(!selector);
}

void tst_FullScreenCapture::ownWindows_areTransparentDuringGrabAndRestoredAfter()
{
    RegionCaptureSettingsManager::instance().setFullScreenCaptureAction(
        FullScreenCaptureAction::CopyToClipboard);

    QWidget pin;
    pin.setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    pin.resize(200, 120);
    pin.setWindowOpacity(0.8);
    pin.show();
    QWidget toast;
    toast.setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
    toast.resize(240, 40);
    toast.show();
    QVERIFY(QTest::qWaitForWindowExposed(&pin));
    QVERIFY(QTest::qWaitForWindowExposed(&toast));

    CaptureManager manager(nullptr);
    configure(manager, nullptr);
    std::optional<qreal> pinOpacityAtGrab;
    std::optional<qreal> toastOpacityAtGrab;
    manager.m_snapshotOverride = [&](QScreen* screen) {
        pinOpacityAtGrab = pin.windowHandle()->opacity();
        toastOpacityAtGrab = toast.windowHandle()->opacity();
        return nativeSnapshot(screen);
    };
    QSignalSpy copied(&manager, &CaptureManager::copyCompleted);

    manager.startFullScreenCaptureOnScreen(screenNamed(QStringLiteral("Primary100")));

    // Hidden immediately, grabbed only after the compositor settle delay.
    QCOMPARE(pin.windowHandle()->opacity(), 0.0);
    QCOMPARE(toast.windowHandle()->opacity(), 0.0);
    QVERIFY(!pinOpacityAtGrab.has_value());

    QTRY_COMPARE(copied.count(), 1);
    QCOMPARE(*pinOpacityAtGrab, 0.0);
    QCOMPARE(*toastOpacityAtGrab, 0.0);
    QVERIFY(qAbs(pin.windowHandle()->opacity() - 0.8) < 0.01);
    QCOMPARE(toast.windowHandle()->opacity(), 1.0);
    QVERIFY(pin.isVisible());
    QVERIFY(toast.isVisible());
}

void tst_FullScreenCapture::ownWindows_restoredWhenGrabFails()
{
    QWidget pin;
    pin.resize(200, 120);
    pin.show();
    QVERIFY(QTest::qWaitForWindowExposed(&pin));

    CaptureManager manager(nullptr);
    configure(manager, nullptr);
    manager.m_snapshotOverride = [](QScreen*) { return QPixmap(); };
    QSignalSpy cancelled(&manager, &CaptureManager::captureCancelled);

    manager.startFullScreenCaptureOnScreen(screenNamed(QStringLiteral("Right150")));
    QCOMPARE(pin.windowHandle()->opacity(), 0.0);

    QTRY_COMPARE(cancelled.count(), 1);
    QCOMPARE(pin.windowHandle()->opacity(), 1.0);
    QVERIFY(!manager.isFullScreenCaptureInProgress());
}

void tst_FullScreenCapture::ownWindowHider_skipsHiddenWindowsAndKeepsCustomOpacity()
{
    QWidget visible;
    visible.resize(100, 100);
    visible.setWindowOpacity(0.5);
    visible.show();
    QWidget hidden;
    hidden.resize(100, 100);
    hidden.show();
    hidden.hide();
    QVERIFY(QTest::qWaitForWindowExposed(&visible));

    {
        snaptray::capture::OwnWindowCaptureHider hider;
        hider.hide();
        QVERIFY(hider.hasHiddenWindows());
        QCOMPARE(visible.windowHandle()->opacity(), 0.0);
        QCOMPARE(hidden.windowHandle()->opacity(), 1.0);

        // A second hide must not record the transparent state as "original".
        hider.hide();
        hider.restore();
        QVERIFY(!hider.hasHiddenWindows());
        QVERIFY(qAbs(visible.windowHandle()->opacity() - 0.5) < 0.01);

        // Destruction restores anything still hidden.
        hider.hide();
    }
    QVERIFY(qAbs(visible.windowHandle()->opacity() - 0.5) < 0.01);

    // Windows destroyed while hidden are skipped on restore.
    auto* transient = new QWidget;
    transient->resize(50, 50);
    transient->show();
    QVERIFY(QTest::qWaitForWindowExposed(transient));
    snaptray::capture::OwnWindowCaptureHider hider;
    hider.hide();
    delete transient;
    hider.restore();
}

void tst_FullScreenCapture::secondRequestWhilePending_isIgnored()
{
    RegionCaptureSettingsManager::instance().setFullScreenCaptureAction(
        FullScreenCaptureAction::CopyToClipboard);

    QWidget pin;
    pin.resize(100, 100);
    pin.show();
    QVERIFY(QTest::qWaitForWindowExposed(&pin));

    CaptureManager manager(nullptr);
    configure(manager, nullptr);
    int grabs = 0;
    manager.m_snapshotOverride = [&grabs](QScreen* screen) {
        ++grabs;
        return nativeSnapshot(screen);
    };
    QSignalSpy started(&manager, &CaptureManager::captureStarted);
    QSignalSpy copied(&manager, &CaptureManager::copyCompleted);

    QScreen* screen = screenNamed(QStringLiteral("Far125"));
    manager.startFullScreenCaptureOnScreen(screen);
    QVERIFY(manager.isFullScreenCaptureInProgress());
    manager.startFullScreenCaptureOnScreen(screen);

    QTRY_COMPARE(copied.count(), 1);
    QTest::qWait(snaptray::capture::OwnWindowCaptureHider::kSettleDelayMs * 2);
    QCOMPARE(started.count(), 1);
    QCOMPARE(grabs, 1);
    QCOMPARE(pin.windowHandle()->opacity(), 1.0);
}

void tst_FullScreenCapture::otherCaptureEntryPoints_rejectedWhilePending()
{
    RegionCaptureSettingsManager::instance().setFullScreenCaptureAction(
        FullScreenCaptureAction::CopyToClipboard);

    QWidget pin;
    pin.resize(100, 100);
    pin.show();
    QVERIFY(QTest::qWaitForWindowExposed(&pin));

    CaptureManager manager(nullptr);
    configure(manager, nullptr);
    int grabs = 0;
    manager.m_snapshotOverride = [&grabs](QScreen* screen) {
        ++grabs;
        return nativeSnapshot(screen);
    };
    QSignalSpy started(&manager, &CaptureManager::captureStarted);
    QSignalSpy copied(&manager, &CaptureManager::copyCompleted);

    QScreen* screen = screenNamed(QStringLiteral("Primary100"));
    manager.startFullScreenCaptureOnScreen(screen);

    // During the compositor delay the capture counts as active, so a selector
    // cannot open and end up in the full-screen image.
    QVERIFY(manager.isActive());
    manager.startRegionCapture();
    manager.startQuickPinCapture();
    manager.startRegionCaptureWithPreset(QRect(10, 10, 100, 100), screen);
    QVERIFY(!manager.startHistoryReplay(QStringLiteral("entry")));
    QVERIFY(!manager.m_regionSelector);
    QCOMPARE(started.count(), 1);

    QTRY_COMPARE(copied.count(), 1);
    QCOMPARE(grabs, 1);
    QVERIFY(!manager.isActive());
}

void tst_FullScreenCapture::otherCaptureEntryPoints_rejectedDuringExport()
{
    RegionCaptureSettingsManager::instance().setFullScreenCaptureAction(
        FullScreenCaptureAction::CopyToClipboard);

    CaptureManager manager(nullptr);
    configure(manager, nullptr);
    PlatformFeatures::ClipboardCopyCompletion finishClipboardWrite;
    manager.m_clipboardWriterOverride =
        [&finishClipboardWrite](const QImage&, PlatformFeatures::ClipboardCopyCompletion done) {
            finishClipboardWrite = std::move(done);
        };
    QSignalSpy copied(&manager, &CaptureManager::copyCompleted);

    QScreen* screen = screenNamed(QStringLiteral("Right150"));
    manager.startFullScreenCaptureOnScreen(screen);
    QTRY_VERIFY(finishClipboardWrite);

    QVERIFY(manager.isActive());
    manager.startRegionCapture();
    QVERIFY(!manager.m_regionSelector);
    manager.startFullScreenCaptureOnScreen(screen);
    QCOMPARE(copied.count(), 0);

    finishClipboardWrite(PlatformFeatures::ClipboardCopyResult::Success);
    QCOMPARE(copied.count(), 1);
    QVERIFY(!manager.isActive());
}

void tst_FullScreenCapture::cancelCapture_whilePending_restoresWindowsWithoutGrabbing()
{
    QWidget pin;
    pin.resize(100, 100);
    pin.setWindowOpacity(0.7);
    pin.show();
    QVERIFY(QTest::qWaitForWindowExposed(&pin));

    CaptureManager manager(nullptr);
    configure(manager, nullptr);
    int grabs = 0;
    manager.m_snapshotOverride = [&grabs](QScreen* screen) {
        ++grabs;
        return nativeSnapshot(screen);
    };
    QSignalSpy cancelled(&manager, &CaptureManager::captureCancelled);

    manager.startFullScreenCaptureOnScreen(screenNamed(QStringLiteral("Left200")));
    QCOMPARE(pin.windowHandle()->opacity(), 0.0);

    manager.cancelCapture();
    QCOMPARE(cancelled.count(), 1);
    QVERIFY(!manager.isActive());
    QVERIFY(qAbs(pin.windowHandle()->opacity() - 0.7) < 0.01);

    QTest::qWait(snaptray::capture::OwnWindowCaptureHider::kSettleDelayMs * 2);
    QCOMPARE(grabs, 0);
    QCOMPARE(cancelled.count(), 1);
}

void tst_FullScreenCapture::windowShownDuringSettleDelay_isHiddenBeforeGrab()
{
    RegionCaptureSettingsManager::instance().setFullScreenCaptureAction(
        FullScreenCaptureAction::CopyToClipboard);

    QWidget pin;
    pin.resize(100, 100);
    pin.show();
    QVERIFY(QTest::qWaitForWindowExposed(&pin));

    CaptureManager manager(nullptr);
    configure(manager, nullptr);
    QWidget latePin;  // e.g. a pin pasted from the clipboard mid-delay
    latePin.resize(120, 80);
    std::optional<qreal> latePinOpacityAtGrab;
    QElapsedTimer sinceStart;
    qint64 grabDelayMs = -1;
    manager.m_snapshotOverride = [&](QScreen* screen) {
        latePinOpacityAtGrab = latePin.windowHandle()->opacity();
        grabDelayMs = sinceStart.elapsed();
        return nativeSnapshot(screen);
    };
    QSignalSpy copied(&manager, &CaptureManager::copyCompleted);

    sinceStart.start();
    manager.startFullScreenCaptureOnScreen(screenNamed(QStringLiteral("Primary100")));
    QTimer::singleShot(snaptray::capture::OwnWindowCaptureHider::kSettleDelayMs / 2,
                       &latePin, [&latePin]() { latePin.show(); });

    QTRY_COMPARE(copied.count(), 1);
    QVERIFY(latePinOpacityAtGrab.has_value());
    QCOMPARE(*latePinOpacityAtGrab, 0.0);
    // The late window got its own settle round before the grab.
    QVERIFY(grabDelayMs >= 2 * snaptray::capture::OwnWindowCaptureHider::kSettleDelayMs - 10);
    QCOMPARE(latePin.windowHandle()->opacity(), 1.0);
    QCOMPARE(pin.windowHandle()->opacity(), 1.0);
}

void tst_FullScreenCapture::recentlyClosedMenu_waitsBeforeGrab()
{
    using snaptray::capture::OwnWindowCaptureHider;
    RegionCaptureSettingsManager::instance().setFullScreenCaptureAction(
        FullScreenCaptureAction::CopyToClipboard);

    QMenu menu;
    menu.addAction(QStringLiteral("Full Screen Capture"));
    menu.popup(QPoint(20, 20));
    QVERIFY(QTest::qWaitForWindowExposed(&menu));
    // QMenu closes before dispatching the action, leaving no visible window
    // for the capture hider to detect, but potentially a stale composed frame.
    menu.close();
    QVERIFY(!menu.isVisible());
    OwnWindowCaptureHider hider;
    QCOMPARE(hider.hide(), 0);

    CaptureManager manager(nullptr);
    configure(manager, nullptr);
    QElapsedTimer sinceStart;
    qint64 grabDelayMs = -1;
    manager.m_snapshotOverride = [&](QScreen* screen) {
        grabDelayMs = sinceStart.elapsed();
        return nativeSnapshot(screen);
    };
    QSignalSpy copied(&manager, &CaptureManager::copyCompleted);

    sinceStart.start();
    manager.startFullScreenCaptureOnScreen(screenNamed(QStringLiteral("Primary100")));
    QTRY_COMPARE(copied.count(), 1);
    // Allow coarse timer rounding, as in the late-window settle test.
    QVERIFY(grabDelayMs >= OwnWindowCaptureHider::kSettleDelayMs - 10);
}

void tst_FullScreenCapture::windowShownAfterCaptureStarts_isHiddenBeforeGrab()
{
    RegionCaptureSettingsManager::instance().setFullScreenCaptureAction(
        FullScreenCaptureAction::CopyToClipboard);

    CaptureManager manager(nullptr);
    configure(manager, nullptr);
    QWidget toast;  // nothing else visible when capture starts
    toast.resize(200, 40);
    std::optional<qreal> toastOpacityAtGrab;
    manager.m_snapshotOverride = [&](QScreen* screen) {
        toastOpacityAtGrab = toast.windowHandle()->opacity();
        return nativeSnapshot(screen);
    };
    QSignalSpy copied(&manager, &CaptureManager::copyCompleted);

    manager.startFullScreenCaptureOnScreen(screenNamed(QStringLiteral("Far125")));
    toast.show();

    QTRY_COMPARE(copied.count(), 1);
    QVERIFY(toastOpacityAtGrab.has_value());
    QCOMPARE(*toastOpacityAtGrab, 0.0);
    QCOMPARE(toast.windowHandle()->opacity(), 1.0);
}

void tst_FullScreenCapture::windowsKeepAppearing_cancelsInsteadOfGrabbing()
{
    using snaptray::capture::OwnWindowCaptureHider;
    RegionCaptureSettingsManager::instance().setFullScreenCaptureAction(
        FullScreenCaptureAction::CopyToClipboard);

    QWidget pin;
    pin.resize(100, 100);
    pin.show();
    QVERIFY(QTest::qWaitForWindowExposed(&pin));

    CaptureManager manager(nullptr);
    configure(manager, nullptr);
    int grabs = 0;
    manager.m_snapshotOverride = [&grabs](QScreen* screen) {
        ++grabs;
        return nativeSnapshot(screen);
    };
    QSignalSpy cancelled(&manager, &CaptureManager::captureCancelled);
    QSignalSpy copied(&manager, &CaptureManager::copyCompleted);

    // A new window shows up during every settle interval, including the last.
    std::vector<std::unique_ptr<QWidget>> lateWindows;
    QTimer spawner;
    spawner.setInterval(OwnWindowCaptureHider::kSettleDelayMs / 2);
    QObject::connect(&spawner, &QTimer::timeout, [&lateWindows]() {
        auto window = std::make_unique<QWidget>();
        window->resize(60, 40);
        window->show();
        lateWindows.push_back(std::move(window));
    });
    spawner.start();

    manager.startFullScreenCaptureOnScreen(screenNamed(QStringLiteral("Right150")));

    QTRY_COMPARE_WITH_TIMEOUT(cancelled.count(), 1,
        (OwnWindowCaptureHider::kMaxSettleRounds + 3) * OwnWindowCaptureHider::kSettleDelayMs);
    spawner.stop();

    QCOMPARE(grabs, 0);
    QCOMPARE(copied.count(), 0);
    QVERIFY(!manager.isActive());
    QCOMPARE(pin.windowHandle()->opacity(), 1.0);
    for (const auto& window : lateWindows) {
        QCOMPARE(window->windowHandle()->opacity(), 1.0);
    }
}

// #352: the Library's "Edit" opens a selector only for an entry whose whole
// document loads. A damaged one starts no capture at all, rather than showing
// the bare canvas as if it were the saved picture.
void tst_FullScreenCapture::libraryReopen_refusesDamagedEntryAndOpensIntactOne_data()
{
    QTest::addColumn<bool>("removeAnnotations");
    QTest::addColumn<QByteArray>("payload");
    QTest::newRow("missing-file") << true << QByteArray();
    QTest::newRow("empty-file") << false << QByteArray();
    QTest::newRow("truncated-json") << false << QByteArray("{");
    QTest::newRow("root-not-object") << false << QByteArray("[]");
    QTest::newRow("items-missing") << false << QByteArray("{}");
    QTest::newRow("items-not-array") << false << QByteArray("{\"items\":{}}");
    QTest::newRow("item-not-object") << false << QByteArray("{\"items\":[42]}");
    QTest::newRow("unknown-item-type") << false << QByteArray("{\"items\":[{\"type\":\"hologram\"}]}");
}

void tst_FullScreenCapture::libraryReopen_refusesDamagedEntryAndOpensIntactOne()
{
#ifdef Q_OS_MACOS
    QSKIP("RegionSelector needs the Cocoa platform; this suite runs offscreen.");
#endif
    QFETCH(bool, removeAnnotations);
    QFETCH(QByteArray, payload);

    SnapTray::CaptureSessionWriteRequest request;
    request.canvasImage = QImage(400, 300, QImage::Format_RGB32);
    request.canvasImage.fill(Qt::blue);
    request.selectionRect = QRect(5, 5, 80, 60);
    request.resultImage = request.canvasImage.copy(request.selectionRect);
    request.canvasLogicalSize = request.canvasImage.size();
    request.annotationsJson = SnapTray::serializeAnnotationLayer(AnnotationLayer());
    const auto intact = SnapTray::HistoryStore::writeCaptureSession(request);
    QVERIFY(intact);
    const auto damaged = SnapTray::HistoryStore::writeCaptureSession(request);
    QVERIFY(damaged);
    auto removeEntries = qScopeGuard([&] {
        SnapTray::HistoryStore::deleteEntry(*intact);
        SnapTray::HistoryStore::deleteEntry(*damaged);
    });
    if (removeAnnotations) {
        QVERIFY(QFile::remove(damaged->annotationsPath));
    } else {
        QFile file(damaged->annotationsPath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(payload), payload.size());
    }

    CaptureManager manager(nullptr);
    configure(manager, nullptr);
    QSignalSpy started(&manager, &CaptureManager::captureStarted);
    QSignalSpy cancelled(&manager, &CaptureManager::captureCancelled);

    // Refused before a capture starts: no screen grab, no selector flashing up.
    QVERIFY(!manager.startHistoryReplay(damaged->id));
    QCOMPARE(started.count(), 0);
    QCOMPARE(cancelled.count(), 0);
    QVERIFY(!manager.isActive());
    QVERIFY(!manager.m_regionSelector);

    QVERIFY(manager.startHistoryReplay(intact->id));
    QCOMPARE(started.count(), 1);
    QCOMPARE(cancelled.count(), 0);
    QPointer<RegionSelector> selector = manager.m_regionSelector;
    QVERIFY(selector);
    auto closeSelector = qScopeGuard([&selector] { if (selector) selector->close(); });
    QVERIFY(manager.isActive());
    QCOMPARE(RegionSelectorTestAccess::selectionRect(*selector), request.selectionRect);
    QCOMPARE(selector->backgroundPixmap().size(), request.canvasImage.size());

    // With the selector open, the Library reaches the same entry through it;
    // the refusal leaves what is on screen alone.
    QVERIFY(!manager.startHistoryReplay(damaged->id));
    QVERIFY(selector && selector->isVisible());
    QCOMPARE(RegionSelectorTestAccess::selectionRect(*selector), request.selectionRect);
    QCOMPARE(cancelled.count(), 0);

    selector->close();
    QTRY_VERIFY(!selector);
}

void tst_FullScreenCapture::regionCardReopensItsCapture()
{
#ifdef Q_OS_MACOS
    QSKIP("RegionSelector requires the native Cocoa platform.");
#endif
    auto& prefs = RegionCaptureSettingsManager::instance();
    prefs.setPostCaptureCard(true);
    prefs.setPostCaptureCopy(true);
    CaptureManager manager(nullptr);
    std::optional<QImage> copiedImage;
    configure(manager, &copiedImage);
    auto* screen = screenNamed("Primary100");
    const QRect selection(36, 48, 120, 75);
    manager.startPresetRegionCapture(screen, selection, nativeSnapshot(screen));
    QPointer<RegionSelector> original = manager.m_regionSelector;
    QVERIFY(original);
    RegionSelectorTestAccess::finishSelection(*original);
    QTRY_VERIFY(!original);
    QVERIFY(copiedImage && copiedImage->size() == selection.size());
    QPointer<PostCaptureCard> card = manager.m_postCaptureCard;
    QVERIFY(card && card->isVisible());
    QTRY_VERIFY(card->m_edit->isEnabled());
    card->m_edit->trigger();
    QPointer<RegionSelector> reopened = manager.m_regionSelector;
    QVERIFY(reopened && reopened->isVisible());
    QCOMPARE(RegionSelectorTestAccess::selectionRect(*reopened), selection);
    QVERIFY(!card || !card->isVisible());
    manager.cancelCapture();
    QTRY_VERIFY(!reopened);
}

void tst_FullScreenCapture::cardActionsUseOriginalRegionMetadata()
{
#ifdef Q_OS_MACOS
    QSKIP("PinWindow requires the native Cocoa platform.");
#endif
    auto& prefs = RegionCaptureSettingsManager::instance();
    prefs.setPostCaptureCard(true);
    auto& files = FileSettingsManager::instance();
    const QString previousTemplate = files.loadFilenameTemplate();
    auto restoreTemplate = qScopeGuard([&] { files.saveFilenameTemplate(previousTemplate); });
    files.saveFilenameTemplate(QStringLiteral("{windowTitle}_{appName}_{regionIndex}.{ext}"));
    PinWindowManager pins;
    CaptureManager manager(&pins);
    auto* screen = screenNamed("Primary100");
    QPixmap picture(160, 80);
    picture.fill(Qt::red);
    ScreenshotOrigin origin;
    origin.windowTitle = QStringLiteral("OriginalTitle");
    origin.ownerApp = QStringLiteral("OriginalApp");
    origin.regionIndex = 2;
    origin.globalRect = QRect(QPoint(12, 32), picture.size());
    LayoutRegion left;
    left.rect = QRect(0, 0, 80, 80);
    left.originalRect = left.rect;
    left.image = picture.copy(left.rect).toImage();
    LayoutRegion right = left;
    right.rect.translate(80, 0);
    right.originalRect = right.rect;
    origin.layout = {left, right};
    manager.afterScreenshot(picture, origin, screen, CaptureManager::PrimaryAction::Pin, manager.m_postCaptureGeneration);
    origin = {};
    QPointer<PostCaptureCard> card = manager.m_postCaptureCard;
    QVERIFY(card);
    QSignalSpy saved(&manager, &CaptureManager::saveCompleted);
    emit card->actionRequested(PostCaptureCard::Action::Save);
    QTRY_COMPARE(saved.size(), 1);
    const QString name = QFileInfo(saved.front().at(1).toString()).fileName();
    QVERIFY2(name.startsWith(QStringLiteral("OriginalTitle_OriginalApp_2")), qPrintable(name));
    emit card->actionRequested(PostCaptureCard::Action::Pin);
    QCOMPARE(pins.windowCount(), 1);
    QVERIFY(pins.windows().first()->hasMultiRegionData());
    pins.closeAllWindows();
}

void tst_FullScreenCapture::cardRetainsImageAndRetiresCallbacks()
{
    using Action = PostCaptureCard::Action;
    auto& prefs = RegionCaptureSettingsManager::instance();
    prefs.setPostCaptureCard(true);
    prefs.setFullScreenCaptureAction(FullScreenCaptureAction::CopyToClipboard);
    CaptureManager manager(nullptr);
    auto* screen = screenNamed("Right150");
    QVERIFY(screen);
    QColor color = Qt::red;
    manager.m_snapshotOverride = [&color](QScreen* source) {
        QPixmap result(source->size() * source->devicePixelRatio());
        result.setDevicePixelRatio(source->devicePixelRatio());
        result.fill(color);
        return result;
    };
    QImage copied;
    PlatformFeatures::ClipboardCopyCompletion done;
    manager.m_clipboardWriterOverride = [&](const QImage& image, auto callback) {
        copied = image;
        done = std::move(callback);
    };
    manager.startFullScreenCaptureOnScreen(screen);
    QTRY_VERIFY(done);
    // Even the first copy can fail. The card must still offer the same image.
    std::exchange(done, {})(PlatformFeatures::ClipboardCopyResult::Failed);
    QPointer<PostCaptureCard> first = manager.m_postCaptureCard;
    QVERIFY(first && first->isVisible());
    QVERIFY2(screen->availableGeometry().contains(first->geometry()),
             qPrintable(QDebug::toString(std::pair(screen->availableGeometry(), first->geometry()))));
    QVERIFY(SnapTray::HistoryRecorder::instance().waitForIdleForTests());
    const auto entries = SnapTray::HistoryStore::loadEntries();
    QVERIFY(!entries.isEmpty());
    QVERIFY(SnapTray::HistoryStore::deleteEntry(entries.first()));
    emit first->actionRequested(Action::Copy);
    QCOMPARE(copied.pixelColor(0, 0), QColor(Qt::red));
    QCOMPARE(copied.devicePixelRatio(), 1.0);
    auto lateResult = std::exchange(done, {});

    color = Qt::blue;
    manager.startFullScreenCaptureOnScreen(screen);
    QVERIFY(!first || !first->isVisible());
    QTRY_VERIFY(done);
    std::exchange(done, {})(PlatformFeatures::ClipboardCopyResult::Success);
    QPointer<PostCaptureCard> second = manager.m_postCaptureCard;
    QVERIFY(second && second->isVisible());
    lateResult(PlatformFeatures::ClipboardCopyResult::Failed);
    for (auto* status : second->findChildren<QLabel*>())
        QVERIFY(status->text().isEmpty());
    emit second->actionRequested(Action::Copy);
    QCOMPARE(copied.pixelColor(0, 0), QColor(Qt::blue));
    second->close();
    std::exchange(done, {})(PlatformFeatures::ClipboardCopyResult::Failed);
    QTRY_VERIFY(second.isNull());
}

void tst_FullScreenCapture::automaticSaveWithoutCardKeepsOriginalPixels_data()
{
    QTest::addColumn<bool>("primarySave");
    QTest::addColumn<bool>("superseded");
    QTest::newRow("primary-copy") << false << false;
    QTest::newRow("primary-save") << true << false;
    QTest::newRow("superseded-copy") << false << true;
}

void tst_FullScreenCapture::automaticSaveWithoutCardKeepsOriginalPixels()
{
    QFETCH(bool, primarySave);
    QFETCH(bool, superseded);
    auto& prefs = RegionCaptureSettingsManager::instance();
    prefs.setPostCaptureSave(true);
    prefs.setPostCaptureCopy(true);
    prefs.setFullScreenCaptureAction(primarySave ? FullScreenCaptureAction::Save : FullScreenCaptureAction::CopyToClipboard);
    FileSettingsManager::instance().saveAutoSaveScreenshots(false);
    CaptureManager manager(nullptr);
    configure(manager, nullptr);
    if (superseded) {
        manager.m_clipboardWriterOverride = [](const QImage&, PlatformFeatures::ClipboardCopyCompletion done) {
            done(PlatformFeatures::ClipboardCopyResult::Superseded);
        };
    }
    QSignalSpy saved(&manager, &CaptureManager::saveCompleted);
    QSignalSpy copied(&manager, &CaptureManager::copyCompleted);
    auto* screen = screenNamed("Left200");
    manager.startFullScreenCaptureOnScreen(screen);
    QTRY_COMPARE(saved.size(), 1);
    QCOMPARE(copied.size(), superseded ? 0 : 1);
    QVERIFY(!manager.m_postCaptureCard);
    verifyNativeImage(QImage(saved.first().at(1).toString()), *testScreenFor(screen));
}

void tst_FullScreenCapture::lateExportAfterDismissalKeepsCardHidden_data()
{
    QTest::addColumn<bool>("region");
    QTest::newRow("pending-full-screen-copy") << false;
    QTest::newRow("pending-region-save") << true;
}

void tst_FullScreenCapture::lateExportAfterDismissalKeepsCardHidden()
{
    QFETCH(bool, region);
    auto& prefs = RegionCaptureSettingsManager::instance();
    prefs.setPostCaptureCard(true);
    auto* screen = screenNamed("Primary100");
    CaptureManager manager(nullptr);
    std::optional<QImage> copied;
    configure(manager, &copied);
    if (region) {
#ifdef Q_OS_MACOS
        QSKIP("RegionSelector requires the native Cocoa platform.");
#endif
        prefs.setPostCaptureCopy(true);
        const QPixmap sample = nativeSnapshot(screen);
        const QRect selection(10, 10, 120, 80);
        manager.startPresetRegionCapture(screen, selection, sample);
        QPointer<RegionSelector> selector = manager.m_regionSelector;
        QVERIFY(selector);
        QSignalSpy saved(&manager, &CaptureManager::saveCompleted);
        RegionSelectorTestAccess::saveToFile(*selector);
        selector->hide(); // A manual save leaves its selector hidden during the write.
        manager.startRegionCaptureWithPreset(QRect(200, 100, 50, 50), screen);
        QVERIFY(manager.m_regionSelector && manager.m_regionSelector != selector);
        manager.cancelCapture();
        QTRY_COMPARE(saved.size(), 1);
        QVERIFY(copied);
        QCOMPARE(copied->size(), selection.size());
        QCOMPARE(copied->pixelColor(selection.width() / 2, selection.height() / 2), kFill);
        QCOMPARE(copied->pixelColor(0, 0), QImage(saved.first().at(1).toString()).pixelColor(0, 0));
    } else {
        prefs.setPostCaptureSave(true);
        prefs.setFullScreenCaptureAction(FullScreenCaptureAction::CopyToClipboard);
        PlatformFeatures::ClipboardCopyCompletion complete;
        manager.m_clipboardWriterOverride = [&](const QImage&, auto callback) {
            complete = std::move(callback);
        };
        QSignalSpy saved(&manager, &CaptureManager::saveCompleted);
        manager.startFullScreenCaptureOnScreen(screen);
        QTRY_VERIFY(complete);
        manager.dismissPostCaptureCard();
        complete(PlatformFeatures::ClipboardCopyResult::Success);
        QTRY_COMPARE(saved.size(), 1);
        verifyNativeImage(QImage(saved.first().at(1).toString()), *testScreenFor(screen));
    }
    QVERIFY(!manager.m_postCaptureCard);
}

void tst_FullScreenCapture::cardPausesAndRetriesFailedSave()
{
    using Action = PostCaptureCard::Action;
    auto& prefs = RegionCaptureSettingsManager::instance();
    prefs.setPostCaptureCard(true);
    prefs.setFullScreenCaptureAction(FullScreenCaptureAction::CopyToClipboard);
    CaptureManager manager(nullptr);
    configure(manager, nullptr);
    auto* screen = screenNamed("Left200");
    manager.startFullScreenCaptureOnScreen(screen);
    QTRY_VERIFY(manager.m_postCaptureCard);
    QPointer<PostCaptureCard> card = manager.m_postCaptureCard;
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(card, &leave);
    auto& timer = card->m_timer;
    timer.start(800);
    QTest::qSleep(80);
    QEnterEvent enter(QPointF(1,1), QPointF(1,1), QPointF(card->pos()));
    QApplication::sendEvent(card, &enter);
    QVERIFY(!timer.isActive());
    const int paused = card->m_remainingMs;
    QVERIFY(paused > 0 && paused < 800);
    QTest::mousePress(card, Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
    QApplication::sendEvent(card, &leave);
    QVERIFY(!timer.isActive());
    card->m_menu->popup(card->pos());
    QTest::mouseRelease(card, Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
    QVERIFY(!timer.isActive());
    card->m_menu->hide();
    QApplication::sendEvent(card, &leave);
    QVERIFY(timer.isActive());
    QVERIFY(timer.remainingTime() <= paused);

    QFile obstruction(m_saveDirectory.filePath("not-a-directory"));
    QVERIFY(obstruction.open(QIODevice::WriteOnly));
    obstruction.close();
    FileSettingsManager::instance().saveScreenshotPath(obstruction.fileName());
    QSignalSpy failed(&manager, &CaptureManager::saveFailed);
    QSignalSpy saved(&manager, &CaptureManager::saveCompleted);
    emit card->actionRequested(Action::Save);
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(saved.count(), 0);
    QVERIFY(card->isVisible());
    QVERIFY(!timer.isActive());

    // Cancelling a manual retry must keep the failed save and its retry
    // button available. Only a successful save clears this error.
    const QString originalError = card->m_errors.value(Action::Save);
    QVERIFY(!originalError.isEmpty());
    FileSettingsManager::instance().saveAutoSaveScreenshots(false);
    const bool previousDialogs = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    auto restoreDialogs = qScopeGuard([=] {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previousDialogs);
    });
    QTimer::singleShot(0, [] {
        if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget()))
            dialog->reject();
    });
    emit card->actionRequested(Action::Save);
    QCOMPARE(saved.count(), 0);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(card->m_errors.value(Action::Save), originalError);
    QVERIFY(!timer.isActive());
    FileSettingsManager::instance().saveAutoSaveScreenshots(true);
    FileSettingsManager::instance().saveScreenshotPath(m_saveDirectory.path());
    emit card->actionRequested(Action::Save);
    QTRY_COMPARE(saved.count(), 1);
    verifyNativeImage(QImage(saved.first().at(1).toString()), *testScreenFor(screen));
    QVERIFY(card->m_errors.isEmpty());
}

void tst_FullScreenCapture::nativeCardFocusEscapeAndCaptureExclusion()
{
    if (QGuiApplication::platformName() != QStringLiteral("xcb"))
        QSKIP("This check uses a native X11 desktop");

    QScreen* screen = QGuiApplication::primaryScreen();
    QWidget backdrop(nullptr, Qt::Tool | Qt::FramelessWindowHint);
    const QColor background(24, 72, 112);
    QPalette palette;
    palette.setColor(QPalette::Window, background);
    backdrop.setPalette(palette);
    backdrop.setAutoFillBackground(true);
    const QSize size(500, 280);
    backdrop.setGeometry(QRect(screen->availableGeometry().bottomRight() - QPoint(size.width(), size.height()), size));
    backdrop.show();
    QVERIFY(QTest::qWaitForWindowExposed(&backdrop));
    backdrop.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&backdrop));

    CaptureManager manager(nullptr);
    RegionCaptureSettingsManager::instance().setPostCaptureCard(true);
    QPixmap sample(80, 60);
    sample.fill(Qt::red);
    auto showCard = [&] {
        manager.afterScreenshot(sample, ScreenshotOrigin{QStringLiteral("native-card")}, screen, CaptureManager::PrimaryAction::Pin, manager.m_postCaptureGeneration);
    };
    showCard();
    QPointer<PostCaptureCard> first = manager.m_postCaptureCard;
    QVERIFY(first && QTest::qWaitForWindowExposed(first));
    QCOMPARE(QApplication::activeWindow(), &backdrop);
    first->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(first));
    QTest::keyClick(first, Qt::Key_Escape);
    QTRY_VERIFY(!first || !first->isVisible());

    showCard();
    QPointer<PostCaptureCard> second = manager.m_postCaptureCard;
    QVERIFY(second && QTest::qWaitForWindowExposed(second));
    QLabel* preview = second->findChild<QLabel*>();
    const QPoint previewCenter = preview->rect().center();
    QTRY_COMPARE(preview->grab().toImage().pixelColor(previewCenter * preview->devicePixelRatioF()), QColor(Qt::red));
    const QPoint point = preview->mapToGlobal(previewCenter);
    QVERIFY(backdrop.geometry().contains(point));
    const QPoint local = point - screen->geometry().topLeft();
    auto readPixel = [&] {
        return screen->grabWindow(0, local.x(), local.y(), 1, 1).toImage().pixelColor(0, 0);
    };
    // X11 compositors may already omit translucent windows from root grabs.
    // An uncomposited Xvfb root includes the visible red thumbnail.
    QTRY_VERIFY(readPixel() == QColor(Qt::red) || readPixel() == background);
    QColor captured;
    manager.m_snapshotOverride = [&](QScreen*) {
        captured = readPixel();
        return sample; // only the synthetic pixmap goes to the selector
    };
    manager.startRegionCapture();
    QCOMPARE(captured, background);
    manager.cancelCapture();
    QVERIFY(!second || !second->isVisible());
}

int main(int argc, char** argv)
{
    // Emulate a mixed-DPI multi-monitor desktop before QApplication starts.
    QTemporaryDir configDirectory;
    if (!configDirectory.isValid()) {
        qFatal("Failed to create offscreen screen configuration directory");
    }
    const QString configPath = configDirectory.filePath(QStringLiteral("screens.json"));
    QFile configFile(configPath);
    if (!configFile.open(QIODevice::WriteOnly) || configFile.write(offscreenConfigJson()) < 0) {
        qFatal("Failed to write offscreen screen configuration");
    }
    configFile.close();

    // Platform arguments are split on ':', which would cut a Windows drive
    // letter off an absolute path. Use a relative path, resolved against the
    // working directory while the platform plugin initializes.
    const QString previousDirectory = QDir::currentPath();
    QDir::setCurrent(configDirectory.path());
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen:configfile=screens.json"));

    QApplication app(argc, argv);
    QDir::setCurrent(previousDirectory);
    tst_FullScreenCapture test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "tst_FullScreenCapture.moc"
