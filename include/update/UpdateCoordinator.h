#ifndef SNAPTRAY_UPDATECOORDINATOR_H
#define SNAPTRAY_UPDATECOORDINATOR_H

#include "update/IUpdateService.h"

#include <QObject>
#include <QDateTime>
#include <functional>
#include <memory>

class UpdateCoordinator : public QObject
{
    Q_OBJECT

public:
    using UpdateServiceFactoryFn =
        std::function<std::unique_ptr<IUpdateService>(UpdateServiceKind, InstallSource)>;
    using ShutdownGuardFn = std::function<bool()>;
    using ShutdownRequestFn = std::function<void()>;

    static UpdateCoordinator& instance();

    static UpdatePlatform currentPlatform();
    static UpdateServiceKind selectServiceKind(UpdatePlatform platform, InstallSource source);
    static void resetForTests();
    static void setServiceFactoryForTests(UpdateServiceFactoryFn factory);
    static void setShutdownHooks(ShutdownGuardFn canShutdown,
                                 ShutdownRequestFn requestShutdown);
    static bool canRequestApplicationShutdown();
    static void requestApplicationShutdown();

    void initialize();
    void startAutomaticChecks();
    void syncSettings(bool autoCheckEnabled, int checkIntervalHours);
    UpdateCheckResult checkForUpdatesInteractive();

    InstallSource installSource() const;
    UpdateServiceKind serviceKind() const;
    QString updateChannelLabel() const;
    bool isExternallyManaged() const;
    QString managementMessage() const;
    QDateTime lastCheckTime() const;
    void recordSuccessfulCheck();

signals:
    void lastCheckTimeChanged();

private:
    UpdateCoordinator() = default;

    void ensureService();

    std::unique_ptr<IUpdateService> m_service;
    InstallSource m_installSource = InstallSource::Unknown;
    UpdateServiceKind m_serviceKind = UpdateServiceKind::Unsupported;
    bool m_initialized = false;
    bool m_autoCheckEnabled = true;
    int m_checkIntervalHours = 24;
    bool m_hasPendingSettingsSync = false;
};

#endif // SNAPTRAY_UPDATECOORDINATOR_H
