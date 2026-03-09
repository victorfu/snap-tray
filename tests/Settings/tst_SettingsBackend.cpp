#include <QtTest>
#include <QSignalSpy>

#include "qml/SettingsBackend.h"
#include "settings/RecordingSettingsManager.h"
#include "settings/Settings.h"

using SnapTray::SettingsBackend;

class tst_SettingsBackend : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testNormalizeRecordingAudioSettings_PreservesUnavailableLoadedDevice();
    void testBlurTypeMapping_RoundTripMatchesUiIndices();

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

QTEST_MAIN(tst_SettingsBackend)
#include "tst_SettingsBackend.moc"
