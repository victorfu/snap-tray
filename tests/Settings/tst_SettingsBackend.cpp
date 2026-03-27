#include <QtTest>
#include <QSignalSpy>

#include "qml/SettingsBackend.h"
#include "settings/RecordingSettingsManager.h"
#include "settings/Settings.h"
#include "update/IUpdateService.h"
#include "update/InstallSourceDetector.h"
#include "update/UpdateCoordinator.h"

using SnapTray::SettingsBackend;

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
        if (errorMessage) {
            *errorMessage = QString();
        }
        initializeCalled = true;
        return true;
    }

    void startAutomaticChecks() override {}

    void syncSettings(bool autoCheckEnabled, int checkIntervalHours) override
    {
        lastAutoCheckEnabled = autoCheckEnabled;
        lastCheckIntervalHours = checkIntervalHours;
    }

    UpdateCheckResult checkForUpdatesInteractive() override
    {
        ++interactiveCheckCount;
        return interactiveResult;
    }

    UpdateCheckResult interactiveResult = UpdateCheckResult::started();
    bool initializeCalled = false;
    int interactiveCheckCount = 0;
    bool lastAutoCheckEnabled = true;
    int lastCheckIntervalHours = 24;
};

FakeUpdateService* g_fakeUpdateService = nullptr;

} // namespace

class tst_SettingsBackend : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testAvailableLanguages_PrioritizesConfiguredAsianLanguages();
    void testCursorCompanionStyle_RoundTripPersistsAndSignals();
    void testMagnifierEnabled_RoundTripPersistsAndSignals();
    void testNormalizeRecordingAudioSettings_PreservesUnavailableLoadedDevice();
    void testBlurTypeMapping_RoundTripMatchesUiIndices();
    void testCheckForUpdates_Started_EmitsNoTerminalSignals();
    void testCheckForUpdates_Failed_EmitsFailure();
    void testCheckForUpdates_UnsupportedInstallSource_EmitsUnavailable();
    void testLastCheckedTextChanged_WhenCoordinatorRecordsSuccessfulCheck();

private:
    void clearTestSettings();
    void installFakeUpdateService(UpdateCheckResult result);
};

void tst_SettingsBackend::init()
{
    clearTestSettings();
    InstallSourceDetector::clearDetectedSourceForTests();
    UpdateCoordinator::resetForTests();
    g_fakeUpdateService = nullptr;
}

void tst_SettingsBackend::cleanup()
{
    clearTestSettings();
    InstallSourceDetector::clearDetectedSourceForTests();
    UpdateCoordinator::resetForTests();
    g_fakeUpdateService = nullptr;
}

void tst_SettingsBackend::clearTestSettings()
{
    auto settings = SnapTray::getSettings();
    settings.remove("recording/audioDevice");
    settings.remove("detection/blurType");
    settings.remove("general/startOnLogin");
    settings.remove("regionCapture/cursorCompanionStyle");
    settings.remove("update/lastCheckTime");
    settings.sync();
}

void tst_SettingsBackend::installFakeUpdateService(UpdateCheckResult result)
{
    InstallSourceDetector::setDetectedSourceForTests(InstallSource::DirectDownload);
    UpdateCoordinator::setServiceFactoryForTests(
        [result](UpdateServiceKind, InstallSource) -> std::unique_ptr<IUpdateService> {
            auto service = std::make_unique<FakeUpdateService>();
            service->interactiveResult = result;
            g_fakeUpdateService = service.get();
            return service;
        });
}

void tst_SettingsBackend::testAvailableLanguages_PrioritizesConfiguredAsianLanguages()
{
    const SettingsBackend backend;
    const QVariantList languages = backend.availableLanguages();

    QVERIFY(languages.size() >= 7);

    const QStringList expectedLeadingCodes = {
        QStringLiteral("en"),
        QStringLiteral("zh_TW"),
        QStringLiteral("zh_HK"),
        QStringLiteral("zh_CN"),
        QStringLiteral("ja"),
        QStringLiteral("ko"),
        QStringLiteral("th"),
    };

    QStringList actualCodes;
    actualCodes.reserve(languages.size());
    for (const QVariant& language : languages) {
        actualCodes.append(language.toMap().value(QStringLiteral("code")).toString());
    }

    QCOMPARE(actualCodes.first(expectedLeadingCodes.size()), expectedLeadingCodes);
    QVERIFY(actualCodes.indexOf(QStringLiteral("ar")) < actualCodes.indexOf(QStringLiteral("cs")));
    QVERIFY(actualCodes.indexOf(QStringLiteral("pt")) < actualCodes.indexOf(QStringLiteral("pt_PT")));
    QCOMPARE(actualCodes.last(), QStringLiteral("vi"));
}

