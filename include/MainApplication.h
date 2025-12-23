#ifndef MAINAPPLICATION_H
#define MAINAPPLICATION_H

#include <QObject>

class QSystemTrayIcon;
class QMenu;
class QAction;
class QHotkey;
class CaptureManager;
class PinWindowManager;
class ScreenCanvasManager;
class RecordingManager;
class SettingsDialog;

class MainApplication : public QObject
{
    Q_OBJECT

public:
    explicit MainApplication(QObject *parent = nullptr);
    ~MainApplication();

    void initialize();

private slots:
    void onRegionCapture();
    void onScreenCanvas();
    void onScreenRecording();
    void onCloseAllPins();
    void onSettings();

public:
    bool updateHotkey(const QString &newHotkey);
    bool updateScreenCanvasHotkey(const QString &newHotkey);

private:
    void setupHotkey();
    void updateTrayMenuHotkeyText(const QString &hotkey);

    QSystemTrayIcon *m_trayIcon;
    QMenu *m_trayMenu;
    QHotkey *m_regionHotkey;
    QHotkey *m_screenCanvasHotkey;
    QHotkey *m_recordingHotkey;
    CaptureManager *m_captureManager;
    PinWindowManager *m_pinWindowManager;
    ScreenCanvasManager *m_screenCanvasManager;
    RecordingManager *m_recordingManager;
    QAction *m_regionCaptureAction;
    QAction *m_screenCanvasAction;
    QAction *m_screenRecordingAction;
    SettingsDialog *m_settingsDialog;
};

#endif // MAINAPPLICATION_H
