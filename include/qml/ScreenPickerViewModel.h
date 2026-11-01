#pragma once

#include "recording/ScreenSourceService.h"
#include "capture/AudioLevelMonitor.h"

#include <QObject>
#include <QPointer>
#include <QScreen>
#include <QStringList>
#include <QVariantList>
#include <QVector>

namespace SnapTray {

class ScreenPickerViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList screens READ screens NOTIFY screensChanged)
    Q_PROPERTY(int thumbnailCacheBuster READ thumbnailCacheBuster NOTIFY thumbnailCacheBusterChanged)

    // Recording setup shown with the screens. Changes are saved through
    // RecordingSettingsManager, so the next quick recording reuses them.
    Q_PROPERTY(bool microphoneEnabled READ microphoneEnabled WRITE setMicrophoneEnabled NOTIFY audioSettingsChanged)
    Q_PROPERTY(bool systemAudioEnabled READ systemAudioEnabled WRITE setSystemAudioEnabled NOTIFY audioSettingsChanged)
    Q_PROPERTY(bool systemAudioSupported READ systemAudioSupported NOTIFY devicesChanged)
    Q_PROPERTY(bool outputHasAudio READ outputHasAudio CONSTANT)
    Q_PROPERTY(QString selectedDevice READ selectedDevice WRITE setSelectedDevice NOTIFY audioSettingsChanged)
    Q_PROPERTY(QVariantList inputDevices READ inputDevices NOTIFY devicesChanged)
    Q_PROPERTY(bool devicesLoaded READ devicesLoaded NOTIFY devicesChanged)
    Q_PROPERTY(QString outputSummary READ outputSummary CONSTANT)
    Q_PROPERTY(QString audioNote READ audioNote CONSTANT)
    Q_PROPERTY(bool showSetupBeforeRecording READ showSetupBeforeRecording WRITE setShowSetupBeforeRecording NOTIFY showSetupBeforeRecordingChanged)
    Q_PROPERTY(bool microphoneTestActive READ microphoneTestActive NOTIFY microphoneTestChanged)
    Q_PROPERTY(qreal microphoneLevel READ microphoneLevel NOTIFY microphoneLevelChanged)
    Q_PROPERTY(QString microphoneTestStatus READ microphoneTestStatus NOTIFY microphoneTestChanged)

public:
    explicit ScreenPickerViewModel(QObject* parent = nullptr);
    explicit ScreenPickerViewModel(const QVector<ScreenSourceInfo>& sources, QObject* parent = nullptr);
    ~ScreenPickerViewModel() override;

    QVariantList screens() const { return m_screens; }
    int thumbnailCacheBuster() const { return m_thumbnailCacheBuster; }

    void setSources(const QVector<ScreenSourceInfo>& sources);

    Q_INVOKABLE void chooseScreen(int index);
    Q_INVOKABLE void cancel();

    bool microphoneEnabled() const { return m_microphoneEnabled; }
    void setMicrophoneEnabled(bool enabled);
    bool systemAudioEnabled() const { return m_systemAudioEnabled; }
    void setSystemAudioEnabled(bool enabled);
    bool systemAudioSupported() const { return m_systemAudioSupported; }
    bool outputHasAudio() const;
    QString selectedDevice() const { return m_selectedDevice; }
    void setSelectedDevice(const QString& deviceId);
    QVariantList inputDevices() const { return m_inputDevices; }
    bool devicesLoaded() const { return m_devicesLoaded; }
    QString outputSummary() const;
    QString audioNote() const;
    bool showSetupBeforeRecording() const;
    void setShowSetupBeforeRecording(bool show);

    // Live input level while testing; the microphone is released when the
    // test stops, a screen is chosen, or the picker is cancelled.
    Q_INVOKABLE void toggleMicrophoneTest();
    Q_INVOKABLE void stopMicrophoneTest();
    bool microphoneTestActive() const;
    qreal microphoneLevel() const { return m_levelMonitor.level(); }
    QString microphoneTestStatus() const;

    // Loads input devices; the dialog calls it after it is shown.
    Q_INVOKABLE void loadDevices();
    AudioLevelMonitor& levelMonitor() { return m_levelMonitor; }
    void setAudioEngineFactory(AudioLevelMonitor::EngineFactory factory);

signals:
    void screenChosen(QScreen* screen);
    void cancelled();
    void screensChanged();
    void thumbnailCacheBusterChanged();
    void audioSettingsChanged();
    void devicesChanged();
    void showSetupBeforeRecordingChanged();
    void microphoneTestChanged();
    void microphoneLevelChanged();

private:
    void clearThumbnails();
    void loadAudioSettings();
    void saveAudioSettings();

    AudioLevelMonitor m_levelMonitor;
    AudioLevelMonitor::EngineFactory m_engineFactory;
    bool m_microphoneEnabled = false;
    bool m_systemAudioEnabled = false;
    bool m_systemAudioSupported = true;
    bool m_devicesLoaded = false;
    QString m_selectedDevice;
    QVariantList m_inputDevices;

    QVector<QPointer<QScreen>> m_screenRefs;
    QStringList m_screenIds;
    QVariantList m_screens;
    QStringList m_thumbnailIds;
    int m_thumbnailCacheBuster = 0;
};

} // namespace SnapTray