void tst_SettingsBackend::testMagnifierEnabled_RoundTripPersistsAndSignals()
{
    auto settings = SnapTray::getSettings();

    SettingsBackend backend;
    QCOMPARE(backend.magnifierEnabled(), false);

    QSignalSpy changedSpy(&backend, &SettingsBackend::magnifierEnabledChanged);

    backend.setMagnifierEnabled(true);

    QCOMPARE(backend.magnifierEnabled(), true);
    QCOMPARE(settings.value("regionCapture/cursorCompanionStyle").toInt(), 1);
    QCOMPARE(changedSpy.count(), 1);

    backend.setMagnifierEnabled(false);

    QCOMPARE(backend.magnifierEnabled(), false);
    QCOMPARE(settings.value("regionCapture/cursorCompanionStyle").toInt(), 0);
    QCOMPARE(changedSpy.count(), 2);
}

void tst_SettingsBackend::testCursorCompanionStyle_RoundTripPersistsAndSignals()
{
    auto settings = SnapTray::getSettings();
    SettingsBackend backend;
    QCOMPARE(backend.cursorCompanionStyle(), 2);
    QVERIFY(!settings.contains("regionCapture/cursorCompanionStyle"));

    QSignalSpy styleChangedSpy(&backend, &SettingsBackend::cursorCompanionStyleChanged);
    QSignalSpy magnifierChangedSpy(&backend, &SettingsBackend::magnifierEnabledChanged);

    backend.setCursorCompanionStyle(2);

    QCOMPARE(backend.cursorCompanionStyle(), 2);
    QCOMPARE(backend.magnifierEnabled(), false);
    QVERIFY(!settings.contains("regionCapture/cursorCompanionStyle"));
    QCOMPARE(styleChangedSpy.count(), 0);
    QCOMPARE(magnifierChangedSpy.count(), 0);

    backend.setCursorCompanionStyle(1);

    QCOMPARE(backend.cursorCompanionStyle(), 1);
    QCOMPARE(backend.magnifierEnabled(), true);
    QCOMPARE(settings.value("regionCapture/cursorCompanionStyle").toInt(), 1);
    QCOMPARE(styleChangedSpy.count(), 1);
    QCOMPARE(magnifierChangedSpy.count(), 1);

    backend.setCursorCompanionStyle(2);

    QCOMPARE(settings.value("regionCapture/cursorCompanionStyle").toInt(), 2);
    QCOMPARE(backend.cursorCompanionStyle(), 2);
    QCOMPARE(backend.magnifierEnabled(), false);
    QCOMPARE(styleChangedSpy.count(), 2);
    QCOMPARE(magnifierChangedSpy.count(), 2);
}

void tst_SettingsBackend::testNormalizeRecordingAudioSettings_PreservesUnavailableLoadedDevice()
{
    RecordingSettingsManager::instance().setAudioDevice(QStringLiteral("stale-device"));

    SettingsBackend backend;
    QCOMPARE(backend.recordingAudioDevice(), QStringLiteral("stale-device"));

    backend.m_recordingAudioDeviceItems = {
        QVariantMap{
            {QStringLiteral("text"), QStringLiteral("Built-in Microphone")},
            {QStringLiteral("value"), QStringLiteral("built-in-mic")},
        },
    };
    backend.m_recordingAudioDevicesLoaded = true;

    QSignalSpy deviceChangedSpy(&backend, &SettingsBackend::recordingAudioDeviceChanged);

    QVERIFY(!backend.normalizeRecordingAudioSettings());
    QCOMPARE(backend.recordingAudioDevice(), QStringLiteral("stale-device"));
    QCOMPARE(deviceChangedSpy.count(), 0);
}

