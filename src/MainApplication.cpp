#include "MainApplication.h"

#include "CaptureManager.h"
#include "PinWindow.h"
#include "PinWindowManager.h"
#include "PlatformFeatures.h"
#include "RecordingManager.h"
#include "recording/ScreenSourceService.h"
#include "ScreenCanvasManager.h"
#include "qml/QmlDialog.h"
#include "qml/QmlHistoryWindow.h"
#include "qml/ScreenPickerViewModel.h"
#include "qml/RecordingRecoveryViewModel.h"
#include "qml/QmlSettingsWindow.h"
#include "pinwindow/PinWindowPlacement.h"
#include "ImageColorSpaceHelper.h"
#include "cli/IPCProtocol.h"
#include "hotkey/HotkeyManager.h"
#include "qml/QmlToast.h"
#include "qml/RecordingPreviewBackend.h"
#include "recording/WindowTimelineSidecar.h"
#include "recording/RecordingSession.h"
#include "settings/RecordingSettingsManager.h"
#include "ui/TrayRecordingIndicator.h"
#include "ui/TrayTooltipFormatter.h"
#include "update/InstallSourceDetector.h"
#include "update/UpdateCoordinator.h"
#include "utils/CoordinateHelper.h"
#ifdef Q_OS_MACOS
#include "platform/MacTrayActivationGuard.h"
#endif

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QApplication>
#include <QScreen>
#include <QTimer>
#include <QDebug>
#include <QClipboard>
#include <QMimeData>
#include <QFileDialog>
#include <QStandardPaths>
#include <QFileInfo>
#include <QImageReader>
#include <QFutureWatcher>
#include <QPainter>
#include <QFont>
#include <QFontMetrics>
#include <QtConcurrent>

#include <algorithm>
#include <utility>

namespace {

SnapTray::TrayRecordingStatus trayRecordingStatusForState(RecordingManager::State state)
{
    using State = RecordingManager::State;
    using Status = SnapTray::TrayRecordingStatus;
    switch (state) {
    case State::Preparing: return Status::Preparing;
    case State::Countdown: return Status::Countdown;
    case State::Recording: return Status::Recording;
    case State::Paused: return Status::Paused;
    case State::Encoding: return Status::Finalizing;
    case State::Idle:
    case State::Previewing:
        return Status::Idle;
    }
    return Status::Idle;
}

constexpr qint64 kTrayElapsedResolutionMs = 1000;
// Recording hotkeys repeated faster than this are treated as one press.
// Text pixmap rendering constants (for paste-as-text feature)
constexpr int kMaxTextPixmapWidth = 800;
constexpr int kMaxTextPixmapHeight = 600;
constexpr int kTextPixmapPadding = 16;
const QColor kStickyNoteBackground{255, 255, 240};  // Light yellow
const QColor kTextAnnotationForeground{40, 40, 40}; // Dark gray text

// Toast timeout
constexpr int kUIToastTimeout = 2000;

bool isBlockingUpdateRecordingState(RecordingManager::State state)
{
    switch (state) {
    case RecordingManager::State::Recording:
    case RecordingManager::State::Paused:
    case RecordingManager::State::Encoding:
    case RecordingManager::State::Previewing:
        return true;
    case RecordingManager::State::Idle:
    case RecordingManager::State::Preparing:
    case RecordingManager::State::Countdown:
        return false;
    }

    return false;
}

QString hotkeyDisplayText(const SnapTray::HotkeyConfig& config, const QString& fallbackText)
{
    const QString displayHotkey = SnapTray::HotkeyManager::formatKeySequence(config.keySequence);
    switch (config.status) {
    case SnapTray::HotkeyStatus::Registered:
        return displayHotkey.isEmpty() ? fallbackText : displayHotkey;
    case SnapTray::HotkeyStatus::Failed:
        return displayHotkey.isEmpty()
            ? fallbackText
            : QCoreApplication::translate("MainApplication", "%1 unavailable").arg(displayHotkey);
    case SnapTray::HotkeyStatus::Disabled:
        return QCoreApplication::translate("MainApplication", "Disabled");
    case SnapTray::HotkeyStatus::Unset:
        return fallbackText;
    }

    return fallbackText;
}

QString hotkeyDisplayTextOrFallback(SnapTray::HotkeyAction action, const QString& fallbackText)
{
    return hotkeyDisplayText(SnapTray::HotkeyManager::instance().getConfig(action), fallbackText);
}
}

MainApplication::MainApplication(QObject* parent)
    : QObject(parent)
    , m_trayIcon(nullptr)
    , m_trayMenu(nullptr)
    , m_captureManager(nullptr)
    , m_pinWindowManager(nullptr)
    , m_screenCanvasManager(nullptr)
    , m_recordingManager(nullptr)
    , m_regionCaptureAction(nullptr)
    , m_screenCanvasAction(nullptr)
    , m_pasteAction(nullptr)
    , m_pinFromImageAction(nullptr)
    , m_historyAction(nullptr)
    , m_togglePinsVisibilityAction(nullptr)
    , m_closeAllPinsAction(nullptr)
    , m_fullScreenRecordingAction(nullptr)
    , m_checkForUpdatesAction(nullptr)
    , m_screenPickerDialog(nullptr)
    , m_screenPickerViewModel(nullptr)
{
    m_historyReplayStarter = [this](const QString& entryId) {
        return m_captureManager && m_captureManager->startHistoryReplay(entryId);
    };
}

MainApplication::~MainApplication()
{
    UpdateCoordinator::setShutdownHooks(UpdateCoordinator::ShutdownGuardFn(),
                                        UpdateCoordinator::ShutdownRequestFn());

    // Shutdown hotkey manager to unregister all hotkeys before destruction
    SnapTray::HotkeyManager::instance().shutdown();

    closeScreenPicker();
    delete m_trayMenu;
}

void MainApplication::activate()
{
    if (m_trayIcon) {
        m_trayIcon->showMessage(
            QStringLiteral("SnapTray"),
            tr("SnapTray is already running"),
            QSystemTrayIcon::Information,
            kUIToastTimeout);
    }
}

void MainApplication::handleCLICommand(const QByteArray& commandData)
{
    using namespace SnapTray::CLI;

    IPCMessage msg = IPCMessage::fromJson(commandData);
    qDebug() << "CLI command received:" << msg.command;
    qDebug() << "  captureManager->isActive():" << m_captureManager->isActive();
    qDebug() << "  screenCanvasManager->isActive():" << m_screenCanvasManager->isActive();
    qDebug() << "  recordingManager->isActive():" << m_recordingManager->isActive();
    qDebug() << "  screenPickerDialog active:" << static_cast<bool>(m_screenPickerDialog);

    // CLI commands should preempt existing capture mode
    // This ensures CLI commands work reliably even if RegionSelector is stuck
    if (msg.command != "gui" && m_captureManager->isActive()) {
        qDebug() << "CLI: Cancelling active capture to process new command";
        m_captureManager->cancelCapture();
    }

    // A full screen capture that already grabbed the screen is still copying or
    // saving and cannot be cancelled. Run the command once it finishes.
    if (m_captureManager->isFullScreenCaptureInProgress()) {
        qDebug() << "CLI: Deferring command until full screen capture finishes";
        m_deferredCLICommands.append(commandData);
        return;
    }

    if (msg.command == "gui") {
        int delay = msg.options["delay"].toInt();
        if (delay > 0) {
            QTimer::singleShot(delay, this, &MainApplication::onRegionCapture);
        }
        else {
            onRegionCapture();
        }
    }
    else if (msg.command == "canvas") {
        onScreenCanvas();
    }
    else if (msg.command == "pin") {
        std::optional<QPoint> position;
        if (msg.options.contains("x") && msg.options.contains("y")) {
            position = QPoint(msg.options["x"].toInt(), msg.options["y"].toInt());
        }
        if (msg.options["clipboard"].toBool()) {
            pinFromClipboard(position);
        }
        else if (msg.options.contains("file")) {
            loadImageForPin(msg.options["file"].toString(), position);
        }
    }
    else if (msg.command == "config") {
        onSettings();
    }
    else {
        qWarning() << "Unknown CLI command:" << msg.command;
    }
}

