#include "platform/MacDockPresence.h"
#include "update/SparkleReadyToInstallRaise.h"

#include <QStringList>
#include <QtTest>

#import <AppKit/AppKit.h>

// The test binary never loads Sparkle.framework, so these stand-ins can use the
// class names SnapTray looks up at runtime.
namespace {
QStringList userDriverCalls;
}

@interface SPUStandardUserDriver : NSObject
- (void)showReadyToInstallAndRelaunch:(void (^)(NSInteger))installUpdateHandler;
- (void)showUpdateInFocus;
@end

@implementation SPUStandardUserDriver

- (void)showReadyToInstallAndRelaunch:(void (^)(NSInteger))installUpdateHandler
{
    (void)installUpdateHandler;
    userDriverCalls << QStringLiteral("showReadyToInstallAndRelaunch");
}

- (void)showUpdateInFocus
{
    userDriverCalls << QStringLiteral("showUpdateInFocus");
}

@end

@interface SUStatusController : NSWindowController
@end

@implementation SUStatusController
@end

@interface FakeSparkleUpdateState : NSObject
@property (nonatomic) BOOL userInitiated;
@end

@implementation FakeSparkleUpdateState
@end

@protocol SparkleUserDriverDelegateForTest <NSObject>
- (BOOL)supportsGentleScheduledUpdateReminders;
- (void)standardUserDriverWillHandleShowingUpdate:(BOOL)handleShowingUpdate
                                        forUpdate:(id)update
                                            state:(id)state;
- (void)standardUserDriverDidReceiveUserAttentionForUpdate:(id)update;
- (void)standardUserDriverWillFinishUpdateSession;
@end

namespace {

constexpr int kRegular = static_cast<int>(NSApplicationActivationPolicyRegular);
constexpr int kAccessory = static_cast<int>(NSApplicationActivationPolicyAccessory);

int activationPolicy()
{
    return static_cast<int>(NSApp.activationPolicy);
}

QString dockBadge()
{
    return QString::fromNSString(NSApp.dockTile.badgeLabel);
}

id<SparkleUserDriverDelegateForTest> makeDelegate()
{
    Class delegateClass = NSClassFromString(@"SnapTraySparkleUpdaterDelegate");
    return [[[delegateClass alloc] init] autorelease];
}

FakeSparkleUpdateState* makeState(bool userInitiated)
{
    FakeSparkleUpdateState* state = [[[FakeSparkleUpdateState alloc] init] autorelease];
    state.userInitiated = userInitiated ? YES : NO;
    return state;
}

bool implementsUserDriverDelegate(id delegate)
{
    return [delegate respondsToSelector:@selector(supportsGentleScheduledUpdateReminders)]
        && [delegate respondsToSelector:@selector(standardUserDriverWillHandleShowingUpdate:forUpdate:state:)]
        && [delegate respondsToSelector:@selector(standardUserDriverDidReceiveUserAttentionForUpdate:)]
        && [delegate respondsToSelector:@selector(standardUserDriverWillFinishUpdateSession)];
}

} // namespace

class tst_SparkleGentleReminders : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void declaresGentleReminderSupport();
    void scheduledUpdateKeepsAppInDockWithBadge();
    void userAttentionClearsBadge();
    void userInitiatedUpdateSkipsBadge();
    void repeatedShowingHoldsOneDockClaim();
    void sessionEndKeepsDockWhileSettingsHoldsIt();
    void releasingDelegateEndsDockPresence();
    void readyToInstallBringsStatusWindowForward();
};

void tst_SparkleGentleReminders::init()
{
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    NSApp.dockTile.badgeLabel = nil;
    userDriverCalls.clear();
}

void tst_SparkleGentleReminders::declaresGentleReminderSupport()
{
    @autoreleasepool {
        id<SparkleUserDriverDelegateForTest> delegate = makeDelegate();
        QVERIFY(delegate != nil);
        QVERIFY(implementsUserDriverDelegate(delegate));
        QVERIFY([delegate supportsGentleScheduledUpdateReminders]);
    }
}

void tst_SparkleGentleReminders::scheduledUpdateKeepsAppInDockWithBadge()
{
    @autoreleasepool {
        id<SparkleUserDriverDelegateForTest> delegate = makeDelegate();
        QVERIFY(implementsUserDriverDelegate(delegate));

        [delegate standardUserDriverWillHandleShowingUpdate:YES forUpdate:nil state:makeState(false)];
        QCOMPARE(activationPolicy(), kRegular);
        QCOMPARE(dockBadge(), QStringLiteral("1"));

        [delegate standardUserDriverWillFinishUpdateSession];
        QCOMPARE(activationPolicy(), kAccessory);
        QVERIFY(dockBadge().isEmpty());
    }
}

