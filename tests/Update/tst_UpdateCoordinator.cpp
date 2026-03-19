#include <QtTest>
#include <QSignalSpy>

#include "settings/Settings.h"
#include "update/UpdateCoordinator.h"
#include "update/InstallSourceDetector.h"
#include "update/UpdateSettingsManager.h"

namespace {

class FakeUpdateService final : public IUpdateService
{
public:
    UpdateServiceKind kind() const override { return UpdateServiceKind::Unsupported; }
    InstallSource installSource() const override { return InstallSource::DirectDownload; }
    bool isExternallyManaged() const override { return false; }
    QString managementMessage() const override { return QString(); }

    bool initialize(QString* errorMessage) override
    {
        initializeCalled = true;
        sawSyncBeforeInitialize = preInitializeSyncCount > 0;
        autoCheckValueSeenAtInitialize = lastAutoCheckEnabled;
        checkIntervalSeenAtInitialize = lastCheckIntervalHours;
        if (errorMessage) {
            *errorMessage = QString();
        }
        return true;
    }

    void startAutomaticChecks() override
    {
        startAutomaticChecksCalled = true;
    }

    void syncSettings(bool autoCheckEnabled, int checkIntervalHours) override
    {
        ++syncCallCount;
        if (!initializeCalled) {
            ++preInitializeSyncCount;
        }
        lastAutoCheckEnabled = autoCheckEnabled;
        lastCheckIntervalHours = checkIntervalHours;
    }

    UpdateCheckResult checkForUpdatesInteractive() override
    {
        return UpdateCheckResult::started();
    }

    bool initializeCalled = false;
    bool startAutomaticChecksCalled = false;
    bool sawSyncBeforeInitialize = false;
    bool autoCheckValueSeenAtInitialize = true;
    int checkIntervalSeenAtInitialize = -1;
    int syncCallCount = 0;
    int preInitializeSyncCount = 0;
    bool lastAutoCheckEnabled = true;
    int lastCheckIntervalHours = UpdateSettingsManager::kDefaultCheckIntervalHours;
};

FakeUpdateService* g_fakeService = nullptr;

void clearUpdateSettings()
{
    auto settings = SnapTray::getSettings();
    settings.remove("update/autoCheck");
    settings.remove("update/checkIntervalHours");
    settings.remove("update/lastCheckTime");
    settings.sync();
}

} // namespace

class tst_UpdateCoordinator : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testSelectServiceKind_MacDirectDownload_UsesSparkle();
    void testSelectServiceKind_WindowsDirectDownload_UsesWinSparkle();
    void testSelectServiceKind_StoreManagedSources_UseExternalManaged();
    void testSelectServiceKind_Homebrew_UsesExternalManaged();
    void testSelectServiceKind_Unknown_UsesUnsupported();
    void testInitialize_LoadsPersistedSettingsBeforeServiceStarts();
    void testShutdownHooks_DefaultToAllowQuit();
    void testShutdownHooks_InvokeRegisteredCallbacks();
    void testRecordSuccessfulCheck_UpdatesLastCheckTimeAndEmitsSignal();
};

void tst_UpdateCoordinator::init()
{
    clearUpdateSettings();
    InstallSourceDetector::clearDetectedSourceForTests();
    UpdateCoordinator::resetForTests();
    g_fakeService = nullptr;
}

void tst_UpdateCoordinator::cleanup()
{
    clearUpdateSettings();
    InstallSourceDetector::clearDetectedSourceForTests();
    UpdateCoordinator::resetForTests();
    g_fakeService = nullptr;
}

void tst_UpdateCoordinator::testSelectServiceKind_MacDirectDownload_UsesSparkle()
{
    QCOMPARE(UpdateCoordinator::selectServiceKind(UpdatePlatform::MacOS,
                                                  InstallSource::DirectDownload),
             UpdateServiceKind::Sparkle);
}

void tst_UpdateCoordinator::testSelectServiceKind_WindowsDirectDownload_UsesWinSparkle()
{
    QCOMPARE(UpdateCoordinator::selectServiceKind(UpdatePlatform::Windows,
                                                  InstallSource::DirectDownload),
             UpdateServiceKind::WinSparkle);
}

