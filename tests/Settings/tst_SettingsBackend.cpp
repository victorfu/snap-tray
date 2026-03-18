#include <QtTest>
#include <QSignalSpy>

#include "qml/SettingsBackend.h"
#include "settings/RecordingSettingsManager.h"
#include "settings/Settings.h"
#include "update/UpdateChecker.h"

using SnapTray::SettingsBackend;

class tst_SettingsBackend : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testNormalizeRecordingAudioSettings_PreservesUnavailableLoadedDevice();
    void testBlurTypeMapping_RoundTripMatchesUiIndices();
    void testCheckForUpdates_UnsupportedInstallSource_EmitsUnavailable();

private:
    void clearTestSettings();
};

void tst_SettingsBackend::init()
{
    clearTestSettings();
}

void tst_SettingsBackend::cleanup()
{
    clearTestSettings();
}

void tst_SettingsBackend::clearTestSettings()
{
    auto settings = SnapTray::getSettings();
    settings.remove("recording/audioDevice");
    settings.remove("detection/blurType");
    settings.sync();
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

void tst_SettingsBackend::testCheckForUpdates_UnsupportedInstallSource_EmitsUnavailable()
{
    SettingsBackend backend;
    backend.m_updateChecker = new UpdateChecker(&backend);
    backend.m_updateChecker->m_installSource = InstallSource::MicrosoftStore;

    QSignalSpy unavailableSpy(&backend, &SettingsBackend::updateCheckUnavailable);
    QSignalSpy failedSpy(&backend, &SettingsBackend::updateCheckFailed);

    backend.checkForUpdates();

    QCOMPARE(unavailableSpy.count(), 1);
    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(unavailableSpy.at(0).at(0).toString(),
             UpdateChecker::updateCheckDisabledReason(InstallSource::MicrosoftStore));
    QVERIFY(!backend.isCheckingForUpdates());
}

QTEST_MAIN(tst_SettingsBackend)
#include "tst_SettingsBackend.moc"
