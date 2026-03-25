#include "MainApplication.h"

#include "CaptureManager.h"
#include "PinWindow.h"
#include "PinWindowManager.h"
#include "PlatformFeatures.h"
#include "RecordingManager.h"
#include "ScreenCanvasManager.h"
#include "qml/QmlHistoryWindow.h"
#include "qml/QmlSettingsWindow.h"
#include "pinwindow/PinWindowPlacement.h"
#include "ImageColorSpaceHelper.h"
#include "cli/IPCProtocol.h"
#include "hotkey/HotkeyManager.h"
#include "qml/QmlToast.h"
#include "qml/RecordingPreviewBackend.h"
#include "ui/TrayTooltipFormatter.h"
#include "update/InstallSourceDetector.h"
#include "update/UpdateCoordinator.h"
#include "utils/CoordinateHelper.h"
#ifdef SNAPTRAY_ENABLE_MCP
#include "mcp/MCPServer.h"
#include "settings/MCPSettingsManager.h"
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
#include <QPainter>
#include <QFont>
#include <QFontMetrics>
#include <QtConcurrent>

#include <cmath>
#include <limits>

namespace {
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
    case RecordingManager::State::Selecting:
    case RecordingManager::State::Preparing:
    case RecordingManager::State::Countdown:
        return false;
    }

    return false;
}

bool tryReadJsonInt(const QJsonObject& options, const QString& key, int* value)
{
    if (!value || !options.contains(key)) {
        return false;
    }

    const QJsonValue optionValue = options.value(key);
    if (optionValue.isDouble()) {
        const double number = optionValue.toDouble();
        if (!std::isfinite(number)
            || number < static_cast<double>(std::numeric_limits<int>::min())
            || number > static_cast<double>(std::numeric_limits<int>::max())
            || std::floor(number) != number) {
            return false;
        }

        *value = static_cast<int>(number);
        return true;
    }

    if (optionValue.isString()) {
        bool ok = false;
        const int parsedValue = optionValue.toString().toInt(&ok);
        if (!ok) {
            return false;
        }

        *value = parsedValue;
        return true;
    }

    return false;
}

