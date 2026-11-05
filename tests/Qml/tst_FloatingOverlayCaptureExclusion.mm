#include <QtTest/QtTest>

#import <Cocoa/Cocoa.h>

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

// PR #241 review: the pin hover bar and value panel are never captured.
void tst_FloatingOverlayCaptureExclusion::testPinHoverWindowsExcludedFromCapture()
{
    PinHoverBarViewModel viewModel;
    PinHoverBar bar(&viewModel, nullptr);

    const QList<WId> ids = bar.captureExclusionWindowIds();
    QCOMPARE(ids.size(), 2);
    for (QWindow* w : {bar.barWindow(), bar.panelWindow()}) {
        NSWindow* window = nsWindowForQWindow(w);
        QVERIFY(window != nil);
        QCOMPARE((NSInteger)[window sharingType], (NSInteger)NSWindowSharingNone);
    }
}

QTEST_MAIN(tst_FloatingOverlayCaptureExclusion)
#include "tst_FloatingOverlayCaptureExclusion.moc"