void MainApplication::runDeferredCLICommands()
{
    const QList<QByteArray> commands = std::exchange(m_deferredCLICommands, {});
    for (const QByteArray& command : commands) {
        handleCLICommand(command);
    }
}

void MainApplication::initialize()
{
    // Create pin window manager first (needed by capture manager)
    m_pinWindowManager = new PinWindowManager(this);

    // Create capture manager
    m_captureManager = new CaptureManager(m_pinWindowManager, this);

    connect(m_captureManager, &CaptureManager::fullScreenCaptureFinished,
            this, &MainApplication::runDeferredCLICommands, Qt::QueuedConnection);

    // Create screen canvas manager
    m_screenCanvasManager = new ScreenCanvasManager(this);

    // Create recording manager
    m_recordingManager = new RecordingManager(this);

    // Connect recording signals
    connect(m_recordingManager, &RecordingManager::recordingStopped,
        this, [](const QString& path) {
            SnapTray::QmlToast::screenToast().showToast(
                SnapTray::QmlToast::Level::Success,
                MainApplication::tr("Recording Saved"),
                MainApplication::tr("Saved to: %1").arg(path));
        });

    connect(m_recordingManager, &RecordingManager::recordingError,
        this, [](const QString& error) {
            SnapTray::QmlToast::screenToast().showToast(
                SnapTray::QmlToast::Level::Error,
                MainApplication::tr("Recording Error"),
                error, 5000);
        });
    connect(m_recordingManager, &RecordingManager::recordingWarning,
        this, &MainApplication::showRecordingWarning);

    // Connect screenshot save signals from CaptureManager
    connect(m_captureManager, &CaptureManager::saveCompleted,
        this, [](const QPixmap&, const QString& path) {
            SnapTray::QmlToast::screenToast().showToast(
                SnapTray::QmlToast::Level::Success,
                MainApplication::tr("Screenshot Saved"),
                MainApplication::tr("Saved to: %1").arg(path));
        });
    connect(m_captureManager, &CaptureManager::saveFailed,
        this, [](const QString& path, const QString& error) {
            SnapTray::QmlToast::screenToast().showToast(
                SnapTray::QmlToast::Level::Error,
                MainApplication::tr("Screenshot Save Failed"),
                MainApplication::tr("%1\n%2").arg(error).arg(path), 5000);
        });
    connect(m_captureManager, &CaptureManager::copyCompleted,
        this, [](const QPixmap&) {
            SnapTray::QmlToast::screenToast().showToast(
                SnapTray::QmlToast::Level::Success,
                MainApplication::tr("Screenshot Copied"),
                MainApplication::tr("Copied to clipboard"));
        });
    connect(m_captureManager, &CaptureManager::copyFailed,
        this, []() {
            SnapTray::QmlToast::screenToast().showToast(
                SnapTray::QmlToast::Level::Error,
                MainApplication::tr("Copy failed"),
                QString(), 5000);
        });

    // Connect screenshot save signals from PinWindowManager
    connect(m_pinWindowManager, &PinWindowManager::saveCompleted,
        this, [](const QPixmap&, const QString& path) {
            SnapTray::QmlToast::screenToast().showToast(
                SnapTray::QmlToast::Level::Success,
                MainApplication::tr("Screenshot Saved"),
                MainApplication::tr("Saved to: %1").arg(path));
        });
    connect(m_pinWindowManager, &PinWindowManager::saveFailed,
        this, [](const QString& path, const QString& error) {
            SnapTray::QmlToast::screenToast().showToast(
                SnapTray::QmlToast::Level::Error,
                MainApplication::tr("Screenshot Save Failed"),
                MainApplication::tr("%1\n%2").arg(error).arg(path), 5000);
        });

    // Connect preview request signal
    connect(m_recordingManager, &RecordingManager::previewRequested,
        this, &MainApplication::showRecordingPreview);

    connect(m_recordingManager, &RecordingManager::stateChanged,
        this, [this](RecordingManager::State state) {
            switch (state) {
            case RecordingManager::State::Preparing:
            case RecordingManager::State::Countdown:
            case RecordingManager::State::Recording:
            case RecordingManager::State::Paused:
            case RecordingManager::State::Encoding:
            case RecordingManager::State::Previewing:
                break;
            case RecordingManager::State::Idle:
                break;
            }

            updateRecordingActionText();
            updateTrayRecordingIndicator();
            if (!holdsRecordingWarnings()) {
                flushHeldRecordingWarnings();
            }
        });
    connect(m_recordingManager, &RecordingManager::durationChanged,
        this, &MainApplication::onRecordingDurationChanged);
    connect(m_recordingManager, &RecordingManager::audioStatusChanged,
        this, &MainApplication::updateTrayToolTip);
    connect(m_recordingManager, &RecordingManager::droppingFramesChanged,
        this, &MainApplication::updateTrayToolTip);
    connect(m_recordingManager, &RecordingManager::lowDiskSpaceChanged,
        this, &MainApplication::updateTrayToolTip);
    connect(m_recordingManager, &RecordingManager::diagnosticsAvailable,
        this, &MainApplication::updateRecordingActionText);
    connect(m_recordingManager, &RecordingManager::recoveriesFound, this,
        [this](const QList<SnapTray::RecordingSession::Recoverable>& found) {
            m_recoverablePaths.clear();
            int recoverable = 0;
            for (const auto& item : found) {
                m_recoverablePaths.append(item.info.videoPath);
                if (item.decodable) ++recoverable;
            }
            updateRecoveryAction();
            if (recoverable > 0) {
                SnapTray::QmlToast::screenToast().showToast(
                    SnapTray::QmlToast::Level::Info,
                    tr("Unsaved Recordings Found"),
                    tr("SnapTray found %1 recording(s) that were not saved. Recover them from the tray menu.")
                        .arg(recoverable),
                    8000);
            }
        });

    // Create system tray icon
    m_trayBasePixmap = PlatformFeatures::instance().createTrayPixmap();
    m_trayIcon = new QSystemTrayIcon(QIcon(m_trayBasePixmap), this);

    // Create context menu
    m_trayMenu = new QMenu();

    m_regionCaptureAction = m_trayMenu->addAction(tr("Region Capture"));
    connect(m_regionCaptureAction, &QAction::triggered, this, &MainApplication::onRegionCapture);

    m_fullScreenCaptureAction = m_trayMenu->addAction(tr("Full Screen Capture"));
    connect(m_fullScreenCaptureAction, &QAction::triggered,
            this, &MainApplication::onFullScreenCapture);

    m_screenCanvasAction = m_trayMenu->addAction(tr("Screen Canvas"));
    connect(m_screenCanvasAction, &QAction::triggered, this, &MainApplication::onScreenCanvas);

    m_pasteAction = m_trayMenu->addAction(tr("Paste"));
    connect(m_pasteAction, &QAction::triggered, this, &MainApplication::onPasteFromClipboard);

    m_pinFromImageAction = m_trayMenu->addAction(tr("Pin from Image..."));
    connect(m_pinFromImageAction, &QAction::triggered, this, &MainApplication::onPinFromImage);

    m_historyAction = m_trayMenu->addAction(tr("History"));
    connect(m_historyAction, &QAction::triggered, this, &MainApplication::onHistoryWindow);

    m_togglePinsVisibilityAction = m_trayMenu->addAction(tr("Hide All Pins"));
    connect(m_togglePinsVisibilityAction, &QAction::triggered, this, &MainApplication::onToggleAllPinsVisibility);

    m_closeAllPinsAction = m_trayMenu->addAction(tr("Close All Pins"));
    connect(m_closeAllPinsAction, &QAction::triggered, this, &MainApplication::onCloseAllPins);

    connect(m_pinWindowManager, &PinWindowManager::windowCreated,
            this, [this](PinWindow*) { updatePinsVisibilityActionText(); });
    connect(m_pinWindowManager, &PinWindowManager::windowClosed,
            this, [this](PinWindow*) { updatePinsVisibilityActionText(); });
    connect(m_pinWindowManager, &PinWindowManager::allWindowsClosed,
            this, &MainApplication::updatePinsVisibilityActionText);
    connect(m_pinWindowManager, &PinWindowManager::allPinsVisibilityChanged,
            this, [this](bool) { updatePinsVisibilityActionText(); });

    if (PlatformFeatures::instance().capabilities().supportsRecording) {
        m_fullScreenRecordingAction = m_trayMenu->addAction(tr("Record Screen"));
        connect(m_fullScreenRecordingAction,
                &QAction::triggered,
                this,
                &MainApplication::onFullScreenRecording);
        m_pauseRecordingAction = m_trayMenu->addAction(QCoreApplication::translate("RecordingControlBar", "Pause Recording"));
        m_pauseRecordingAction->setVisible(false);
        connect(m_pauseRecordingAction, &QAction::triggered, this, [this] {
            if (m_recordingManager) m_recordingManager->togglePause();
        });
        // Recordings an earlier run left behind (crash, power loss, quit with
        // an unsaved preview). Shown only when there is something to offer.
        m_recoverRecordingsAction = m_trayMenu->addAction(tr("Recover Recordings..."));
        m_recoverRecordingsAction->setVisible(false);
        connect(m_recoverRecordingsAction, &QAction::triggered, this, &MainApplication::showRecoveryDialog);
        // Local diagnostics for the last recording; only copied when the user asks.
        m_copyRecordingDiagnosticsAction = m_trayMenu->addAction(tr("Copy Recording Diagnostics"));
        m_copyRecordingDiagnosticsAction->setVisible(false);
        connect(m_copyRecordingDiagnosticsAction, &QAction::triggered, this, [this] {
            if (!m_recordingManager || m_recordingManager->lastDiagnostics().isEmpty()) return;
            QGuiApplication::clipboard()->setText(m_recordingManager->lastDiagnostics());
            SnapTray::QmlToast::screenToast().showToast(
                SnapTray::QmlToast::Level::Info,
                tr("Diagnostics Copied"),
                tr("The recording diagnostics are on the clipboard. They contain no images, audio or file names."));
        });
        // Platforms that keep active controls in the tray never show the bar while recording.
        if (!PlatformFeatures::instance().capabilities().recordingControlsInTray) {
            m_showRecordingControlsAction = m_trayMenu->addAction(tr("Show Recording Controls"));
            m_showRecordingControlsAction->setVisible(false);
            connect(m_showRecordingControlsAction, &QAction::triggered, this, [this] {
                if (m_recordingManager) m_recordingManager->showRecordingControls();
            });
        }
    }

    m_trayMenu->addSeparator();

    m_checkForUpdatesAction = m_trayMenu->addAction(tr("Check for Updates"));
    m_checkForUpdatesAction->setEnabled(!UpdateCoordinator::instance().isExternallyManaged());
    connect(m_checkForUpdatesAction, &QAction::triggered, this, &MainApplication::onCheckForUpdates);

    QAction* settingsAction = m_trayMenu->addAction(tr("Settings"));
    connect(settingsAction, &QAction::triggered, this, &MainApplication::onSettings);

    m_trayMenu->addSeparator();

    QAction* exitAction = m_trayMenu->addAction(tr("Exit"));
    connect(exitAction, &QAction::triggered, qApp, &QCoreApplication::quit);

    // Set menu and show tray icon
#ifdef Q_OS_MACOS
    SnapTray::installMacTrayActivationGuard();
#endif
    m_trayIcon->setContextMenu(m_trayMenu);
    m_trayIcon->setToolTip(tr("SnapTray - Screenshot Utility"));
    m_trayIcon->show();

    // Connect OCR completion signal (must be after m_trayIcon is created)
    connect(m_pinWindowManager, &PinWindowManager::ocrCompleted,
        this, [](bool success, const QString& message) {
            SnapTray::QmlToast::screenToast().showToast(
                success ? SnapTray::QmlToast::Level::Success
                        : SnapTray::QmlToast::Level::Error,
                success ? MainApplication::tr("OCR Success")
                        : MainApplication::tr("OCR Failed"),
                message);
        });

    // Connect hotkey signals before initialization so startup events are not missed.
    auto& hotkeyManager = SnapTray::HotkeyManager::instance();
    connect(&hotkeyManager, &SnapTray::HotkeyManager::actionTriggered,
            this, &MainApplication::onHotkeyAction);
    connect(&hotkeyManager, &SnapTray::HotkeyManager::actionReleased, this,
            [this](SnapTray::HotkeyAction action) { m_recordingHotkeyHolds.release(static_cast<int>(action)); });
    connect(&hotkeyManager, &SnapTray::HotkeyManager::hotkeyChanged,
            this, &MainApplication::onHotkeyChanged);
    connect(&hotkeyManager, &SnapTray::HotkeyManager::initializationCompleted,
            this, &MainApplication::onHotkeyInitializationCompleted);
    hotkeyManager.initialize();

    // Update tray menu with current hotkey text
    updateTrayMenuHotkeyText();
    updateTrayToolTip();
    updatePinsVisibilityActionText();

    // Look for recordings an earlier run could not save (off the GUI thread).
    if (PlatformFeatures::instance().capabilities().supportsRecording) {
        m_recordingManager->scanForRecoveries();
    }

    QTimer::singleShot(0, this, [captureManager = QPointer<CaptureManager>(m_captureManager)]() {
        if (!captureManager) {
            return;
        }
        QMetaObject::invokeMethod(captureManager, "prewarmWindowDetector", Qt::DirectConnection);
    });

    UpdateCoordinator::setShutdownHooks(
        [this]() { return canShutdownForUpdate(); },
        [this]() { prepareForUpdateShutdown(); });
    UpdateCoordinator::instance().startAutomaticChecks();

}