void tst_SparkleGentleReminders::userAttentionClearsBadge()
{
    @autoreleasepool {
        id<SparkleUserDriverDelegateForTest> delegate = makeDelegate();
        QVERIFY(implementsUserDriverDelegate(delegate));

        [delegate standardUserDriverWillHandleShowingUpdate:YES forUpdate:nil state:makeState(false)];
        [delegate standardUserDriverDidReceiveUserAttentionForUpdate:nil];
        QVERIFY(dockBadge().isEmpty());
        // The update session is still open, so the app stays reachable from the Dock.
        QCOMPARE(activationPolicy(), kRegular);

        [delegate standardUserDriverWillFinishUpdateSession];
        QCOMPARE(activationPolicy(), kAccessory);
    }
}

void tst_SparkleGentleReminders::userInitiatedUpdateSkipsBadge()
{
    @autoreleasepool {
        id<SparkleUserDriverDelegateForTest> delegate = makeDelegate();
        QVERIFY(implementsUserDriverDelegate(delegate));

        [delegate standardUserDriverWillHandleShowingUpdate:YES forUpdate:nil state:makeState(true)];
        QCOMPARE(activationPolicy(), kRegular);
        QVERIFY(dockBadge().isEmpty());

        [delegate standardUserDriverWillFinishUpdateSession];
        QCOMPARE(activationPolicy(), kAccessory);
    }
}

void tst_SparkleGentleReminders::repeatedShowingHoldsOneDockClaim()
{
    @autoreleasepool {
        id<SparkleUserDriverDelegateForTest> delegate = makeDelegate();
        QVERIFY(implementsUserDriverDelegate(delegate));

        [delegate standardUserDriverWillHandleShowingUpdate:YES forUpdate:nil state:makeState(false)];
        [delegate standardUserDriverWillHandleShowingUpdate:YES forUpdate:nil state:makeState(false)];
        [delegate standardUserDriverWillFinishUpdateSession];
        QCOMPARE(activationPolicy(), kAccessory);

        // Sparkle can also end sessions that never showed an update.
        [delegate standardUserDriverWillFinishUpdateSession];
        QCOMPARE(activationPolicy(), kAccessory);
    }
}

void tst_SparkleGentleReminders::sessionEndKeepsDockWhileSettingsHoldsIt()
{
    @autoreleasepool {
        id<SparkleUserDriverDelegateForTest> delegate = makeDelegate();
        QVERIFY(implementsUserDriverDelegate(delegate));

        {
            SnapTray::DockPresenceClaim settings;
            [delegate standardUserDriverWillHandleShowingUpdate:YES forUpdate:nil state:makeState(false)];
            [delegate standardUserDriverWillFinishUpdateSession];
            QCOMPARE(activationPolicy(), kRegular);
        }
        QCOMPARE(activationPolicy(), kAccessory);
    }
}

void tst_SparkleGentleReminders::releasingDelegateEndsDockPresence()
{
    @autoreleasepool {
        Class delegateClass = NSClassFromString(@"SnapTraySparkleUpdaterDelegate");
        id<SparkleUserDriverDelegateForTest> delegate = [[delegateClass alloc] init];
        QVERIFY(implementsUserDriverDelegate(delegate));

        [delegate standardUserDriverWillHandleShowingUpdate:YES forUpdate:nil state:makeState(false)];
        QCOMPARE(activationPolicy(), kRegular);

        // The update service can be torn down while a session is still open.
        [delegate release];
    }
    QCOMPARE(activationPolicy(), kAccessory);
}

void tst_SparkleGentleReminders::readyToInstallBringsStatusWindowForward()
{
    QVERIFY(installSparkleReadyToInstallRaise());
    QVERIFY(installSparkleReadyToInstallRaise()); // Must not wrap the method twice.

    @autoreleasepool {
        NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 240, 120)
                                                       styleMask:NSWindowStyleMaskTitled
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        window.releasedWhenClosed = NO;
        SUStatusController* statusController = [[SUStatusController alloc] initWithWindow:window];
        [window release];
        QVERIFY(!statusController.window.visible);

        SPUStandardUserDriver* userDriver = [[[SPUStandardUserDriver alloc] init] autorelease];
        [userDriver showReadyToInstallAndRelaunch:^(NSInteger) {}];

        // showUpdateInFocus would make "Install and Relaunch" the key window's default
        // button, so a Return meant for another app or a SnapTray capture relaunches.
        QCOMPARE(userDriverCalls, QStringList{QStringLiteral("showReadyToInstallAndRelaunch")});
        QVERIFY(statusController.window.visible);
        QVERIFY(!statusController.window.keyWindow);

        [statusController.window orderOut:nil];
        [statusController release];
    }
}

QTEST_MAIN(tst_SparkleGentleReminders)
#include "tst_SparkleGentleReminders.moc"
