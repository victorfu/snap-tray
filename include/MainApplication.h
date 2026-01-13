#ifndef MAINAPPLICATION_H
#define MAINAPPLICATION_H

#include <QObject>
#include <QPointer>
#include <QPixmap>

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

public slots:
    void activate();

private slots:
    void onRegionCapture();
    void onQuickPin();
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
    bool updateQuickPinHotkey(const QString &newHotkey);

private:
    void setupHotkey();
    void updateTrayMenuHotkeyText(const QString &hotkey);
    QPixmap renderTextToPixmap(const QString &text);

    QSystemTrayIcon *m_trayIcon;
    QMenu *m_trayMenu;
    QHotkey *m_regionHotkey;
    QHotkey *m_quickPinHotkey;
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