bool MainApplication::canShutdownForUpdate() const
{
    return !m_screenPickerDialog &&
           (!m_recordingManager || !isBlockingUpdateRecordingState(m_recordingManager->state()));
}

void MainApplication::prepareForUpdateShutdown()
{
    if (m_recordingManager && isBlockingUpdateRecordingState(m_recordingManager->state())) {
        qWarning() << "MainApplication: Refusing updater shutdown while recording work is active";
        return;
    }

    if (m_captureManager && m_captureManager->isActive()) {
        m_captureManager->cancelCapture();
    }

    closeScreenPicker();

    if (m_screenCanvasManager && m_screenCanvasManager->isActive()) {
        m_screenCanvasManager->toggle();
    }

    if (m_recordingManager) {
        switch (m_recordingManager->state()) {
        case RecordingManager::State::Preparing:
        case RecordingManager::State::Countdown:
            m_recordingManager->cancelRecording();
            break;
        case RecordingManager::State::Idle:
        case RecordingManager::State::Recording:
        case RecordingManager::State::Paused:
        case RecordingManager::State::Encoding:
        case RecordingManager::State::Previewing:
            break;
        }
    }

    QCoreApplication::quit();
}

bool MainApplication::canStartRegionCapture() const
{
    return m_captureManager && !m_screenPickerDialog
        && (!m_screenCanvasManager || !m_screenCanvasManager->isActive())
        && (!m_recordingManager || !m_recordingManager->blocksCapture());
}

