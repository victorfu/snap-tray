#include "platform/MacDockPresence.h"
#include "update/IUpdateService.h"
#include "update/SparkleReadyToInstallRaise.h"
#include "update/UpdateCoordinator.h"
#include "update/UpdateServiceFactory.h"
#include "update/UpdateSettingsManager.h"
#include "update_config.h"
#include "version.h"

#ifdef Q_OS_MACOS

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <objc/message.h>
#import <objc/runtime.h>

#include <QCoreApplication>
#include <QDebug>
#include <QMetaObject>

#include <optional>

static NSString* const kPendingUpdateDockBadge = @"1";

static void notifySuccessfulUpdateCheck()
{
    QMetaObject::invokeMethod(&UpdateCoordinator::instance(),
                              &UpdateCoordinator::recordSuccessfulCheck,
                              Qt::QueuedConnection);
}

static BOOL isUserInitiatedUpdate(id state)
{
    SEL selector = @selector(userInitiated);
    return [state respondsToSelector:selector]
        && ((BOOL(*)(id, SEL))objc_msgSend)(state, selector);
}

@interface SnapTraySparkleUpdaterDelegate : NSObject
@end

@implementation SnapTraySparkleUpdaterDelegate {
    std::optional<SnapTray::DockPresenceClaim> _dockPresence;
}

- (void)updater:(id)updater didFinishLoadingAppcast:(id)appcast
{
    (void)updater;
    (void)appcast;
    notifySuccessfulUpdateCheck();
}

// Sparkle 2.2+ shows scheduled update alerts of a dockless app behind other apps
// and leaves reminders to the app. Follow Sparkle's background-app example: keep
// SnapTray in the Dock, with a badge for scheduled updates, until the session ends.
// https://sparkle-project.org/documentation/gentle-reminders/
- (BOOL)supportsGentleScheduledUpdateReminders
{
    return YES;
}

- (void)standardUserDriverWillHandleShowingUpdate:(BOOL)handleShowingUpdate
                                        forUpdate:(id)update
                                            state:(id)state
{
    (void)handleShowingUpdate;
    (void)update;

    if (!_dockPresence) {
        _dockPresence.emplace();
    }
    if (!isUserInitiatedUpdate(state)) {
        NSApp.dockTile.badgeLabel = kPendingUpdateDockBadge;
    }
}

- (void)standardUserDriverDidReceiveUserAttentionForUpdate:(id)update
{
    (void)update;
    NSApp.dockTile.badgeLabel = nil;
}

- (void)standardUserDriverWillFinishUpdateSession
{
    NSApp.dockTile.badgeLabel = nil;
    _dockPresence.reset();
}

@end

namespace {

class SparkleUpdateService final : public IUpdateService
{
public:
    explicit SparkleUpdateService(InstallSource source)
        : m_installSource(source)
    {
    }

    ~SparkleUpdateService() override
    {
        if (m_controller != nil) {
            [m_controller release];
        }
        if (m_delegate != nil) {
            [m_delegate release];
        }
    }

    UpdateServiceKind kind() const override { return UpdateServiceKind::Sparkle; }
    InstallSource installSource() const override { return m_installSource; }
    bool isExternallyManaged() const override { return false; }
    QString managementMessage() const override { return QString(); }

