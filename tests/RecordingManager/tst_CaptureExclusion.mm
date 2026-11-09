#include <QtTest>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>
#include <QScopeGuard>
#include <QtQml/qqmlextensionplugin.h>
#include "qml/QmlRecordingControlBar.h"
#include "capture/SCKCaptureEngine.h"
#import <Cocoa/Cocoa.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#include <atomic>
#include <memory>

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

class TestRecordingCaptureExclusion : public QObject
{
    Q_OBJECT
private slots:
    void preparedTooltipIsDiscoverableAndStable();
    void sckFramesExcludeLateTooltip();
};

void TestRecordingCaptureExclusion::preparedTooltipIsDiscoverableAndStable()
{
    if (QGuiApplication::platformName() != "cocoa") QSKIP("Requires native Cocoa windows.");
    SnapTray::QmlRecordingControlBar bar;
    bar.show();
    const auto ids = bar.prepareCaptureExclusions();
    QCOMPARE(ids.size(), 2);
    QVERIFY(ids[0] != ids[1]);
    QVERIFY(!bar.m_tooltipRootItem->isVisible());
    QVERIFY(bar.m_tooltipView->isVisible());
    if (@available(macOS 14.4, *)) {
        struct Result {
            SCShareableContent* content = nil;
            NSError* error = nil;
            std::atomic<bool> done{false};
        };
        const auto result = std::make_shared<Result>();
        [SCShareableContent getCurrentProcessShareableContentWithCompletionHandler:
            ^(SCShareableContent* content, NSError* error) {
                result->content = content;
                result->error = error;
                result->done = true;
            }];
        QTRY_VERIFY_WITH_TIMEOUT(result->done.load(), 10000);
        QVERIFY(result->error == nil);
        QSet<quintptr> discoverable;
        for (SCWindow* window in result->content.windows) discoverable.insert(window.windowID);
        for (quintptr id : ids) QVERIFY2(discoverable.contains(id), "Prepared native window missing from SCK content");
    }
    for (int i = 0; i < 3; ++i) {
        bar.showTooltip("Stop recording", bar.m_view->geometry());
        QTRY_VERIFY(bar.m_tooltipRootItem->isVisible());
        QCOMPARE(bar.prepareCaptureExclusions(), ids);
        bar.hideTooltip();
        QVERIFY(!bar.m_tooltipRootItem->isVisible());
        QCOMPARE(bar.prepareCaptureExclusions(), ids);
    }
    bar.showTooltip("Queued tooltip", bar.m_view->geometry());
    bar.close();
    QCoreApplication::processEvents();
    QVERIFY(!bar.m_tooltipView);
    bar.show();
    QCOMPARE(bar.prepareCaptureExclusions().size(), 2);
    bar.close();
}

void TestRecordingCaptureExclusion::sckFramesExcludeLateTooltip()
{
    if (QGuiApplication::platformName() != "cocoa") QSKIP("Requires native Cocoa windows.");
    if (!SCKCaptureEngine::hasScreenRecordingPermission()) QSKIP("Screen Recording permission unavailable; do not request TCC in tests.");
    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);
    const QRect available = screen->availableGeometry();
    if (available.width() < 700 || available.height() < 360) QSKIP("Display too small for controlled capture fixture.");
    QWidget background;
    background.setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    background.setAttribute(Qt::WA_ShowWithoutActivating);
    background.setAutoFillBackground(true);
    QPalette palette = background.palette();
    palette.setColor(QPalette::Window, QColor(47, 123, 177));
    background.setPalette(palette);
    background.setGeometry(QRect(available.topLeft() + QPoint(20, 20), QSize(660, 280)));
    background.show();
    background.raise();
    QTest::qWait(100);
    SnapTray::QmlRecordingControlBar bar;
    bar.show();
    bar.m_view->setPosition(background.pos() + QPoint(40, 60));
    const auto ids = bar.prepareCaptureExclusions();
    QCOMPARE(ids.size(), 2);
    SCKCaptureEngine capture;
    capture.setFrameRate(30);
    QVERIFY(capture.setRegion(background.geometry(), CaptureScreenInfo::fromScreen(screen)));
    capture.setExcludedCaptureWindowIds(ids);
    QSignalSpy errors(&capture, &ICaptureEngine::error);
    const auto stop = qScopeGuard([&] { capture.stop(); });
    QVERIFY2(capture.start(), errors.isEmpty() ? "SCK start failed" : qPrintable(errors.last()[0].toString()));
    QTRY_VERIFY_WITH_TIMEOUT(!capture.captureFrame().isNull(), 5000);
    QTest::qWait(100);
    const QImage reference = capture.captureFrame().copy();
    const QColor backgroundColor = reference.pixelColor(reference.width() - 20, reference.height() - 20);
    for (int y = 20; y < reference.height() - 20; y += 10) {
        for (int x = 20; x < reference.width() - 20; x += 10) {
            QCOMPARE(reference.pixelColor(x, y), backgroundColor);
        }
    }
    bar.showTooltip("Stop recording", bar.m_view->geometry());
    QTRY_VERIFY(bar.m_tooltipRootItem->isVisible());
    for (int i = 0; i < 8; ++i) {
        QTest::qWait(50);
        const QImage frame = capture.captureFrame();
        QCOMPARE(frame, reference);
    }
    bar.hideTooltip();
    bar.close();
}

QTEST_MAIN(TestRecordingCaptureExclusion)
#include "tst_CaptureExclusion.moc"