bool MainApplication::startHistoryReplay(const QString& entryId)
{
    return canStartRegionCapture() && m_historyReplayStarter(entryId);
}

void MainApplication::startRegionCapture(bool showShortcutHintsOnEntry)
{
    if (!canStartRegionCapture()) return;

    // Note: Don't close popup menus - allow capturing them (like Snipaste)
    // Modal dialogs (QMessageBox) are handled by CaptureManager
    m_captureManager->startRegionCapture(showShortcutHintsOnEntry);
}

void MainApplication::onRegionCapture()
{
    startRegionCapture(true);
}

void MainApplication::onFullScreenCapture()
{
    if (!canStartRegionCapture() || m_captureManager->isActive()) return;

    m_captureManager->startFullScreenCapture();
}

void MainApplication::onQuickPin()
{
    // Don't trigger if screen canvas is active
    if (m_screenCanvasManager->isActive()) {
        return;
    }

    // Don't trigger while a recording is in flight (an open preview is fine)
    if (m_recordingManager->blocksCapture()) {
        return;
    }
    if (m_screenPickerDialog) {
        return;
    }

    // Note: Don't close popup menus - allow capturing them (like Snipaste)
    // Modal dialogs (QMessageBox) are handled by CaptureManager
    m_captureManager->startQuickPinCapture();
}

void MainApplication::onScreenCanvas()
{
    // Don't trigger if capture is active
    if (m_captureManager->isActive()) {
        qDebug() << "onScreenCanvas: blocked by captureManager";
        return;
    }

    // Don't trigger while a recording is in flight (an open preview is fine)
    if (m_recordingManager->blocksCapture()) {
        qDebug() << "onScreenCanvas: blocked by recordingManager";
        return;
    }
    if (m_screenPickerDialog) {
        qDebug() << "onScreenCanvas: blocked by screen picker";
        return;
    }

    // Close any open popup menus to prevent focus conflicts
    if (QWidget* popup = QApplication::activePopupWidget()) {
        popup->close();
    }

    m_screenCanvasManager->toggle();
}

void MainApplication::onFullScreenRecording()
{
    if (!PlatformFeatures::instance().capabilities().supportsRecording) {
        return;
    }

    if (m_screenPickerDialog) {
        closeScreenPicker();
        return;
    }

    // Don't trigger if screen canvas is active
    if (m_screenCanvasManager->isActive()) {
        return;
    }

    // Don't trigger if capture is active
    switch (m_recordingManager->state()) {
    case RecordingManager::State::Recording:
    case RecordingManager::State::Paused:
        m_recordingManager->stopRecording();
        return;
    case RecordingManager::State::Preparing:
    case RecordingManager::State::Countdown:
        m_recordingManager->cancelRecording();
        return;
    case RecordingManager::State::Encoding:
    case RecordingManager::State::Previewing:
        return;
    case RecordingManager::State::Idle:
        break;
    }

    if (m_captureManager->isActive()) {
        return;
    }

    // Close any open popup menus to prevent focus conflicts
    if (QWidget* popup = QApplication::activePopupWidget()) {
        popup->close();
    }

    startScreenRecordingFlow(nullptr);
}

void MainApplication::onToggleAllPinsVisibility()
{
    m_pinWindowManager->toggleAllPinsVisibility();
}

void MainApplication::onCloseAllPins()
{
    m_pinWindowManager->closeAllWindows();
}

void MainApplication::onPinFromImage()
{
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);

    QString filePath = QFileDialog::getOpenFileName(
        nullptr,
        tr("Select Image to Pin"),
        defaultDir,
        tr("Images (*.png *.jpg *.jpeg *.gif *.bmp *.webp *.tiff *.tif);;All Files (*)")
    );

    if (filePath.isEmpty()) {
        return;
    }

    loadImageForPin(filePath);
}

void MainApplication::loadImageForPin(const QString& filePath, std::optional<QPoint> position)
{
    // Show loading toast
    SnapTray::QmlToast::screenToast().showToast(
        SnapTray::QmlToast::Level::Info,
        tr("Loading Image"),
        QFileInfo(filePath).fileName()
    );

    // Keep completion ownership on the GUI thread. Destroying the application
    // also destroys the watcher; the decoding task never holds a QObject pointer.
    auto* watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, filePath, position] {
        const QImage image = watcher->result();
        watcher->deleteLater();
        onImageLoaded(filePath, image, position);
    });
    watcher->setFuture(QtConcurrent::run([filePath] {
        QImageReader reader(filePath);
        reader.setAutoTransform(true);
        return reader.read();
    }));
}

void MainApplication::onHistoryWindow()
{
    if (m_historyWindow) {
        if (m_historyWindow->isMinimized()) {
            m_historyWindow->showNormal();
        }
        else if (!m_historyWindow->isVisible()) {
            m_historyWindow->show();
        }
        m_historyWindow->raise();
        m_historyWindow->activateWindow();
        return;
    }

    m_historyWindow = new SnapTray::QmlHistoryWindow(
        m_pinWindowManager,
        [this](const QString& entryId) {
            return startHistoryReplay(entryId);
        },
        this);
    m_historyWindow->show();
    m_historyWindow->raise();
    m_historyWindow->activateWindow();
}