    bool initialize(QString* errorMessage) override
    {
        if (m_initialized) {
            if (errorMessage) {
                *errorMessage = QString();
            }
            return true;
        }

        if (QStringLiteral(SNAPTRAY_SPARKLE_FEED_URL).trimmed().isEmpty() ||
            QStringLiteral(SNAPTRAY_SPARKLE_PUBLIC_KEY).trimmed().isEmpty()) {
            m_lastError = QCoreApplication::translate(
                "SparkleUpdateService",
                "Sparkle is not configured. Set the Sparkle feed URL and public key at build time.");
            if (errorMessage) {
                *errorMessage = m_lastError;
            }
            return false;
        }

        @autoreleasepool {
            NSBundle* mainBundle = [NSBundle mainBundle];
            NSString* frameworksPath = [mainBundle privateFrameworksPath];
            NSString* sparkleBundlePath =
                [frameworksPath stringByAppendingPathComponent:@"Sparkle.framework"];
            NSBundle* sparkleBundle = [NSBundle bundleWithPath:sparkleBundlePath];
            if (sparkleBundle == nil || ![sparkleBundle load]) {
                m_lastError = QCoreApplication::translate(
                    "SparkleUpdateService",
                    "Sparkle.framework is missing from the application bundle.");
                if (errorMessage) {
                    *errorMessage = m_lastError;
                }
                return false;
            }

            Class controllerClass = NSClassFromString(@"SPUStandardUpdaterController");
            if (controllerClass == Nil) {
                m_lastError = QCoreApplication::translate(
                    "SparkleUpdateService",
                    "Sparkle.framework loaded, but the updater controller class is unavailable.");
                if (errorMessage) {
                    *errorMessage = m_lastError;
                }
                return false;
            }

            SEL allocSel = sel_registerName("alloc");
            SEL initSel = sel_registerName("initWithStartingUpdater:updaterDelegate:userDriverDelegate:");
            m_delegate = [[SnapTraySparkleUpdaterDelegate alloc] init];
            id controllerAlloc = ((id(*)(id, SEL))objc_msgSend)(controllerClass, allocSel);
            m_controller = ((id(*)(id, SEL, BOOL, id, id))objc_msgSend)(
                controllerAlloc, initSel, NO, m_delegate, m_delegate);

            if (m_controller == nil) {
                m_lastError = QCoreApplication::translate(
                    "SparkleUpdateService",
                    "Failed to create the Sparkle updater controller.");
                if (errorMessage) {
                    *errorMessage = m_lastError;
                }
                return false;
            }

            installSparkleReadyToInstallRaise();
            applyUpdaterSettings();
            m_initialized = true;
        }

        if (errorMessage) {
            *errorMessage = QString();
        }
        return true;
    }

    void startAutomaticChecks() override
    {
        QString errorMessage;
        if (!initialize(&errorMessage)) {
            return;
        }

        if (m_autoCheckEnabled) {
            startUpdater();
        }
    }

    void syncSettings(bool autoCheckEnabled, int checkIntervalHours) override
    {
        m_autoCheckEnabled = autoCheckEnabled;
        m_checkIntervalHours = checkIntervalHours;

        if (!m_initialized) {
            return;
        }

        @autoreleasepool {
            applyUpdaterSettings();
        }

        if (autoCheckEnabled && !m_updaterStarted) {
            startUpdater();
        }
    }

    UpdateCheckResult checkForUpdatesInteractive() override
    {
        QString errorMessage;
        if (!initialize(&errorMessage)) {
            return UpdateCheckResult::failed(errorMessage);
        }

        @autoreleasepool {
            if (!m_updaterStarted) {
                startUpdater();
            }

            if ([m_controller respondsToSelector:@selector(checkForUpdates:)]) {
                ((void(*)(id, SEL, id))objc_msgSend)(m_controller,
                                                     @selector(checkForUpdates:),
                                                     nil);
                return UpdateCheckResult::started();
            }

            id updater = updaterObject();
            if (updater != nil && [updater respondsToSelector:@selector(checkForUpdates)]) {
                ((void(*)(id, SEL))objc_msgSend)(updater, @selector(checkForUpdates));
                return UpdateCheckResult::started();
            }
        }

        return UpdateCheckResult::failed(QCoreApplication::translate(
            "SparkleUpdateService",
            "Sparkle is available, but interactive update checks are unsupported."));
    }

private:
    void applyUpdaterSettings()
    {
        id updater = updaterObject();
        if (updater == nil) {
            return;
        }

        setBooleanProperty(updater, "setAutomaticallyChecksForUpdates:", m_autoCheckEnabled);
        setDoubleProperty(updater, "setUpdateCheckInterval:",
                          static_cast<double>(m_checkIntervalHours) * 3600.0);
    }

