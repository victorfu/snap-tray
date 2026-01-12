#ifndef MAINAPPLICATION_H
#define MAINAPPLICATION_H

#include <QObject>
#include <QPointer>

class QSystemTrayIcon;
class QMenu;
class QAction;
class QHotkey;
class CaptureManager;
class PinWindowManager;
class ScreenCanvasManager;
class RecordingManager;
class SettingsDialog;
class RecordingPreviewWindow;

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
    void onPasteFromClipboard();
    void onFullScreenRecording();
    void onCloseAllPins();
    void onSettings();
    void showRecordingPreview(const QString &videoPath);
    void showRecordingPreviewForAnnotation(const QString& videoPath);
    void onPreviewSaveRequested(const QString &videoPath);
    void onPreviewDiscardRequested();

public:
    bool updateHotkey(const QString &newHotkey);
    bool updateScreenCanvasHotkey(const QString &newHotkey);
    bool updatePasteHotkey(const QString &newHotkey);

private:
    void setupHotkey();
    void updateTrayMenuHotkeyText(const QString &hotkey);

    QSystemTrayIcon *m_trayIcon;
    QMenu *m_trayMenu;
    QHotkey *m_regionHotkey;
    QHotkey *m_screenCanvasHotkey;
    QHotkey *m_pasteHotkey;
    CaptureManager *m_captureManager;
    PinWindowManager *m_pinWindowManager;
    ScreenCanvasManager *m_screenCanvasManager;
    RecordingManager *m_recordingManager;
    QAction *m_regionCaptureAction;
    QAction *m_screenCanvasAction;
    QAction *m_fullScreenRecordingAction;
    SettingsDialog *m_settingsDialog;
    QPointer<RecordingPreviewWindow> m_previewWindow;
};

#endif // MAINAPPLICATION_H