QString hotkeyDisplayTextOrFallback(SnapTray::HotkeyAction action, const QString& fallbackText)
{
    const auto config = SnapTray::HotkeyManager::instance().getConfig(action);
    const QString displayHotkey = SnapTray::HotkeyManager::formatKeySequence(config.keySequence);
    return displayHotkey.isEmpty() ? fallbackText : displayHotkey;
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
    , m_pinFromImageAction(nullptr)
    , m_historyAction(nullptr)
    , m_togglePinsVisibilityAction(nullptr)
    , m_closeAllPinsAction(nullptr)
    , m_fullScreenRecordingAction(nullptr)
{
}

MainApplication::~MainApplication()
{
    UpdateCoordinator::setShutdownHooks(UpdateCoordinator::ShutdownGuardFn(),
                                        UpdateCoordinator::ShutdownRequestFn());

    // Shutdown hotkey manager to unregister all hotkeys before destruction
    SnapTray::HotkeyManager::instance().shutdown();

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

    // CLI commands should preempt existing capture mode
    // This ensures CLI commands work reliably even if RegionSelector is stuck
    if (msg.command != "gui" && m_captureManager->isActive()) {
        qDebug() << "CLI: Cancelling active capture to process new command";
        m_captureManager->cancelCapture();
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
    else if (msg.command == "record") {
        QString action = msg.options["action"].toString().toLower();
        const auto stopOrCancelRecording = [this]() {
            switch (m_recordingManager->state()) {
            case RecordingManager::State::Recording:
            case RecordingManager::State::Paused:
                m_recordingManager->stopRecording();
                break;
            case RecordingManager::State::Selecting:
            case RecordingManager::State::Preparing:
            case RecordingManager::State::Countdown:
                m_recordingManager->cancelRecording();
                break;
            case RecordingManager::State::Encoding:
            case RecordingManager::State::Idle:
            case RecordingManager::State::Previewing:
                break;
            }
        };

        if (action == "start") {
            if (!m_recordingManager->isActive()) {
                // Check if a specific screen is requested
                if (msg.options.contains("screen")) {
                    int screenNum = -1;
                    if (tryReadJsonInt(msg.options, "screen", &screenNum)) {
                        auto screens = QGuiApplication::screens();
                        if (screenNum >= 0 && screenNum < screens.size()) {
                            QScreen* screen = screens.at(screenNum);
                            m_recordingManager->startFullScreenRecording(screen);
                        }
                        else {
                            qWarning() << "Invalid screen number:" << screenNum;
                        }
                    }
                    else {
                        qWarning() << "Invalid screen option value:" << msg.options.value("screen");
                    }
                }
                else {
                    onFullScreenRecording();
                }
            }
        }
        else if (action == "stop") {
            if (m_recordingManager->isActive()) {
                stopOrCancelRecording();
            }
        }
        else if (action.isEmpty() || action == "toggle") {
            // Default action (no explicit action provided): toggle
            if (m_recordingManager->isActive()) {
                stopOrCancelRecording();
            }
            else {
                onFullScreenRecording();
            }
        }
        else {
            qWarning() << "Unknown record action:" << action;
        }
    }
    else if (msg.command == "pin") {
        if (msg.options["clipboard"].toBool()) {
            onPasteFromClipboard();
        }
        else if (msg.options.contains("file")) {
            QString filePath = msg.options["file"].toString();
            QImage image(filePath);
            if (!image.isNull()) {
                QPixmap pixmap = QPixmap::fromImage(image);

                // Get positioning options
                bool hasX = msg.options.contains("x");
                bool hasY = msg.options.contains("y");
                bool center = msg.options["center"].toBool();

                // Calculate position
                QScreen* screen = QGuiApplication::primaryScreen();
                if (!screen) {
                    qCritical() << "MainApplication: No valid screen available for pin window";
                    return;
                }
                QRect screenGeo = screen->availableGeometry();
                const qreal dpr = pixmap.devicePixelRatio() > 0.0 ? pixmap.devicePixelRatio() : 1.0;
                QSize logicalSize = CoordinateHelper::toLogical(pixmap.size(), dpr);
                QPoint position;

                if (hasX && hasY) {
                    position = QPoint(msg.options["x"].toInt(), msg.options["y"].toInt());
                }
                else if (center) {
                    position = screenGeo.center() - QPoint(logicalSize.width() / 2, logicalSize.height() / 2);
                }
                else {
                    // Default: center on screen (same as onImageLoaded)
                    position = screenGeo.center() - QPoint(logicalSize.width() / 2, logicalSize.height() / 2);
                }

                m_pinWindowManager->createPinWindow(pixmap, position);
            }
        }
    }
    else if (msg.command == "config") {
        onSettings();
    }
    else {
        qWarning() << "Unknown CLI command:" << msg.command;
    }
}

void MainApplication::initialize()
{
    // Create pin window manager first (needed by capture manager)
    m_pinWindowManager = new PinWindowManager(this);

    // Create capture manager
    m_captureManager = new CaptureManager(m_pinWindowManager, this);

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

    // Connect capture manager's recording request to recording manager
    connect(m_captureManager, &CaptureManager::recordingRequested,
        m_recordingManager, &RecordingManager::startRegionSelectionWithPreset);

    // Connect recording cancellation to return to capture mode
    // Use delay to ensure RecordingRegionSelector is fully closed before capturing new screenshot
    connect(m_recordingManager, &RecordingManager::selectionCancelledWithRegion,
        this, [this](const QRect& region, QScreen* screen) {
            // Use QPointer to guard against screen being deleted during the delay
            QPointer<QScreen> safeScreen = screen;
            QTimer::singleShot(50, this, [this, region, safeScreen]() {
                // Check if screen is still valid before using it
                if (safeScreen) {
                    m_captureManager->startRegionCaptureWithPreset(region, safeScreen);
                }
                else {
                    qWarning() << "MainApplication: Screen was deleted during delay, skipping capture";
                }
                });
        });

    // Create system tray icon
    QIcon icon = PlatformFeatures::instance().createTrayIcon();
    m_trayIcon = new QSystemTrayIcon(icon, this);

    // Create context menu
    m_trayMenu = new QMenu();

    m_regionCaptureAction = m_trayMenu->addAction(tr("Region Capture"));
    connect(m_regionCaptureAction, &QAction::triggered, this, &MainApplication::onRegionCapture);

    m_screenCanvasAction = m_trayMenu->addAction(tr("Screen Canvas"));
    connect(m_screenCanvasAction, &QAction::triggered, this, &MainApplication::onScreenCanvas);

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

    m_fullScreenRecordingAction = m_trayMenu->addAction(tr("Record Full Screen"));
    connect(m_fullScreenRecordingAction, &QAction::triggered, this, &MainApplication::onFullScreenRecording);

    m_trayMenu->addSeparator();

    QAction* settingsAction = m_trayMenu->addAction(tr("Settings"));
    connect(settingsAction, &QAction::triggered, this, &MainApplication::onSettings);

    m_trayMenu->addSeparator();

    QAction* exitAction = m_trayMenu->addAction(tr("Exit"));
    connect(exitAction, &QAction::triggered, qApp, &QCoreApplication::quit);

    // Set menu and show tray icon
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
    connect(&hotkeyManager, &SnapTray::HotkeyManager::hotkeyChanged,
            this, &MainApplication::onHotkeyChanged);
    connect(&hotkeyManager, &SnapTray::HotkeyManager::initializationCompleted,
            this, &MainApplication::onHotkeyInitializationCompleted);
    hotkeyManager.initialize();

    // Update tray menu with current hotkey text
    updateTrayMenuHotkeyText();
    updateTrayToolTip();
    updatePinsVisibilityActionText();

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

#ifdef SNAPTRAY_ENABLE_MCP
    if (MCPSettingsManager::instance().isEnabled()) {
        startMcpServer();
    }
#endif
}

bool MainApplication::canShutdownForUpdate() const
{
    return !m_recordingManager || !isBlockingUpdateRecordingState(m_recordingManager->state());
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

    if (m_screenCanvasManager && m_screenCanvasManager->isActive()) {
        m_screenCanvasManager->toggle();
    }

    if (m_recordingManager) {
        switch (m_recordingManager->state()) {
        case RecordingManager::State::Selecting:
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

#ifdef SNAPTRAY_ENABLE_MCP
bool MainApplication::startMcpServer()
{
    if (!m_mcpServer) {
        SnapTray::MCP::ToolCallContext toolContext;
        toolContext.pinWindowManager = m_pinWindowManager;
        toolContext.parentObject = this;
        m_mcpServer = std::make_unique<SnapTray::MCP::MCPServer>(toolContext, this);
    }

    if (m_mcpServer->isListening()) {
        return true;
    }

    QString mcpError;
    if (!m_mcpServer->start(SnapTray::MCP::MCPServer::kDefaultPort, &mcpError)) {
        qWarning() << "MainApplication: Failed to start MCP server:" << mcpError;
        SnapTray::QmlToast::screenToast().showToast(
            SnapTray::QmlToast::Level::Error,
            tr("MCP Server Unavailable"),
            tr("Unable to start MCP HTTP server on 127.0.0.1:%1")
                .arg(SnapTray::MCP::MCPServer::kDefaultPort),
            5000);
        return false;
    }

    qDebug() << "MainApplication: MCP server listening on 127.0.0.1:" << m_mcpServer->port();
    return true;
}

void MainApplication::stopMcpServer()
{
    if (!m_mcpServer || !m_mcpServer->isListening()) {
        return;
    }

    m_mcpServer->stop();
    qDebug() << "MainApplication: MCP server stopped";
}
#endif

void MainApplication::startRegionCapture(bool showShortcutHintsOnEntry)
{
    // Don't trigger if screen canvas is active
    if (m_screenCanvasManager->isActive()) {
        qDebug() << "onRegionCapture: blocked by screenCanvasManager";
        return;
    }

    // Don't trigger if recording is active
    if (m_recordingManager->isActive()) {
        qDebug() << "onRegionCapture: blocked by recordingManager";
        return;
    }

    // Note: Don't close popup menus - allow capturing them (like Snipaste)
    // Modal dialogs (QMessageBox) are handled by CaptureManager
    m_captureManager->startRegionCapture(showShortcutHintsOnEntry);
}

void MainApplication::onRegionCapture()
{
    startRegionCapture(true);
}

void MainApplication::onQuickPin()
{
    // Don't trigger if screen canvas is active
    if (m_screenCanvasManager->isActive()) {
        return;
    }

    // Don't trigger if recording is active
    if (m_recordingManager->isActive()) {
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

    // Don't trigger if recording is active
    if (m_recordingManager->isActive()) {
        qDebug() << "onScreenCanvas: blocked by recordingManager";
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
    // Don't trigger if screen canvas is active
    if (m_screenCanvasManager->isActive()) {
        return;
    }

    // Don't trigger if already recording
    if (m_recordingManager->isActive()) {
        return;
    }

    // Don't trigger if capture is active
    if (m_captureManager->isActive()) {
        return;
    }

    // Close any open popup menus to prevent focus conflicts
    if (QWidget* popup = QApplication::activePopupWidget()) {
        popup->close();
    }

    m_recordingManager->startFullScreenRecording();
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

    // Show loading toast
    SnapTray::QmlToast::screenToast().showToast(
        SnapTray::QmlToast::Level::Info,
        tr("Loading Image"),
        QFileInfo(filePath).fileName()
    );

    // Load image asynchronously (QImage is thread-safe, QPixmap is not)
    QPointer<MainApplication> guard(this);
    (void)QtConcurrent::run([filePath, guard]() {
        QImageReader reader(filePath);
        reader.setAutoTransform(true);
        QImage image = reader.read();

        // Call back to UI thread
        if (guard) {
            QMetaObject::invokeMethod(guard, [guard, filePath, image = std::move(image)]() {
                if (guard) {
                    guard->onImageLoaded(filePath, image);
                }
            }, Qt::QueuedConnection);
        }
    });
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
        [captureManager = m_captureManager](const QString& entryId) {
            return captureManager && captureManager->startHistoryReplay(entryId);
        },
        this);
    m_historyWindow->show();
    m_historyWindow->raise();
    m_historyWindow->activateWindow();
}

void MainApplication::onImageLoaded(const QString &filePath, const QImage &image)
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
    QScreen *screen = QGuiApplication::primaryScreen();
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
    PinWindow *pinWindow = m_pinWindowManager->createPinWindow(pixmap, placement.position);
    if (pinWindow && placement.zoomLevel < 1.0) {
        pinWindow->setZoomLevel(placement.zoomLevel);
    }
}

void MainApplication::onSettings()
{
    // If window already open, bring it to front
    if (m_settingsWindow && m_settingsWindow->isVisible()) {
        m_settingsWindow->raise();
        m_settingsWindow->activateWindow();
        return;
    }

    // Clean up any previous (closed) settings window
    delete m_settingsWindow;

    // Create QML settings window
    m_settingsWindow = new SnapTray::QmlSettingsWindow(this);

    // Connect OCR languages change signal
    connect(m_settingsWindow, &SnapTray::QmlSettingsWindow::ocrLanguagesChanged,
        m_pinWindowManager, &PinWindowManager::updateOcrLanguages);
#ifdef SNAPTRAY_ENABLE_MCP
    connect(m_settingsWindow, &SnapTray::QmlSettingsWindow::mcpEnabledChanged,
        this, &MainApplication::onMcpEnabledChanged);
#endif

    m_settingsWindow->show();
}

#ifdef SNAPTRAY_ENABLE_MCP
void MainApplication::onMcpEnabledChanged(bool enabled)
{
    if (enabled) {
        startMcpServer();
    }
    else {
        stopMcpServer();
    }
}
#endif

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
        onFullScreenRecording();
        break;
    default:
        break;
    }
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

        m_pinWindowManager->createPinWindow(pixmap, position);
    }
}

void MainApplication::showRecordingPreview(const QString& videoPath, int defaultOutputFormat)
{
    // Prevent multiple preview windows
    if (m_previewBackend)
        return;

    m_previewBackend = new RecordingPreviewBackend(videoPath, this);
    m_previewBackend->setDefaultOutputFormat(defaultOutputFormat);

    connect(m_previewBackend, &RecordingPreviewBackend::saveRequested,
        this, &MainApplication::onPreviewSaveRequested);
    connect(m_previewBackend, &RecordingPreviewBackend::discardRequested,
        this, &MainApplication::onPreviewDiscardRequested);
    connect(m_previewBackend, &RecordingPreviewBackend::closed,
        this, [this](bool saved) {
            m_recordingManager->onPreviewClosed(saved);
            m_previewBackend->deleteLater();
            m_previewBackend = nullptr;
        });

    m_previewBackend->show();
}

void MainApplication::onPreviewSaveRequested(const QString& videoPath)
{
    m_recordingManager->triggerSaveDialog(videoPath);
}

void MainApplication::onPreviewDiscardRequested(const QString& videoPath)
{
    if (!videoPath.isEmpty() && QFile::exists(videoPath)) {
        if (!QFile::remove(videoPath)) {
            qWarning() << "MainApplication: Failed to delete temp file:" << videoPath;
        }
    }
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
    QString displayHotkey = SnapTray::HotkeyManager::formatKeySequence(config.keySequence);
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
    const QString displayHotkey = SnapTray::HotkeyManager::formatKeySequence(config.keySequence);
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
    updateActionHotkeyText(m_screenCanvasAction,
                           SnapTray::HotkeyAction::ScreenCanvas,
                           tr("Screen Canvas"));
    updateActionHotkeyText(m_pinFromImageAction,
                           SnapTray::HotkeyAction::PinFromImage,
                           tr("Pin from Image..."));
    updateActionHotkeyText(m_historyAction,
                           SnapTray::HotkeyAction::HistoryWindow,
                           tr("History"));
    updateActionHotkeyText(m_fullScreenRecordingAction,
                           SnapTray::HotkeyAction::RecordFullScreen,
                           tr("Record Full Screen"));
}

void MainApplication::updateTrayToolTip()
{
    if (!m_trayIcon) {
        return;
    }

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
        notSetText));
#else
    m_trayIcon->setToolTip(tr("SnapTray - Screenshot Utility"));
#endif
}