void tst_UpdateCoordinator::testSelectServiceKind_StoreManagedSources_UseExternalManaged()
{
    QCOMPARE(UpdateCoordinator::selectServiceKind(UpdatePlatform::Windows,
                                                  InstallSource::MicrosoftStore),
             UpdateServiceKind::ExternalManaged);
    QCOMPARE(UpdateCoordinator::selectServiceKind(UpdatePlatform::MacOS,
                                                  InstallSource::MacAppStore),
             UpdateServiceKind::ExternalManaged);
    QCOMPARE(UpdateCoordinator::selectServiceKind(UpdatePlatform::MacOS,
                                                  InstallSource::Development),
             UpdateServiceKind::ExternalManaged);
}

void tst_UpdateCoordinator::testSelectServiceKind_Homebrew_UsesExternalManaged()
{
    QCOMPARE(UpdateCoordinator::selectServiceKind(UpdatePlatform::MacOS,
                                                  InstallSource::Homebrew),
             UpdateServiceKind::ExternalManaged);
}

void tst_UpdateCoordinator::testSelectServiceKind_Unknown_UsesUnsupported()
{
    QCOMPARE(UpdateCoordinator::selectServiceKind(UpdatePlatform::Other,
                                                  InstallSource::Unknown),
             UpdateServiceKind::Unsupported);
}

void tst_UpdateCoordinator::testInitialize_LoadsPersistedSettingsBeforeServiceStarts()
{
    UpdateSettingsManager::instance().setAutoCheckEnabled(false);
    UpdateSettingsManager::instance().setCheckIntervalHours(72);
    InstallSourceDetector::setDetectedSourceForTests(InstallSource::DirectDownload);
    UpdateCoordinator::setServiceFactoryForTests(
        [](UpdateServiceKind, InstallSource) -> std::unique_ptr<IUpdateService> {
            auto service = std::make_unique<FakeUpdateService>();
            g_fakeService = service.get();
            return service;
        });

    UpdateCoordinator::instance().startAutomaticChecks();

    QVERIFY(g_fakeService != nullptr);
    QVERIFY(g_fakeService->initializeCalled);
    QVERIFY(g_fakeService->startAutomaticChecksCalled);
    QVERIFY(g_fakeService->sawSyncBeforeInitialize);
    QCOMPARE(g_fakeService->autoCheckValueSeenAtInitialize, false);
    QCOMPARE(g_fakeService->checkIntervalSeenAtInitialize, 72);
    QCOMPARE(g_fakeService->syncCallCount, 2);
}

void tst_UpdateCoordinator::testShutdownHooks_DefaultToAllowQuit()
{
    QVERIFY(UpdateCoordinator::canRequestApplicationShutdown());
}

void tst_UpdateCoordinator::testShutdownHooks_InvokeRegisteredCallbacks()
{
    bool canShutdownCalled = false;
    bool requestShutdownCalled = false;

    UpdateCoordinator::setShutdownHooks(
        [&canShutdownCalled]() {
            canShutdownCalled = true;
            return false;
        },
        [&requestShutdownCalled]() {
            requestShutdownCalled = true;
        });

    QVERIFY(!UpdateCoordinator::canRequestApplicationShutdown());
    QVERIFY(canShutdownCalled);

    UpdateCoordinator::requestApplicationShutdown();
    QVERIFY(requestShutdownCalled);
}

void tst_UpdateCoordinator::testRecordSuccessfulCheck_UpdatesLastCheckTimeAndEmitsSignal()
{
    UpdateSettingsManager::instance().setLastCheckTime(QDateTime());
    UpdateCoordinator::resetForTests();

    auto& coordinator = UpdateCoordinator::instance();
    QSignalSpy lastCheckedSpy(&coordinator, &UpdateCoordinator::lastCheckTimeChanged);

    const QDateTime before = QDateTime::currentDateTime().addSecs(-1);
    coordinator.recordSuccessfulCheck();

    QCOMPARE(lastCheckedSpy.count(), 1);
    QVERIFY(coordinator.lastCheckTime().isValid());
    QVERIFY(coordinator.lastCheckTime() >= before);
}

QTEST_MAIN(tst_UpdateCoordinator)
#include "tst_UpdateCoordinator.moc"
