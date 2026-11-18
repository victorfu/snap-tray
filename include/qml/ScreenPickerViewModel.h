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

    Q_PROPERTY(QString prompterScript MEMBER m_prompterScript NOTIFY prompterChanged)
    Q_PROPERTY(int prompterDisplay MEMBER m_prompterDisplay NOTIFY prompterChanged)
    Q_PROPERTY(QString prompterError READ prompterError NOTIFY prompterChanged)

    // Recording setup shown with the screens. Changes are saved through
    // RecordingSettingsManager, so the next quick recording reuses them.
    Q_PROPERTY(bool microphoneEnabled READ microphoneEnabled WRITE setMicrophoneEnabled NOTIFY audioSettingsChanged)
    Q_PROPERTY(bool systemAudioEnabled READ systemAudioEnabled WRITE setSystemAudioEnabled NOTIFY audioSettingsChanged)
    Q_PROPERTY(bool systemAudioSupported READ systemAudioSupported NOTIFY devicesChanged)
    Q_PROPERTY(bool outputHasAudio READ outputHasAudio NOTIFY recordingModeChanged)
    // Quick or edit mode (#193): the same setting as Settings > Recording.
    Q_PROPERTY(bool editMode READ editMode WRITE setEditMode NOTIFY recordingModeChanged)
    Q_PROPERTY(bool recordingModeSupported READ recordingModeSupported CONSTANT)
    Q_PROPERTY(int quickSaveAction READ quickSaveAction WRITE setQuickSaveAction NOTIFY recordingModeChanged)
    Q_PROPERTY(bool preselectArea READ preselectArea WRITE setPreselectArea NOTIFY recordingModeChanged)
    Q_PROPERTY(bool autoZoomOnOpen READ autoZoomOnOpen WRITE setAutoZoomOnOpen NOTIFY recordingModeChanged)
    Q_PROPERTY(QString selectedDevice READ selectedDevice WRITE setSelectedDevice NOTIFY audioSettingsChanged)
    Q_PROPERTY(QVariantList inputDevices READ inputDevices NOTIFY devicesChanged)
    Q_PROPERTY(bool devicesLoaded READ devicesLoaded NOTIFY devicesChanged)
    Q_PROPERTY(QString outputSummary READ outputSummary NOTIFY recordingModeChanged)
    Q_PROPERTY(QString audioNote READ audioNote NOTIFY recordingModeChanged)
    Q_PROPERTY(bool showSetupBeforeRecording READ showSetupBeforeRecording WRITE setShowSetupBeforeRecording NOTIFY showSetupBeforeRecordingChanged)
    // Keys for shortcut hints (#187): off by default; typed text needs its own switch.
    Q_PROPERTY(bool keyRecordingSupported READ keyRecordingSupported CONSTANT)
    Q_PROPERTY(bool recordShortcutKeys READ recordShortcutKeys WRITE setRecordShortcutKeys NOTIFY keyRecordingChanged)
    Q_PROPERTY(bool recordTypedText READ recordTypedText WRITE setRecordTypedText NOTIFY keyRecordingChanged)
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

    QString prompterError() const { return m_prompterError; }
    QString prompterScript() const { return prompterScreen() ? m_prompterScript : QString(); }
    QScreen* prompterScreen() const { return m_screenRefs.value(m_prompterDisplay).data(); }
    bool editingScript() const { return m_editingScript; }
    Q_INVOKABLE void setEditingScript(bool editing);
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
    // Edit mode opens the preview after recording; quick mode saves right
    // away. Where the platform records direct MP4 only, it is always quick.
    bool editMode() const;
    void setEditMode(bool edit);
    bool recordingModeSupported() const;
    int quickSaveAction() const;
    void setQuickSaveAction(int action);
    // Choose an area before recording (#194).
    bool preselectArea() const;
    bool autoZoomOnOpen() const;
    void setPreselectArea(bool value);
    void setAutoZoomOnOpen(bool value);
    QString audioNote() const;
    bool showSetupBeforeRecording() const;
    bool keyRecordingSupported() const;
    bool recordShortcutKeys() const;
    void setRecordShortcutKeys(bool value);
    bool recordTypedText() const;
    void setRecordTypedText(bool value);
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
    void prompterChanged();
    void editingScriptChanged(bool editing);
    void thumbnailCacheBusterChanged();
    void audioSettingsChanged();
    void devicesChanged();
    void showSetupBeforeRecordingChanged();
    void keyRecordingChanged();
    void recordingModeChanged();
    void microphoneTestChanged();
    void microphoneLevelChanged();

private:
    void clearThumbnails();
    void loadAudioSettings();
    void saveAudioSettings();

    int m_prompterDisplay = -1;
    QString m_prompterScript;
    QString m_prompterError;
    bool m_editingScript = false;
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
