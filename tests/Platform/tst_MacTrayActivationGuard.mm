#include "platform/MacTrayActivationGuard.h"

#include <QMenu>
#include <QSignalSpy>
#include <QSystemTrayIcon>
#include <QtTest>

#import <AppKit/AppKit.h>
#import <objc/runtime.h>

namespace {

NSEvent* currentTestEvent = nil;

NSEvent* testCurrentEvent(id, SEL)
{
    return currentTestEvent;
}

class ScopedCurrentEvent
{
public:
    explicit ScopedCurrentEvent(NSEvent* event)
    {
        currentTestEvent = event;
        method = class_getInstanceMethod([NSApp class], @selector(currentEvent));
        original = method_setImplementation(method, reinterpret_cast<IMP>(testCurrentEvent));
    }
    ~ScopedCurrentEvent()
    {
        method_setImplementation(method, original);
        currentTestEvent = nil;
    }

private:
    Method method;
    IMP original;
};

} // namespace

class tst_MacTrayActivationGuard : public QObject
{
    Q_OBJECT

private slots:
    void menuTracking_data();
    void menuTracking();
};

void tst_MacTrayActivationGuard::menuTracking_data()
{
    QTest::addColumn<int>("type");
    QTest::addColumn<int>("clicks");
    QTest::addColumn<int>("activation");
    QTest::newRow("kit-defined") << int(NSEventTypeAppKitDefined) << 0 << -1;
    QTest::newRow("application-defined") << int(NSEventTypeApplicationDefined) << 0 << -1;
    QTest::newRow("no-current-event") << -1 << 0 << -1;
    QTest::newRow("left-click") << int(NSEventTypeLeftMouseDown) << 1 << int(QSystemTrayIcon::Trigger);
    QTest::newRow("right-click") << int(NSEventTypeRightMouseDown) << 1 << int(QSystemTrayIcon::Context);
    QTest::newRow("double-click") << int(NSEventTypeLeftMouseDown) << 2 << int(QSystemTrayIcon::DoubleClick);
}

void tst_MacTrayActivationGuard::menuTracking()
{
    if (NSProcessInfo.processInfo.operatingSystemVersion.majorVersion < 27) {
        QSKIP("Compatibility guard applies to macOS 27 and later");
    }
    QFETCH(int, type);
    QFETCH(int, clicks);
    QFETCH(int, activation);

    QVERIFY(SnapTray::installMacTrayActivationGuard());
    QVERIFY(SnapTray::installMacTrayActivationGuard()); // Repeated application setup is safe.
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::black);
    QMenu menu;
    QAction* action = menu.addAction(QStringLiteral("Test action"));
    QSignalSpy triggered(action, &QAction::triggered);
    QSystemTrayIcon tray{QIcon(pixmap)};
    tray.setContextMenu(&menu);
    tray.show();
    QSignalSpy activated(&tray, &QSystemTrayIcon::activated);
    NSMenu* nativeMenu = menu.toNSMenu();
    QVERIFY(nativeMenu);

    @autoreleasepool {
        NSEvent* event = nil;
        if (type >= 0 && clicks == 0) {
            event = [NSEvent otherEventWithType:static_cast<NSEventType>(type)
                location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:0
                context:nil subtype:2 data1:0 data2:64];
        } else if (type >= 0) {
            event = [NSEvent mouseEventWithType:static_cast<NSEventType>(type)
                location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:0
                context:nil eventNumber:0 clickCount:clicks pressure:1.0];
        }
        ScopedCurrentEvent scopedEvent(event);
        QString exception;
        @try {
            // Exercise the real Cocoa plugin observer, exactly as native menu
            // tracking does; the unpatched Qt path throws for KitDefined.
            [NSNotificationCenter.defaultCenter
                postNotificationName:NSMenuDidBeginTrackingNotification object:nativeMenu];
        } @catch (NSException* caught) {
            exception = QString::fromNSString(caught.reason);
        }
        QVERIFY2(exception.isEmpty(), qPrintable(exception));
    }

    QCOMPARE(activated.count(), activation < 0 ? 0 : 1);
    if (activation >= 0) {
        QCOMPARE(activated.first().first().value<QSystemTrayIcon::ActivationReason>(),
                 static_cast<QSystemTrayIcon::ActivationReason>(activation));
    }
    // Verify the existing native menu action routing still reaches QAction.
    [nativeMenu performActionForItemAtIndex:0];
    QTRY_COMPARE(triggered.count(), 1);
    tray.hide();
}

QTEST_MAIN(tst_MacTrayActivationGuard)
#include "tst_MacTrayActivationGuard.moc"
