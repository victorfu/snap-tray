#include "update/UpdateCoordinator.h"

#include "update/InstallSourceDetector.h"
#include "update/UpdateSettingsManager.h"
#include "update/UpdateServiceFactory.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QMetaObject>
#include <QThread>

namespace {

UpdateCoordinator::UpdateServiceFactoryFn& serviceFactoryOverride()
{
    static UpdateCoordinator::UpdateServiceFactoryFn factory;
    return factory;
}

UpdateCoordinator::ShutdownGuardFn& shutdownGuardOverride()
{
    static UpdateCoordinator::ShutdownGuardFn callback;
    return callback;
}

UpdateCoordinator::ShutdownRequestFn& shutdownRequestOverride()
{
    static UpdateCoordinator::ShutdownRequestFn callback;
    return callback;
}

class ExternalManagedUpdateService final : public IUpdateService
{
public:
    explicit ExternalManagedUpdateService(InstallSource source)
        : m_installSource(source)
    {
    }

    UpdateServiceKind kind() const override { return UpdateServiceKind::ExternalManaged; }
    InstallSource installSource() const override { return m_installSource; }
    bool isExternallyManaged() const override { return true; }
    QString managementMessage() const override
    {
        return updateManagementMessageForSource(m_installSource);
    }
    bool initialize(QString* errorMessage) override
    {
        if (errorMessage) {
            *errorMessage = QString();
        }
        return true;
    }
    void startAutomaticChecks() override {}
    void syncSettings(bool, int) override {}
    UpdateCheckResult checkForUpdatesInteractive() override
    {
        return UpdateCheckResult::unavailable(managementMessage());
    }

private:
    InstallSource m_installSource;
};

class UnsupportedUpdateService final : public IUpdateService
{
public:
    explicit UnsupportedUpdateService(InstallSource source)
        : m_installSource(source)
    {
    }

    UpdateServiceKind kind() const override { return UpdateServiceKind::Unsupported; }
    InstallSource installSource() const override { return m_installSource; }
    bool isExternallyManaged() const override { return false; }
    QString managementMessage() const override { return QString(); }
    bool initialize(QString* errorMessage) override
    {
        const QString message = QCoreApplication::translate(
            "UpdateCoordinator",
            "In-app updates are unavailable for this platform.");
        if (errorMessage) {
            *errorMessage = message;
        }
        m_errorMessage = message;
        return false;
    }
    void startAutomaticChecks() override {}
    void syncSettings(bool, int) override {}
    UpdateCheckResult checkForUpdatesInteractive() override
    {
        return UpdateCheckResult::failed(m_errorMessage);
    }

private:
    InstallSource m_installSource;
    QString m_errorMessage = QCoreApplication::translate(
        "UpdateCoordinator",
        "In-app updates are unavailable for this platform.");
};

std::unique_ptr<IUpdateService> createService(UpdateServiceKind kind, InstallSource source)
{
    if (auto& factory = serviceFactoryOverride()) {
        if (auto overrideService = factory(kind, source)) {
            return overrideService;
        }
    }

    if (kind == UpdateServiceKind::ExternalManaged) {
        return std::make_unique<ExternalManagedUpdateService>(source);
    }

    if (kind == UpdateServiceKind::Unsupported) {
        return std::make_unique<UnsupportedUpdateService>(source);
    }

    if (auto platformService = createPlatformUpdateService(kind, source)) {
        return platformService;
    }

    return std::make_unique<UnsupportedUpdateService>(source);
}

} // namespace

UpdatePlatform currentUpdatePlatform()
{
#ifdef Q_OS_MAC
    return UpdatePlatform::MacOS;
#elif defined(Q_OS_WIN)
    return UpdatePlatform::Windows;
#else
    return UpdatePlatform::Other;
#endif
}

QString updateManagementMessageForSource(InstallSource source)
{
    switch (source) {
    case InstallSource::MicrosoftStore:
    case InstallSource::MacAppStore:
        return QCoreApplication::translate(
                   "UpdateCoordinator",
                   "Updates for this installation are managed by %1.")
            .arg(InstallSourceDetector::getSourceDisplayName(source));
    case InstallSource::Homebrew:
        return QCoreApplication::translate(
                   "UpdateCoordinator",
                   "Updates for this installation are managed by Homebrew.");
    case InstallSource::Development:
        return QCoreApplication::translate(
            "UpdateCoordinator",
            "In-app updates are unavailable for development builds.");
    case InstallSource::Unknown:
        return QCoreApplication::translate(
            "UpdateCoordinator",
            "In-app updates are unavailable for this installation.");
    case InstallSource::DirectDownload:
    default:
        return QString();
    }
}

UpdateCoordinator& UpdateCoordinator::instance()
{
    static UpdateCoordinator coordinator;
    return coordinator;
}

UpdatePlatform UpdateCoordinator::currentPlatform()
{
    return currentUpdatePlatform();
}

UpdateServiceKind UpdateCoordinator::selectServiceKind(UpdatePlatform platform, InstallSource source)
{
    switch (source) {
    case InstallSource::MicrosoftStore:
    case InstallSource::MacAppStore:
    case InstallSource::Homebrew:
    case InstallSource::Development:
        return UpdateServiceKind::ExternalManaged;
    case InstallSource::DirectDownload:
        if (platform == UpdatePlatform::MacOS) {
            return UpdateServiceKind::Sparkle;
        }
        if (platform == UpdatePlatform::Windows) {
            return UpdateServiceKind::WinSparkle;
        }
        return UpdateServiceKind::Unsupported;
    case InstallSource::Unknown:
    default:
        return UpdateServiceKind::Unsupported;
    }
}