    id updaterObject() const
    {
        if (m_controller == nil || ![m_controller respondsToSelector:@selector(updater)]) {
            return nil;
        }
        return ((id(*)(id, SEL))objc_msgSend)(m_controller, @selector(updater));
    }

    void startUpdater()
    {
        if (m_updaterStarted) {
            return;
        }

        if (m_controller != nil && [m_controller respondsToSelector:@selector(startUpdater)]) {
            ((void(*)(id, SEL))objc_msgSend)(m_controller, @selector(startUpdater));
            m_updaterStarted = true;
        }
    }

    static void setBooleanProperty(id object, const char* selectorName, bool value)
    {
        SEL selector = sel_registerName(selectorName);
        if (object != nil && [object respondsToSelector:selector]) {
            ((void(*)(id, SEL, BOOL))objc_msgSend)(object, selector, value ? YES : NO);
        }
    }

    static void setDoubleProperty(id object, const char* selectorName, double value)
    {
        SEL selector = sel_registerName(selectorName);
        if (object != nil && [object respondsToSelector:selector]) {
            ((void(*)(id, SEL, double))objc_msgSend)(object, selector, value);
        }
    }

    InstallSource m_installSource;
    bool m_initialized = false;
    bool m_autoCheckEnabled = true;
    int m_checkIntervalHours = UpdateSettingsManager::kDefaultCheckIntervalHours;
    QString m_lastError;
    id m_controller = nil;
    id m_delegate = nil;
    bool m_updaterStarted = false;
};

} // namespace

namespace {

using ShowReadyToInstallFn = void (*)(id, SEL, id);
// self, _cmd, and the install handler block.
constexpr unsigned int kShowReadyToInstallArgumentCount = 3;
ShowReadyToInstallFn originalShowReadyToInstall = nullptr;

void showReadyToInstallInFront(id userDriver, SEL selector, id installUpdateHandler)
{
    originalShowReadyToInstall(userDriver, selector, installUpdateHandler);

    // The user already chose to install, so show "Install and Relaunch" above the app
    // they switched to during the download. Order it in without making it key: its
    // default button would turn a Return meant for another app, or for a SnapTray
    // capture in progress, into an immediate relaunch.
    Class statusControllerClass = NSClassFromString(@"SUStatusController");
    for (NSWindow* window in NSApp.windows) {
        if ([window.windowController isKindOfClass:statusControllerClass]) {
            [window orderFrontRegardless];
        }
    }
}

} // namespace

bool installSparkleReadyToInstallRaise()
{
    if (originalShowReadyToInstall != nullptr) {
        return true;
    }

    // Sparkle has no delegate callback for this step, so wrap the user driver method.
    Method method = class_getInstanceMethod(NSClassFromString(@"SPUStandardUserDriver"),
                                            @selector(showReadyToInstallAndRelaunch:));
    if (method == nullptr
        || method_getNumberOfArguments(method) != kShowReadyToInstallArgumentCount) {
        qWarning() << "SparkleUpdateService: unexpected SPUStandardUserDriver; "
                      "Ready to Install will not be brought forward";
        return false;
    }

    originalShowReadyToInstall =
        reinterpret_cast<ShowReadyToInstallFn>(method_getImplementation(method));
    method_setImplementation(method, reinterpret_cast<IMP>(showReadyToInstallInFront));
    return true;
}

std::unique_ptr<IUpdateService> createPlatformUpdateService(UpdateServiceKind kind,
                                                            InstallSource source)
{
    if (kind == UpdateServiceKind::Sparkle) {
        return std::make_unique<SparkleUpdateService>(source);
    }
    return nullptr;
}

#endif // Q_OS_MACOS
