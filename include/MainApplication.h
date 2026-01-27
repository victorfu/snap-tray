#ifndef MAINAPPLICATION_H
#define MAINAPPLICATION_H

#include "hotkey/HotkeyTypes.h"

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QPointer>

class QSystemTrayIcon;
class QMenu;
class QAction;
class CaptureManager;
class PinWindowManager;
class ScreenCanvasManager;
class RecordingManager;
class SettingsDialog;
class RecordingPreviewWindow;
class UpdateChecker;
struct ReleaseInfo;

class MainApplication : public QObject
{
    Q_OBJECT

public:
    explicit MainApplication(QObject *parent = nullptr);
    ~MainApplication();

    void initialize();

public slots:
    void activate();
    void handleCLICommand(const QByteArray& commandData);

private slots:
    void onRegionCapture();
    void onQuickPin();
    void onScreenCanvas();
    void onPasteFromClipboard();
    void onFullScreenRecording();
    void onCloseAllPins();
    void onPinFromImage();
    void onImageLoaded(const QString &filePath, const QImage &image);
    void onSettings();
    void showRecordingPreview(const QString &videoPath);
    void onPreviewSaveRequested(const QString &videoPath);
    void onPreviewDiscardRequested();
    void onHotkeyAction(SnapTray::HotkeyAction action);
    void onUpdateAvailable(const ReleaseInfo& release);

private:
    void updateTrayMenuHotkeyText();
    QPixmap renderTextToPixmap(const QString &text);

    QSystemTrayIcon *m_trayIcon;
    QMenu *m_trayMenu;
    CaptureManager *m_captureManager;
    PinWindowManager *m_pinWindowManager;
    ScreenCanvasManager *m_screenCanvasManager;
    RecordingManager *m_recordingManager;
    QAction *m_regionCaptureAction;
    QAction *m_screenCanvasAction;
    QAction *m_pinFromImageAction;
    QAction *m_fullScreenRecordingAction;
    SettingsDialog *m_settingsDialog;
    QPointer<RecordingPreviewWindow> m_previewWindow;
    UpdateChecker *m_updateChecker;
};

#endif // MAINAPPLICATION_H
