#include <QtTest/QtTest>

#import <Cocoa/Cocoa.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <atomic>
#include <memory>

#include "pinwindow/PinHoverBar.h"
#include "qml/CanvasToolbarViewModel.h"
#include "qml/PinHoverBarViewModel.h"
#include "qml/PinToolOptionsViewModel.h"
#include "qml/QmlEmojiPickerPopup.h"
#include "qml/QmlFloatingSubToolbar.h"
#include "qml/QmlFloatingToolbar.h"

namespace {

NSWindow* nsWindowForQWindow(QWindow* window)
{
    if (!window) {
        return nil;
    }

    NSView* view = reinterpret_cast<NSView*>(window->winId());
    if (!view) {
        return nil;
    }

    return [view window];
}

} // namespace

class tst_FloatingOverlayCaptureExclusion : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testToolbarWindowExcludedFromCapture();
    void testSubToolbarWindowExcludedFromCapture();
    void testEmojiPickerWindowExcludedFromCapture();
    void testPinHoverWindowsExcludedFromCapture();
    void testPinTooltipDiscoverableBeforeHover();
    void testPinHoverWindowsStayOnDeactivate();
};

void tst_FloatingOverlayCaptureExclusion::initTestCase()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("No screens available for floating overlay capture exclusion tests.");
    }
}

void tst_FloatingOverlayCaptureExclusion::testToolbarWindowExcludedFromCapture()
{
    CanvasToolbarViewModel viewModel;
    SnapTray::QmlFloatingToolbar toolbar(&viewModel);

    toolbar.show();
    QTRY_VERIFY(toolbar.isVisible());

    NSWindow* window = nsWindowForQWindow(toolbar.window());
    QVERIFY(window != nil);
    QCOMPARE((NSInteger)[window sharingType], (NSInteger)NSWindowSharingNone);
}

void tst_FloatingOverlayCaptureExclusion::testSubToolbarWindowExcludedFromCapture()
{
    PinToolOptionsViewModel viewModel;
    SnapTray::QmlFloatingSubToolbar subToolbar(&viewModel);

    subToolbar.show();
    QTRY_VERIFY(subToolbar.isVisible());

    NSWindow* window = nsWindowForQWindow(subToolbar.window());
    QVERIFY(window != nil);
    QCOMPARE((NSInteger)[window sharingType], (NSInteger)NSWindowSharingNone);
}

void tst_FloatingOverlayCaptureExclusion::testEmojiPickerWindowExcludedFromCapture()
{
    SnapTray::QmlEmojiPickerPopup popup;

    popup.showAt(QRect(40, 40, 10, 10));
    QTRY_VERIFY(popup.isVisible());

    NSWindow* window = nsWindowForQWindow(popup.window());
    QVERIFY(window != nil);
    QCOMPARE((NSInteger)[window sharingType], (NSInteger)NSWindowSharingNone);
}

// PR #241 review: the pin hover bar, value panel and tooltip are never captured.
void tst_FloatingOverlayCaptureExclusion::testPinHoverWindowsExcludedFromCapture()
{
    PinHoverBarViewModel viewModel;
    PinHoverBar bar(&viewModel, nullptr);

    const QList<WId> ids = bar.captureExclusionWindowIds();
    QCOMPARE(ids.size(), 3);
    for (QWindow* w : {bar.barWindow(), bar.panelWindow(), bar.tooltipWindow()}) {
        NSWindow* window = nsWindowForQWindow(w);
        QVERIFY(window != nil);
        QCOMPARE((NSInteger)[window sharingType], (NSInteger)NSWindowSharingNone);
    }
}

void tst_FloatingOverlayCaptureExclusion::testPinTooltipDiscoverableBeforeHover()
{
    if (QGuiApplication::platformName() != "cocoa") QSKIP("Requires native Cocoa windows.");
    if (@available(macOS 14.4, *)) {
        PinHoverBarViewModel viewModel;
        PinHoverBar bar(&viewModel, nullptr);
        const QList<WId> ids = bar.captureExclusionWindowIds();
        QCOMPARE(ids.size(), 3);
        QVERIFY(bar.tooltipWindow());
        NSWindow* tooltip = nsWindowForQWindow(bar.tooltipWindow());
        QVERIFY(tooltip != nil);
        QVERIFY([tooltip windowNumber] > 0);
        QVERIFY(![tooltip hasShadow]);
        QVERIFY([tooltip ignoresMouseEvents]);

        struct Result {
            QSet<quintptr> windowIds;
            QString error;
            std::atomic<bool> done{false};
        };
        const auto result = std::make_shared<Result>();
        [SCShareableContent getCurrentProcessShareableContentWithCompletionHandler:
            ^(SCShareableContent* content, NSError* error) {
                if (error) result->error = QString::fromNSString(error.localizedDescription);
                for (SCWindow* window in content.windows) result->windowIds.insert(window.windowID);
                result->done = true;
            }];
        QTRY_VERIFY_WITH_TIMEOUT(result->done.load(), 10000);
        QVERIFY2(result->error.isEmpty(), qPrintable(result->error));
        QVERIFY2(result->windowIds.contains(static_cast<quintptr>([tooltip windowNumber])),
                 "Tooltip must be discoverable by SCK before the first hover");
    } else {
        QSKIP("Current-process SCK enumeration requires macOS 14.4.");
    }
}

// PR #241 review: Cocoa hides Qt::Tool windows when the app deactivates; the
// pin stays, so its hover bar and panel stay too.
void tst_FloatingOverlayCaptureExclusion::testPinHoverWindowsStayOnDeactivate()
{
    PinHoverBarViewModel viewModel;
    PinHoverBar bar(&viewModel, nullptr);
    const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
    bar.setPinGeometry(QRect(screen.topLeft() + QPoint(200, 200), QSize(480, 320)));
    bar.showNow();
    QVERIFY(bar.isBarVisible());
    viewModel.openPanel(PinHoverBarViewModel::ZoomPanel);
    QVERIFY(bar.isPanelVisible());
    for (QWindow* w : {bar.barWindow(), bar.panelWindow()}) {
        NSWindow* window = nsWindowForQWindow(w);
        QVERIFY(window != nil);
        QVERIFY(![window hidesOnDeactivate]);
    }
}

QTEST_MAIN(tst_FloatingOverlayCaptureExclusion)
#include "tst_FloatingOverlayCaptureExclusion.moc"
