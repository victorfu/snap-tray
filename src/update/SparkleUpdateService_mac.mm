#include "update/IUpdateService.h"
#include "update/UpdateCoordinator.h"
#include "update/UpdateServiceFactory.h"
#include "update/UpdateSettingsManager.h"
#include "update_config.h"
#include "version.h"

#ifdef Q_OS_MAC

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <objc/message.h>
#import <objc/runtime.h>

#include <QCoreApplication>
#include <QMetaObject>

static void notifySuccessfulUpdateCheck()
{
    QMetaObject::invokeMethod(&UpdateCoordinator::instance(),
                              &UpdateCoordinator::recordSuccessfulCheck,
                              Qt::QueuedConnection);
}

@interface SnapTraySparkleUpdaterDelegate : NSObject
@end

@implementation SnapTraySparkleUpdaterDelegate

- (void)updater:(id)updater didFinishLoadingAppcast:(id)appcast
{
    (void)updater;
    (void)appcast;
    notifySuccessfulUpdateCheck();
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
                controllerAlloc, initSel, NO, m_delegate, nil);

            if (m_controller == nil) {
                m_lastError = QCoreApplication::translate(
                    "SparkleUpdateService",
                    "Failed to create the Sparkle updater controller.");
                if (errorMessage) {
                    *errorMessage = m_lastError;
                }
                return false;
            }

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

std::unique_ptr<IUpdateService> createPlatformUpdateService(UpdateServiceKind kind,
                                                            InstallSource source)
{
    if (kind == UpdateServiceKind::Sparkle) {
        return std::make_unique<SparkleUpdateService>(source);
    }
    return nullptr;
}

#endif // Q_OS_MAC