void tst_SettingsBackend::testBlurTypeMapping_RoundTripMatchesUiIndices()
{
    auto settings = SnapTray::getSettings();
    settings.setValue("detection/blurType", QStringLiteral("gaussian"));
    settings.sync();

    SettingsBackend backend;
    QCOMPARE(backend.blurType(), 1);

    backend.setBlurType(0);
    QCOMPARE(settings.value("detection/blurType").toString(), QStringLiteral("pixelate"));

    settings.setValue("detection/blurType", QStringLiteral("pixelate"));
    settings.sync();

    SettingsBackend reloadedBackend;
    QCOMPARE(reloadedBackend.blurType(), 0);

    reloadedBackend.setBlurType(1);
    QCOMPARE(settings.value("detection/blurType").toString(), QStringLiteral("gaussian"));
}

void tst_SettingsBackend::testCheckForUpdates_Started_EmitsNoTerminalSignals()
{
    installFakeUpdateService(UpdateCheckResult::started());
    SettingsBackend backend;

    QSignalSpy unavailableSpy(&backend, &SettingsBackend::updateCheckUnavailable);
    QSignalSpy failedSpy(&backend, &SettingsBackend::updateCheckFailed);

    backend.checkForUpdates();

    QVERIFY(g_fakeUpdateService != nullptr);
    QVERIFY(g_fakeUpdateService->initializeCalled);
    QCOMPARE(g_fakeUpdateService->interactiveCheckCount, 1);
    QCOMPARE(unavailableSpy.count(), 0);
    QCOMPARE(failedSpy.count(), 0);
    QVERIFY(!backend.isCheckingForUpdates());
}

void tst_SettingsBackend::testCheckForUpdates_Failed_EmitsFailure()
{
    installFakeUpdateService(UpdateCheckResult::failed(QStringLiteral("Native updater unavailable")));
    SettingsBackend backend;

    QSignalSpy unavailableSpy(&backend, &SettingsBackend::updateCheckUnavailable);
    QSignalSpy failedSpy(&backend, &SettingsBackend::updateCheckFailed);

    backend.checkForUpdates();

    QVERIFY(g_fakeUpdateService != nullptr);
    QVERIFY(g_fakeUpdateService->initializeCalled);
    QCOMPARE(g_fakeUpdateService->interactiveCheckCount, 1);
    QCOMPARE(unavailableSpy.count(), 0);
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.at(0).at(0).toString(), QStringLiteral("Native updater unavailable"));
    QVERIFY(!backend.isCheckingForUpdates());
}

void tst_SettingsBackend::testCheckForUpdates_UnsupportedInstallSource_EmitsUnavailable()
{
    InstallSourceDetector::setDetectedSourceForTests(InstallSource::MicrosoftStore);
    UpdateCoordinator::resetForTests();

    SettingsBackend backend;

    QSignalSpy unavailableSpy(&backend, &SettingsBackend::updateCheckUnavailable);
    QSignalSpy failedSpy(&backend, &SettingsBackend::updateCheckFailed);

    QVERIFY(backend.updatesExternallyManaged());
    QCOMPARE(backend.updateChannelLabel(), QStringLiteral("Microsoft Store"));
    QCOMPARE(backend.updateStatusMessage(),
             updateManagementMessageForSource(InstallSource::MicrosoftStore));

    backend.checkForUpdates();

    QCOMPARE(unavailableSpy.count(), 1);
    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(unavailableSpy.at(0).at(0).toString(),
             updateManagementMessageForSource(InstallSource::MicrosoftStore));
    QVERIFY(!backend.isCheckingForUpdates());
}

void tst_SettingsBackend::testLastCheckedTextChanged_WhenCoordinatorRecordsSuccessfulCheck()
{
    SettingsBackend backend;

    QSignalSpy lastCheckedSpy(&backend, &SettingsBackend::lastCheckedTextChanged);

    UpdateCoordinator::instance().recordSuccessfulCheck();

    QCOMPARE(lastCheckedSpy.count(), 1);
    QVERIFY(backend.lastCheckedText() != QStringLiteral("Never"));
}

QTEST_MAIN(tst_SettingsBackend)
#include "tst_SettingsBackend.moc"