void MainApplication::onImageLoaded(const QString& filePath, const QImage& image, std::optional<QPoint> position)
{
    if (image.isNull()) {
        QFileInfo fileInfo(filePath);
        SnapTray::QmlToast::screenToast().showToast(
            SnapTray::QmlToast::Level::Error,
            tr("Failed to Load Image"),
            fileInfo.fileName()
        );
        return;
    }

    // Get screen geometry
    QScreen* screen = position ? QGuiApplication::screenAt(*position) : nullptr;
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) {
        qCritical() << "MainApplication: No valid screen available for pin window";
        return;
    }

    const QImage displayImage = convertImageForDisplay(image);
    const QPixmap pixmap = QPixmap::fromImage(displayImage);
    if (pixmap.isNull()) {
        QFileInfo fileInfo(filePath);
        SnapTray::QmlToast::screenToast().showToast(
            SnapTray::QmlToast::Level::Error,
            tr("Failed to Load Image"),
            fileInfo.fileName()
        );
        return;
    }

    const PinWindowPlacement placement = computeInitialPinWindowPlacement(
        pixmap,
        screen->availableGeometry());

    // Create pin window and apply zoom if needed
    PinWindow *pinWindow = m_pinWindowManager->createPinWindow(pixmap, position.value_or(placement.position));
    if (pinWindow && placement.zoomLevel < 1.0) {
        pinWindow->setZoomLevel(placement.zoomLevel);
    }
}

void MainApplication::onSettings()
{
    SnapTray::QmlSettingsWindow* settingsWindow = ensureSettingsWindow();
    if (!settingsWindow) {
        return;
    }

    if (settingsWindow->isVisible()) {
        settingsWindow->raise();
        settingsWindow->activateWindow();
        return;
    }

    settingsWindow->show();
}

void MainApplication::onCheckForUpdates()
{
    if (UpdateCoordinator::instance().isExternallyManaged()) {
        return;
    }

    SnapTray::QmlSettingsWindow* settingsWindow = ensureSettingsWindow();
    if (settingsWindow) {
        settingsWindow->checkForUpdates();
    }
}

SnapTray::QmlSettingsWindow* MainApplication::ensureSettingsWindow()
{
    if (m_settingsWindow) {
        return m_settingsWindow;
    }

    m_settingsWindow = new SnapTray::QmlSettingsWindow(this);

    connect(m_settingsWindow, &SnapTray::QmlSettingsWindow::ocrLanguagesChanged,
            m_pinWindowManager, &PinWindowManager::updateOcrLanguages);

    return m_settingsWindow;
}

void MainApplication::onHotkeyAction(SnapTray::HotkeyAction action)
{
    using namespace SnapTray;

    switch (action) {
    case HotkeyAction::RegionCapture:
        if (m_captureManager && m_captureManager->isActive()) {
            m_captureManager->cycleOrSwitchCaptureScreenByCursor();
        } else {
            startRegionCapture(true);
        }
        break;
    case HotkeyAction::CaptureFullScreen:
        onFullScreenCapture();
        break;
    case HotkeyAction::ScreenCanvas:
        onScreenCanvas();
        break;
    case HotkeyAction::PasteFromClipboard:
        onPasteFromClipboard();
        break;
    case HotkeyAction::QuickPin:
        onQuickPin();
        break;
    case HotkeyAction::PinFromImage:
        onPinFromImage();
        break;
    case HotkeyAction::HistoryWindow:
        onHistoryWindow();
        break;
    case HotkeyAction::TogglePinsVisibility:
        onToggleAllPinsVisibility();
        break;
    case HotkeyAction::RecordFullScreen:
        if (PlatformFeatures::instance().capabilities().supportsRecording
            && acceptRecordingHotkey(action)) {
            onFullScreenRecording();
        }
        break;
    case HotkeyAction::PauseResumeRecording:
        if (m_recordingManager && acceptRecordingHotkey(action)) {
            m_recordingManager->togglePause();
        }
        break;
    case HotkeyAction::ShowRecordingControls:
        if (m_recordingManager) {
            m_recordingManager->showRecordingControls();
        }
        break;
    default:
        break;
    }
}

bool MainApplication::acceptRecordingHotkey(SnapTray::HotkeyAction action)
{
    // Key auto-repeat or a double press must not start and cancel a recording,
    // or pause and immediately resume it: a hold is one press until release.
    if (!m_recordingHotkeyClock.isValid()) {
        m_recordingHotkeyClock.start();
    }
    return m_recordingHotkeyHolds.accept(static_cast<int>(action), m_recordingHotkeyClock.elapsed());
}

void MainApplication::onHotkeyChanged(SnapTray::HotkeyAction, const SnapTray::HotkeyConfig&)
{
    updateTrayMenuHotkeyText();
    updateTrayToolTip();
    updatePinsVisibilityActionText();
}

void MainApplication::onHotkeyInitializationCompleted(const QStringList& failedHotkeys)
{
    if (failedHotkeys.isEmpty()) {
        return;
    }

    SnapTray::QmlToast::screenToast().showToast(
        SnapTray::QmlToast::Level::Error,
        tr("Hotkey Registration Failed"),
        failedHotkeys.join(QStringLiteral(", ")) + tr(" failed to register."),
        5000);
}

QPixmap MainApplication::renderTextToPixmap(const QString &text)
{
    // Set up font
    QFont font("SF Pro", 14);
    font.setStyleHint(QFont::SansSerif);
    QFontMetrics fm(font);

    // Calculate text dimensions (support multi-line)
    QStringList lines = text.split('\n');
    int maxWidth = 0;
    for (const QString &line : lines) {
        maxWidth = qMax(maxWidth, fm.horizontalAdvance(line));
    }
    int totalHeight = lines.count() * fm.lineSpacing();

    // Limit maximum size
    maxWidth = qMin(maxWidth, kMaxTextPixmapWidth);
    totalHeight = qMin(totalHeight, kMaxTextPixmapHeight);

    // Create pixmap with padding
    const int padding = kTextPixmapPadding;
    qreal dpr = qApp->devicePixelRatio();
    QSize pixmapSize(maxWidth + 2 * padding, totalHeight + 2 * padding);

    QPixmap pixmap(CoordinateHelper::toPhysical(pixmapSize, dpr));
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(kStickyNoteBackground);

    // Draw text
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setFont(font);
    painter.setPen(kTextAnnotationForeground);

    int y = padding + fm.ascent();
    for (const QString &line : lines) {
        painter.drawText(padding, y, line);
        y += fm.lineSpacing();
        if (y > totalHeight + padding) {
            break;  // Stop if exceeding max height
        }
    }

    return pixmap;
}

void MainApplication::onPasteFromClipboard()
{
    pinFromClipboard();
}

