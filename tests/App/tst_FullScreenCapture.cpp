#include <QtTest/QtTest>

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
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
#include "../RegionSelector/RegionSelectorTestAccess.h"
#include "capture/OwnWindowCaptureHider.h"
#include "history/HistoryRecorder.h"
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
    void windowShownBeforeImmediateGrab_isHiddenBeforeGrab();
    void windowsKeepAppearing_cancelsInsteadOfGrabbing();

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
        [copiedImage](const QImage& image, std::function<void(bool)> done) {
            if (copiedImage) {
                *copiedImage = image;
            }
            done(true);
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
    QFETCH(QString, screenName);
    RegionCaptureSettingsManager::instance().setFullScreenCaptureAction(
        FullScreenCaptureAction::Edit);

    CaptureManager manager(nullptr);
    configure(manager, nullptr);

    QScreen* screen = screenNamed(screenName);
    QVERIFY(screen);
    manager.startFullScreenCaptureOnScreen(screen);

    QTRY_VERIFY(manager.m_regionSelector);
    QPointer<RegionSelector> selector = manager.m_regionSelector;
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
    std::function<void(bool)> finishClipboardWrite;
    manager.m_clipboardWriterOverride =
        [&finishClipboardWrite](const QImage&, std::function<void(bool)> done) {
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

    finishClipboardWrite(true);
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

void tst_FullScreenCapture::windowShownBeforeImmediateGrab_isHiddenBeforeGrab()
{
    RegionCaptureSettingsManager::instance().setFullScreenCaptureAction(
        FullScreenCaptureAction::CopyToClipboard);

    CaptureManager manager(nullptr);
    configure(manager, nullptr);
    QWidget toast;  // nothing else visible, so the grab is not delayed by default
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
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen:configfile=") + configPath.toLocal8Bit());

    QApplication app(argc, argv);
    tst_FullScreenCapture test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "tst_FullScreenCapture.moc"
