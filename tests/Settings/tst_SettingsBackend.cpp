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

    void testNormalizeRecordingAudioSettings_ClearsStaleLoadedDevice();

private:
    void clearRecordingSettings();
};

void tst_SettingsBackend::init()
{
    clearRecordingSettings();
}

void tst_SettingsBackend::cleanup()
{
    clearRecordingSettings();
}

void tst_SettingsBackend::clearRecordingSettings()
{
    auto settings = SnapTray::getSettings();
    settings.remove("recording/audioDevice");
    settings.sync();
}

void tst_SettingsBackend::testNormalizeRecordingAudioSettings_ClearsStaleLoadedDevice()
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

    QVERIFY(backend.normalizeRecordingAudioSettings());
    QCOMPARE(backend.recordingAudioDevice(), QString());
    QCOMPARE(deviceChangedSpy.count(), 1);
}

QTEST_MAIN(tst_SettingsBackend)
#include "tst_SettingsBackend.moc"