void MainApplication::pinFromClipboard(std::optional<QPoint> requestedPosition)
{
    if (m_screenPickerDialog) {
        return;
    }

    // If region selection is active with complete selection, trigger pin (same as Enter)
    if (m_captureManager && m_captureManager->hasCompleteSelection()) {
        m_captureManager->triggerFinishSelection();
        return;
    }

    QClipboard *clipboard = QGuiApplication::clipboard();
    QPixmap pixmap = clipboard->pixmap();

    if (pixmap.isNull()) {
        // Try to get image from mimeData
        const QMimeData *mimeData = clipboard->mimeData();
        if (mimeData && mimeData->hasImage()) {
            pixmap = qvariant_cast<QPixmap>(mimeData->imageData());
        }
    }

    // If no image, try to get text and convert to image
    if (pixmap.isNull()) {
        const QMimeData *mimeData = clipboard->mimeData();
        if (mimeData && mimeData->hasText()) {
            QString text = mimeData->text();
            if (!text.isEmpty()) {
                pixmap = renderTextToPixmap(text);
            }
        }
    }

    if (!pixmap.isNull()) {
        // Create pin window at screen center
        QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen) {
            screen = QGuiApplication::primaryScreen();
        }
        if (!screen) {
            qCritical() << "MainApplication: No valid screen available for paste";
            return;
        }
        QRect screenGeometry = screen->geometry();

        // Use logical size for centering (HiDPI support)
        const qreal dpr = pixmap.devicePixelRatio() > 0.0 ? pixmap.devicePixelRatio() : 1.0;
        QSize logicalSize = CoordinateHelper::toLogical(pixmap.size(), dpr);
        QPoint position = screenGeometry.center() - QPoint(logicalSize.width() / 2, logicalSize.height() / 2);

        m_pinWindowManager->createPinWindow(pixmap, requestedPosition.value_or(position));
    }
}

void MainApplication::showRecordingPreview(const QString& videoPath, int defaultOutputFormat,
                                           bool recordedAsIntermediate)
{
    // Prevent multiple preview windows
    if (m_previewBackend)
        return;

    m_previewBackend = new RecordingPreviewBackend(videoPath, recordedAsIntermediate, this);
    m_previewBackend->setDefaultOutputFormat(defaultOutputFormat);

    // The save handler opens a native dialog. Keep it out of the QML click/key
    // handler stack so preview deleteLater() cannot run during a nested dialog
    // event loop while QML is still handling the action.
    connect(m_previewBackend, &RecordingPreviewBackend::saveRequested,
        this, &MainApplication::onPreviewSaveRequested, Qt::QueuedConnection);
    connect(m_previewBackend, &RecordingPreviewBackend::discardRequested,
        this, &MainApplication::onPreviewDiscardRequested, Qt::QueuedConnection);
    connect(m_previewBackend, &RecordingPreviewBackend::closed,
        this, [this](bool saved) {
            m_recordingManager->onPreviewClosed(saved);
            m_previewBackend->deleteLater();
            m_previewBackend = nullptr;
        });

    m_previewBackend->show();
}

void MainApplication::onPreviewSaveRequested(const QString& videoPath, const QSize& outputSize)
{
    const QPointer<RecordingPreviewBackend> backend(m_previewBackend);
    QString savedPath;
    const auto outcome = m_recordingManager->triggerSaveDialog(videoPath, outputSize, &savedPath);
    // The preview keeps the recording until it hears the outcome.
    if (!backend) {
        return;
    }
    using Outcome = RecordingPreviewBackend::SaveOutcome;
    switch (outcome) {
    case RecordingManager::SaveOutcome::Saved: backend->completeSave(Outcome::Saved, savedPath); break;
    case RecordingManager::SaveOutcome::Cancelled: backend->completeSave(Outcome::Cancelled); break;
    case RecordingManager::SaveOutcome::Failed: backend->completeSave(Outcome::Failed); break;
    }
}

void MainApplication::onPreviewDiscardRequested(const QString& videoPath)
{
    if (!videoPath.isEmpty() && QFile::exists(videoPath)) {
        if (!QFile::remove(videoPath)) {
            qWarning() << "MainApplication: Failed to delete temp file:" << videoPath;
        }
    }
    SnapTray::WindowTimelineSidecar::remove(videoPath);
    SnapTray::RecordingSession::remove(videoPath);
}

void MainApplication::updateActionHotkeyText(QAction* action,
                                             SnapTray::HotkeyAction hotkeyAction,
                                             const QString& baseName)
{
    if (!action) {
        return;
    }

    auto& mgr = SnapTray::HotkeyManager::instance();
    auto config = mgr.getConfig(hotkeyAction);
    QString displayHotkey = hotkeyDisplayText(config, QString());
    if (!displayHotkey.isEmpty()) {
        action->setText(tr("%1 (%2)").arg(baseName, displayHotkey));
    } else {
        action->setText(baseName);
    }
}

void MainApplication::updatePinsVisibilityActionText()
{
    if (!m_togglePinsVisibilityAction || !m_closeAllPinsAction || !m_pinWindowManager) {
        return;
    }

    const bool hasPins = m_pinWindowManager->windowCount() > 0;
    m_togglePinsVisibilityAction->setEnabled(hasPins);
    m_closeAllPinsAction->setEnabled(hasPins);

    const QString baseText = m_pinWindowManager->arePinsHidden()
        ? tr("Show All Pins")
        : tr("Hide All Pins");

    const auto config = SnapTray::HotkeyManager::instance().getConfig(
        SnapTray::HotkeyAction::TogglePinsVisibility);
    const QString displayHotkey = hotkeyDisplayText(config, QString());
    if (!displayHotkey.isEmpty()) {
        m_togglePinsVisibilityAction->setText(tr("%1 (%2)").arg(baseText, displayHotkey));
    } else {
        m_togglePinsVisibilityAction->setText(baseText);
    }
}

void MainApplication::updateTrayMenuHotkeyText()
{
    updateActionHotkeyText(m_regionCaptureAction,
                           SnapTray::HotkeyAction::RegionCapture,
                           tr("Region Capture"));
    updateActionHotkeyText(m_fullScreenCaptureAction,
                           SnapTray::HotkeyAction::CaptureFullScreen,
                           tr("Full Screen Capture"));
    updateActionHotkeyText(m_screenCanvasAction,
                           SnapTray::HotkeyAction::ScreenCanvas,
                           tr("Screen Canvas"));
    updateActionHotkeyText(m_pasteAction,
                           SnapTray::HotkeyAction::PasteFromClipboard,
                           tr("Paste"));
    updateActionHotkeyText(m_pinFromImageAction,
                           SnapTray::HotkeyAction::PinFromImage,
                           tr("Pin from Image..."));
    updateActionHotkeyText(m_historyAction,
                           SnapTray::HotkeyAction::HistoryWindow,
                           tr("History"));
    updateRecordingActionText();
}

void MainApplication::updateRecoveryAction()
{
    if (!m_recoverRecordingsAction) return;
    m_recoverRecordingsAction->setVisible(!m_recoverablePaths.isEmpty());
    m_recoverRecordingsAction->setText(tr("Recover Recordings (%1)...").arg(m_recoverablePaths.size()));
}