void UpdateCoordinator::resetForTests()
{
    auto& coordinator = instance();
    coordinator.m_service.reset();
    coordinator.m_installSource = InstallSource::Unknown;
    coordinator.m_serviceKind = UpdateServiceKind::Unsupported;
    coordinator.m_initialized = false;
    coordinator.m_autoCheckEnabled = UpdateSettingsManager::kDefaultAutoCheck;
    coordinator.m_checkIntervalHours = UpdateSettingsManager::kDefaultCheckIntervalHours;
    coordinator.m_hasPendingSettingsSync = false;
    setServiceFactoryForTests(UpdateServiceFactoryFn());
    setShutdownHooks(ShutdownGuardFn(), ShutdownRequestFn());
}

void UpdateCoordinator::setServiceFactoryForTests(UpdateServiceFactoryFn factory)
{
    serviceFactoryOverride() = std::move(factory);
}

void UpdateCoordinator::setShutdownHooks(ShutdownGuardFn canShutdown,
                                         ShutdownRequestFn requestShutdown)
{
    shutdownGuardOverride() = std::move(canShutdown);
    shutdownRequestOverride() = std::move(requestShutdown);
}

bool UpdateCoordinator::canRequestApplicationShutdown()
{
    auto canShutdown = shutdownGuardOverride();
    if (!canShutdown) {
        return true;
    }

    if (auto* app = QCoreApplication::instance();
        app != nullptr && QThread::currentThread() != app->thread()) {
        bool result = true;
        QMetaObject::invokeMethod(app, [&result, &canShutdown]() {
            result = canShutdown();
        }, Qt::BlockingQueuedConnection);
        return result;
    }

    return canShutdown();
}

void UpdateCoordinator::requestApplicationShutdown()
{
    auto requestShutdown = shutdownRequestOverride();

    if (auto* app = QCoreApplication::instance();
        app != nullptr && QThread::currentThread() != app->thread()) {
        if (requestShutdown) {
            QMetaObject::invokeMethod(app, [requestShutdown = std::move(requestShutdown)]() {
                requestShutdown();
            }, Qt::QueuedConnection);
        } else {
            QMetaObject::invokeMethod(app, &QCoreApplication::quit, Qt::QueuedConnection);
        }
        return;
    }

    if (requestShutdown) {
        requestShutdown();
    } else if (auto* app = QCoreApplication::instance()) {
        app->quit();
    }
}

void UpdateCoordinator::ensureService()
{
    if (m_service) {
        return;
    }

    m_installSource = InstallSourceDetector::detect();
    m_serviceKind = selectServiceKind(currentPlatform(), m_installSource);
    m_service = createService(m_serviceKind, m_installSource);
}

void UpdateCoordinator::initialize()
{
    ensureService();

    if (m_initialized) {
        return;
    }

    if (!m_hasPendingSettingsSync) {
        auto& settings = UpdateSettingsManager::instance();
        m_autoCheckEnabled = settings.isAutoCheckEnabled();
        m_checkIntervalHours = settings.checkIntervalHours();
        m_hasPendingSettingsSync = true;
    }

    // Prime the service with the persisted user preference before it initializes
    // any native updater that may snapshot defaults during startup.
    m_service->syncSettings(m_autoCheckEnabled, m_checkIntervalHours);

    QString errorMessage;
    m_service->initialize(&errorMessage);
    m_initialized = true;

    if (m_hasPendingSettingsSync) {
        m_service->syncSettings(m_autoCheckEnabled, m_checkIntervalHours);
        m_hasPendingSettingsSync = false;
    }
}

void UpdateCoordinator::startAutomaticChecks()
{
    initialize();

    if (!m_service->isExternallyManaged()) {
        m_service->startAutomaticChecks();
    }
}

void UpdateCoordinator::syncSettings(bool autoCheckEnabled, int checkIntervalHours)
{
    m_autoCheckEnabled = autoCheckEnabled;
    m_checkIntervalHours = checkIntervalHours;
    m_hasPendingSettingsSync = true;

    ensureService();
    if (m_initialized) {
        m_service->syncSettings(autoCheckEnabled, checkIntervalHours);
        m_hasPendingSettingsSync = false;
    }
}

UpdateCheckResult UpdateCoordinator::checkForUpdatesInteractive()
{
    initialize();
    return m_service->checkForUpdatesInteractive();
}

InstallSource UpdateCoordinator::installSource() const
{
    if (!m_service) {
        const_cast<UpdateCoordinator*>(this)->ensureService();
    }
    return m_installSource;
}

UpdateServiceKind UpdateCoordinator::serviceKind() const
{
    if (!m_service) {
        const_cast<UpdateCoordinator*>(this)->ensureService();
    }
    return m_serviceKind;
}

QString UpdateCoordinator::updateChannelLabel() const
{
    if (!m_service) {
        const_cast<UpdateCoordinator*>(this)->ensureService();
    }
    return InstallSourceDetector::getSourceDisplayName(m_installSource);
}

bool UpdateCoordinator::isExternallyManaged() const
{
    if (!m_service) {
        const_cast<UpdateCoordinator*>(this)->ensureService();
    }
    return m_service && m_service->isExternallyManaged();
}

QString UpdateCoordinator::managementMessage() const
{
    if (!m_service) {
        const_cast<UpdateCoordinator*>(this)->ensureService();
    }
    return m_service ? m_service->managementMessage() : QString();
}

QDateTime UpdateCoordinator::lastCheckTime() const
{
    return UpdateSettingsManager::instance().lastCheckTime();
}

void UpdateCoordinator::recordSuccessfulCheck()
{
    UpdateSettingsManager::instance().setLastCheckTime(QDateTime::currentDateTime());
    emit lastCheckTimeChanged();
}
