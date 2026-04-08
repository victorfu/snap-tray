#ifndef MAINAPPLICATION_H
#define MAINAPPLICATION_H

#include "hotkey/HotkeyTypes.h"

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QPointer>
#include <QStringList>

class QSystemTrayIcon;
class QMenu;
class QAction;
class CaptureManager;
class PinWindowManager;
class ScreenCanvasManager;
class RecordingManager;
class RecordingPreviewBackend;
class QScreen;

namespace SnapTray {
class QmlSettingsWindow;
class QmlHistoryWindow;
class QmlDialog;
class ScreenPickerViewModel;
}

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
    void onCheckForUpdates();
    void onFullScreenRecording();
    void onToggleAllPinsVisibility();
    void onCloseAllPins();
    void onPinFromImage();
    void onHistoryWindow();
    void onImageLoaded(const QString &filePath, const QImage &image);
    void onSettings();
    void showRecordingPreview(const QString &videoPath, int defaultOutputFormat);
    void onPreviewSaveRequested(const QString &videoPath);
    void onPreviewDiscardRequested(const QString &videoPath);
    void onHotkeyAction(SnapTray::HotkeyAction action);
    void onHotkeyChanged(SnapTray::HotkeyAction action, const SnapTray::HotkeyConfig& config);
    void onHotkeyInitializationCompleted(const QStringList& failedHotkeys);

private:
    friend class tst_MainApplicationTrayMenu;

    void startRegionCapture(bool showShortcutHintsOnEntry);
    bool canShutdownForUpdate() const;
    void prepareForUpdateShutdown();
    void updateTrayMenuHotkeyText();
    void updateTrayToolTip();
    void updatePinsVisibilityActionText();
    void updateRecordingActionText();
    SnapTray::QmlSettingsWindow* ensureSettingsWindow();
    void startScreenRecordingFlow(QScreen* preferredScreen = nullptr);
    void attachScreenPicker(SnapTray::QmlDialog* dialog,
                            SnapTray::ScreenPickerViewModel* viewModel);
    void onScreenPickerChosen(QScreen* screen);
    void onScreenPickerCancelled();
    void onScreenPickerClosed(SnapTray::QmlDialog* dialog);
    void closeScreenPicker();
    void updateActionHotkeyText(QAction* action,
                                SnapTray::HotkeyAction hotkeyAction,
                                const QString& baseName);
    QPixmap renderTextToPixmap(const QString &text);

    QSystemTrayIcon *m_trayIcon;
    QMenu *m_trayMenu;
    CaptureManager *m_captureManager;
    PinWindowManager *m_pinWindowManager;
    ScreenCanvasManager *m_screenCanvasManager;
    RecordingManager *m_recordingManager;
    QAction *m_regionCaptureAction;
    QAction *m_screenCanvasAction;
    QAction *m_pasteAction;
    QAction *m_pinFromImageAction;
    QAction *m_historyAction;
    QAction *m_togglePinsVisibilityAction;
    QAction *m_closeAllPinsAction;
    QAction *m_fullScreenRecordingAction;
    QAction *m_checkForUpdatesAction;
    QPointer<SnapTray::QmlSettingsWindow> m_settingsWindow;
    QPointer<SnapTray::QmlHistoryWindow> m_historyWindow;
    QPointer<SnapTray::QmlDialog> m_screenPickerDialog;
    QPointer<SnapTray::ScreenPickerViewModel> m_screenPickerViewModel;
    RecordingPreviewBackend *m_previewBackend = nullptr;
};

#endif // MAINAPPLICATION_H