void MainApplication::showRecoveryDialog()
{
    if (m_recoveryDialog) {
        m_recoveryDialog->showCenteredOnScreen(QGuiApplication::primaryScreen());
        return;
    }
    // Re-scan so the dialog reflects the files as they are now.
    auto* viewModel = new SnapTray::RecordingRecoveryViewModel(this);
    auto* dialog = new SnapTray::QmlDialog(
        QUrl(QStringLiteral("qrc:/SnapTrayQml/dialogs/RecordingRecoveryDialog.qml")),
        viewModel, QStringLiteral("viewModel"), this);
    viewModel->setParent(dialog);
    m_recoveryDialog = dialog;
    connect(dialog, &SnapTray::QmlDialog::closed, dialog, &QObject::deleteLater);
    const QPointer<SnapTray::RecordingRecoveryViewModel> model(viewModel);
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(m_recordingManager, &RecordingManager::recoveriesFound, dialog,
        [model, connection](const QList<SnapTray::RecordingSession::Recoverable>& found) {
            if (model) model->setRecoveries(found);
            QObject::disconnect(*connection);
        });
    connect(viewModel, &SnapTray::RecordingRecoveryViewModel::recoverRequested, this,
        [this](const QString& path) {
            if (!m_recordingManager->openRecoveredRecording(path)) {
                SnapTray::QmlToast::screenToast().showToast(
                    SnapTray::QmlToast::Level::Warning, tr("Recording Busy"),
                    tr("Finish or close the current recording first."));
                return;
            }
            m_recoverablePaths.removeAll(path);
            updateRecoveryAction();
            if (m_recoveryDialog) m_recoveryDialog->close();
        });
    connect(viewModel, &SnapTray::RecordingRecoveryViewModel::discarded, this,
        [this](const QString& path) {
            m_recoverablePaths.removeAll(path);
            updateRecoveryAction();
        });
    connect(viewModel, &SnapTray::RecordingRecoveryViewModel::closeRequested, dialog, [this] {
        if (m_recoveryDialog) m_recoveryDialog->close();
    });
    m_recordingManager->scanForRecoveries();
    dialog->showCenteredOnScreen(QGuiApplication::primaryScreen());
}

void MainApplication::updateRecordingActionText()
{
    const auto recordingState = m_recordingManager ? m_recordingManager->state() : RecordingManager::State::Idle;
    const bool capturing = recordingState == RecordingManager::State::Recording
        || recordingState == RecordingManager::State::Paused;
    if (m_pauseRecordingAction) {
        m_pauseRecordingAction->setVisible(capturing);
        updateActionHotkeyText(m_pauseRecordingAction,
                               SnapTray::HotkeyAction::PauseResumeRecording,
                               recordingState == RecordingManager::State::Paused
                                   ? QCoreApplication::translate("RecordingControlBar", "Resume Recording")
                                   : QCoreApplication::translate("RecordingControlBar", "Pause Recording"));
    }
    if (m_copyRecordingDiagnosticsAction) {
        m_copyRecordingDiagnosticsAction->setVisible(
            m_recordingManager && !m_recordingManager->lastDiagnostics().isEmpty()
            && (recordingState == RecordingManager::State::Idle
                || recordingState == RecordingManager::State::Previewing));
    }
    if (m_showRecordingControlsAction) {
        m_showRecordingControlsAction->setVisible(
            capturing
            || recordingState == RecordingManager::State::Preparing
            || recordingState == RecordingManager::State::Countdown);
        updateActionHotkeyText(m_showRecordingControlsAction,
                               SnapTray::HotkeyAction::ShowRecordingControls,
                               tr("Show Recording Controls"));
    }
    if (!m_fullScreenRecordingAction) {
        return;
    }

    const QString recordScreenText = tr("Record Screen");
    QString baseName = recordScreenText;
    bool includeHotkey = true;

    if (!m_recordingManager && !m_screenPickerDialog) {
        updateActionHotkeyText(m_fullScreenRecordingAction,
                               SnapTray::HotkeyAction::RecordFullScreen,
                               recordScreenText);
        return;
    }

    if (m_screenPickerDialog) {
        baseName = tr("Cancel Recording");
        includeHotkey = false;
    } else if (m_recordingManager) {
        switch (m_recordingManager->state()) {
        case RecordingManager::State::Recording:
        case RecordingManager::State::Paused:
            baseName = tr("Stop Recording");
            includeHotkey = false;
            break;
        case RecordingManager::State::Preparing:
        case RecordingManager::State::Countdown:
            baseName = tr("Cancel Recording");
            includeHotkey = false;
            break;
        case RecordingManager::State::Idle:
        case RecordingManager::State::Encoding:
        case RecordingManager::State::Previewing:
            break;
        }
    }

    if (includeHotkey) {
        updateActionHotkeyText(m_fullScreenRecordingAction,
                               SnapTray::HotkeyAction::RecordFullScreen,
                               baseName);
    } else {
        m_fullScreenRecordingAction->setText(baseName);
    }
}

void MainApplication::updateTrayToolTip()
{
    if (!m_trayIcon) {
        return;
    }

    const QString statusLine = trayRecordingStatusLine();

#ifdef Q_OS_WIN
    const QString notSetText = tr("Not set");
    const QList<SnapTray::TrayTooltipHotkeyLine> hotkeyLines{
        {tr("Region Capture hotkey"),
         hotkeyDisplayTextOrFallback(SnapTray::HotkeyAction::RegionCapture, notSetText)},
        {tr("Paste hotkey"),
         hotkeyDisplayTextOrFallback(SnapTray::HotkeyAction::PasteFromClipboard, notSetText)},
        {tr("Screen Canvas hotkey"),
         hotkeyDisplayTextOrFallback(SnapTray::HotkeyAction::ScreenCanvas, notSetText)},
    };

    const QString applicationName = QCoreApplication::applicationName().isEmpty()
        ? QStringLiteral("SnapTray")
        : QCoreApplication::applicationName();
    const QString version = QCoreApplication::applicationVersion();
    const QString installSource = InstallSourceDetector::getSourceDisplayName(
        InstallSourceDetector::detect());

    m_trayIcon->setToolTip(SnapTray::formatWindowsTrayTooltip(
        applicationName,
        version,
        installSource,
        hotkeyLines,
        notSetText,
        statusLine));
#else
    if (!statusLine.isEmpty()) {
        m_trayIcon->setToolTip(tr("SnapTray - Screenshot Utility") + QStringLiteral("\n") + statusLine);
    } else {
        m_trayIcon->setToolTip(tr("SnapTray - Screenshot Utility"));
    }
#endif
}

bool MainApplication::holdsRecordingWarnings() const
{
    if (!m_recordingManager || !PlatformFeatures::instance().capabilities().recordingControlsInTray) {
        return false;
    }
    // From preparation on: a five-second toast shown during the countdown
    // would still be on screen when capture starts.
    switch (m_recordingManager->state()) {
    case RecordingManager::State::Preparing:
    case RecordingManager::State::Countdown:
    case RecordingManager::State::Recording:
    case RecordingManager::State::Paused:
        return true;
    default:
        return false;
    }
}

void MainApplication::showRecordingWarning(const QString& warning)
{
    if (holdsRecordingWarnings()) {
        if (!m_heldRecordingWarnings.contains(warning)) {
            m_heldRecordingWarnings.append(warning);
        }
        updateTrayToolTip();
        return;
    }
    if (m_recordingWarningToast) {
        m_recordingWarningToast(tr("Recording Warning"), warning);
        return;
    }
    SnapTray::QmlToast::screenToast().showToast(SnapTray::QmlToast::Level::Warning, tr("Recording Warning"),
                                                warning, 5000);
}

void MainApplication::flushHeldRecordingWarnings()
{
    if (m_heldRecordingWarnings.isEmpty()) {
        return;
    }
    const QString text = m_heldRecordingWarnings.join(QLatin1Char('\n'));
    m_heldRecordingWarnings.clear();
    updateTrayToolTip();
    showRecordingWarning(text);
}

