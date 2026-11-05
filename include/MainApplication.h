#ifndef MAINAPPLICATION_H
#define MAINAPPLICATION_H

#include "hotkey/HotkeyTypes.h"
#include "hotkey/HotkeyHoldFilter.h"
#include "ui/TrayRecordingIndicator.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QList>
#include <QObject>
#include <QPixmap>
#include <QPointer>
#include <QSize>
#include <QStringList>
#include <functional>
#include <optional>

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
class QmlRecordingAreaSelector;
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
    void onFullScreenCapture();
    void onDelayedCaptureHotkey();
    void onCancelDelayedCapture();
    void onDelayedCaptureStateChanged(bool pending);
    void onQuickPin();
    void onScreenCanvas();
    void onPasteFromClipboard();
    void onCheckForUpdates();
    void onFullScreenRecording();
    void onToggleAllPinsVisibility();
    void onCloseAllPins();
    void onPinFromImage();
    void onHistoryWindow();
    void onSettings();
    void showRecordingPreview(const QString &videoPath, int defaultOutputFormat, bool recordedAsIntermediate);
    void onPreviewSaveRequested(const QString &videoPath, const QSize &outputSize);
    void onPreviewDiscardRequested(const QString &videoPath);
    void onHotkeyAction(SnapTray::HotkeyAction action);
    void onHotkeyChanged(SnapTray::HotkeyAction action, const SnapTray::HotkeyConfig& config);
    void onHotkeyInitializationCompleted(const QStringList& failedHotkeys);

private:
    friend class tst_MainApplicationTrayMenu;
    void pinFromClipboard(std::optional<QPoint> position = std::nullopt);
    void loadImageForPin(const QString& filePath, std::optional<QPoint> position = std::nullopt);
    void onImageLoaded(const QString& filePath, const QImage& image,
                       std::optional<QPoint> position = std::nullopt);

    void startRegionCapture(bool showShortcutHintsOnEntry);
    void startDelayedCapture(int delaySeconds);
    void updateDelayedCaptureMenu();
    void runDeferredCLICommands();
    bool canStartRegionCapture() const;
    bool startHistoryReplay(const QString& entryId);
    std::function<bool(const QString&)> m_historyReplayStarter;
    bool canShutdownForUpdate() const;
    void prepareForUpdateShutdown();
    void updateTrayMenuHotkeyText();
    void updateTrayToolTip();
    void updateTrayRecordingIndicator();
    void onRecordingDurationChanged(qint64 elapsedMs);
    QString trayRecordingStatusLine() const;
    void updatePinsVisibilityActionText();
    void updateRecordingActionText();
    void showRecoveryDialog();
    void showRecordingLibrary();
    void openRecordingProject(const QString& projectDir);
    void updateRecoveryAction();
    bool acceptRecordingHotkey(SnapTray::HotkeyAction action);
    SnapTray::QmlSettingsWindow* ensureSettingsWindow();
    void startScreenRecordingFlow(QScreen* preferredScreen = nullptr);
    void attachScreenPicker(SnapTray::QmlDialog* dialog,
                            SnapTray::ScreenPickerViewModel* viewModel);
    void onScreenPickerChosen(QScreen* screen);
    // Starts recording `screen`, after choosing an area when that is on (#194).
    void startRecordingOn(QScreen* screen);
    // The recording setup is on screen: the screen picker, or the area
    // selector after it (#194). Captures and other overlays wait.
    bool recordingSetupActive() const;
    void closeAreaSelector();
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
    QAction *m_fullScreenCaptureAction = nullptr;
    QMenu *m_delayedCaptureMenu = nullptr;
    QList<QAction*> m_delayedCapturePresetActions;
    QAction *m_cancelDelayedCaptureAction = nullptr;
    QAction *m_screenCanvasAction;
    QAction *m_pasteAction;
    QAction *m_pinFromImageAction;
    QAction *m_historyAction;
    QAction *m_togglePinsVisibilityAction;
    QAction *m_closeAllPinsAction;
    QAction *m_disableClickThroughAction = nullptr;
    QAction *m_fullScreenRecordingAction;
    QAction *m_pauseRecordingAction = nullptr;
    QAction *m_showRecordingControlsAction = nullptr;
    QAction *m_copyRecordingDiagnosticsAction = nullptr;
    QAction *m_recoverRecordingsAction = nullptr;
    QPointer<SnapTray::QmlDialog> m_recoveryDialog;
    QAction *m_recordingLibraryAction = nullptr;
    QStringList m_recoverablePaths;  // left-behind recordings still offered
    QAction *m_checkForUpdatesAction;
    QPointer<SnapTray::QmlSettingsWindow> m_settingsWindow;
    QPointer<SnapTray::QmlHistoryWindow> m_historyWindow;
    QPointer<SnapTray::QmlDialog> m_screenPickerDialog;
    QPointer<SnapTray::QmlRecordingAreaSelector> m_areaSelector;
    QPointer<SnapTray::ScreenPickerViewModel> m_screenPickerViewModel;
    RecordingPreviewBackend *m_previewBackend = nullptr;
    // Tray artwork without a recording badge, and the state currently shown on it.
    QPixmap m_trayBasePixmap;
    SnapTray::TrayRecordingStatus m_trayRecordingStatus = SnapTray::TrayRecordingStatus::Idle;
    // Effective (pause-excluded) duration shown in the tray tooltip; -1 when none.
    qint64 m_trayRecordingElapsedMs = -1;
    // Recording warnings (#170 review). The toast cannot be kept out of the
    // capture on every platform, and one shown while capturing would be
    // burned into the video: warnings wait in the tray tooltip and are shown
    // when the recording stops.
    bool holdsRecordingWarnings() const;
    void showRecordingWarning(const QString& warning);
    void flushHeldRecordingWarnings();
    QStringList m_heldRecordingWarnings;
    std::function<void(const QString& title, const QString& text)> m_recordingWarningToast;
    // A held recording hotkey counts once (#147 review): on m_recordingHotkeyClock.
    QElapsedTimer m_recordingHotkeyClock;
    SnapTray::HotkeyHoldFilter m_recordingHotkeyHolds;
    // CLI commands received while a full screen capture was still exporting.
    QList<QByteArray> m_deferredCLICommands;
};

#endif // MAINAPPLICATION_H