QString MainApplication::trayRecordingStatusLine() const
{
    using Status = SnapTray::TrayRecordingStatus;
    QString statusText;
    switch (m_trayRecordingStatus) {
    case Status::Idle: return {};
    case Status::Preparing: statusText = tr("Preparing to record"); break;
    case Status::Countdown: statusText = tr("Recording starts soon"); break;
    case Status::Recording: statusText = tr("Recording"); break;
    case Status::Paused: statusText = tr("Recording paused"); break;
    case Status::Finalizing: statusText = tr("Finishing recording"); break;
    }

    QStringList parts{statusText};
    if ((m_trayRecordingStatus == Status::Recording || m_trayRecordingStatus == Status::Paused)
        && m_trayRecordingElapsedMs >= 0) {
        parts.append(SnapTray::formatTrayRecordingElapsed(m_trayRecordingElapsedMs));
    }
    const QString screenSummary = m_recordingManager
        ? m_recordingManager->activeScreenSummary()
        : QString();
    if (!screenSummary.isEmpty()) {
        parts.append(screenSummary);
    }
    if (m_recordingManager && m_recordingManager->isDroppingFrames()) {
        parts.append(tr("dropping frames"));
    }
    if (m_recordingManager && m_recordingManager->isLowOnDiskSpace()) {
        parts.append(tr("low disk space"));
    }
    QString line = parts.join(QStringLiteral(" \u00B7 "));
    // Audio stays visible for the whole recording, including fallbacks that
    // were announced once in a notification.
    const QString audioLine = m_recordingManager && m_trayRecordingStatus != Status::Finalizing
        ? SnapTray::describeRecordingAudioStatus(m_recordingManager->audioStatus())
        : QString();
    if (!audioLine.isEmpty()) {
        line += QLatin1Char('\n') + audioLine;
    }
    // Warnings held back from the screen while capturing (shown after).
    if (!m_heldRecordingWarnings.isEmpty()) {
        line += QLatin1Char('\n') + tr("Warning: %1").arg(m_heldRecordingWarnings.last());
    }
    return line;
}

void MainApplication::updateTrayRecordingIndicator()
{
    const auto state = m_recordingManager ? m_recordingManager->state() : RecordingManager::State::Idle;
    const auto status = trayRecordingStatusForState(state);
    if (status != SnapTray::TrayRecordingStatus::Recording
        && status != SnapTray::TrayRecordingStatus::Paused) {
        m_trayRecordingElapsedMs = -1;
    }
    if (status != m_trayRecordingStatus) {
        m_trayRecordingStatus = status;
        if (m_trayIcon && !m_trayBasePixmap.isNull()) {
            m_trayIcon->setIcon(QIcon(SnapTray::composeTrayRecordingPixmap(m_trayBasePixmap, status)));
        }
    }
    updateTrayToolTip();
}

void MainApplication::onRecordingDurationChanged(qint64 elapsedMs)
{
    // durationChanged fires every 100 ms; the tooltip only shows whole seconds.
    const qint64 previousSecond = m_trayRecordingElapsedMs < 0
        ? -1 : m_trayRecordingElapsedMs / kTrayElapsedResolutionMs;
    m_trayRecordingElapsedMs = qMax<qint64>(0, elapsedMs);
    if (m_trayRecordingElapsedMs / kTrayElapsedResolutionMs != previousSecond) {
        updateTrayToolTip();
    }
}

void MainApplication::startScreenRecordingFlow(QScreen* preferredScreen)
{
    if (m_screenPickerDialog) {
        m_screenPickerDialog->showCenteredOnScreen(
            preferredScreen ? preferredScreen : QGuiApplication::primaryScreen());
        return;
    }

    QVector<SnapTray::ScreenSource> sources = SnapTray::ScreenSourceService::availableSources(true);
    if (sources.isEmpty()) {
        SnapTray::QmlToast::screenToast().showToast(
            SnapTray::QmlToast::Level::Error,
            tr("Recording Error"),
            tr("No screen available for recording."),
            5000);
        return;
    }

    if (preferredScreen) {
        const int preferredIndex = qMax(0, QGuiApplication::screens().indexOf(preferredScreen));
        const QString preferredId = SnapTray::ScreenSourceService::sourceId(preferredScreen, preferredIndex);
        auto preferredIt = std::find_if(
            sources.begin(),
            sources.end(),
            [&preferredId](const SnapTray::ScreenSource& source) {
                return source.id == preferredId;
            });
        if (preferredIt != sources.end() && preferredIt != sources.begin()) {
            const SnapTray::ScreenSource preferredSource = *preferredIt;
            sources.erase(preferredIt);
            sources.prepend(preferredSource);
        }
    }

    // Quick recording: one screen and no setup requested starts right away
    // with the last-used settings.
    if (sources.size() == 1 && !RecordingSettingsManager::instance().showSetupBeforeRecording()) {
        m_recordingManager->startScreenRecording(sources.first().screen.data());
        return;
    }

    auto* viewModel = new SnapTray::ScreenPickerViewModel(sources, this);

    auto* dialog = new SnapTray::QmlDialog(
        QUrl(QStringLiteral("qrc:/SnapTrayQml/dialogs/ScreenPickerDialog.qml")),
        viewModel,
        QStringLiteral("viewModel"),
        this);
    dialog->setModal(true);
    attachScreenPicker(dialog, viewModel);
    dialog->showCenteredOnScreen(preferredScreen ? preferredScreen : QGuiApplication::primaryScreen());
    updateRecordingActionText();
}

void MainApplication::attachScreenPicker(SnapTray::QmlDialog* dialog,
                                         SnapTray::ScreenPickerViewModel* viewModel)
{
    m_screenPickerDialog = dialog;
    m_screenPickerViewModel = viewModel;

    if (!dialog || !viewModel) {
        return;
    }

    connect(viewModel, &SnapTray::ScreenPickerViewModel::screenChosen,
            this, &MainApplication::onScreenPickerChosen);
    connect(viewModel, &SnapTray::ScreenPickerViewModel::cancelled,
            this, &MainApplication::onScreenPickerCancelled);
    connect(dialog, &SnapTray::QmlDialog::closed,
            this, [this, dialogPointer = QPointer<SnapTray::QmlDialog>(dialog)]() {
                onScreenPickerClosed(dialogPointer);
            });
}

void MainApplication::onScreenPickerChosen(QScreen* screen)
{
    closeScreenPicker();

    if (!screen) {
        SnapTray::QmlToast::screenToast().showToast(
            SnapTray::QmlToast::Level::Error,
            tr("Recording Error"),
            tr("No screen available for recording."),
            5000);
        return;
    }

    m_recordingManager->startScreenRecording(screen);
}

void MainApplication::onScreenPickerCancelled()
{
    closeScreenPicker();
}

void MainApplication::onScreenPickerClosed(SnapTray::QmlDialog* dialog)
{
    if (m_screenPickerDialog == dialog) {
        m_screenPickerDialog = nullptr;
        m_screenPickerViewModel = nullptr;
    }

    if (dialog) {
        dialog->deleteLater();
    }

    updateRecordingActionText();
}

void MainApplication::closeScreenPicker()
{
    QPointer<SnapTray::QmlDialog> dialog = m_screenPickerDialog;
    m_screenPickerDialog = nullptr;
    m_screenPickerViewModel = nullptr;

    if (dialog) {
        dialog->close();
    }

    updateRecordingActionText();
}
