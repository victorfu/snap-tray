#include "MainApplication.h"
#include "SettingsDialog.h"
#include "CaptureManager.h"
#include "PinWindowManager.h"
#include "PinWindow.h"
#include "ScreenCanvasManager.h"
#include "RecordingManager.h"
#include "PlatformFeatures.h"
#include "hotkey/HotkeyManager.h"
#include "ui/GlobalToast.h"
#include "video/RecordingPreviewWindow.h"

#include <QFile>
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
#include <QPainter>
#include <QFont>
#include <QFontMetrics>
#include <QtConcurrent>

namespace {
// Text pixmap rendering constants (for paste-as-text feature)
constexpr int kMaxTextPixmapWidth = 800;
constexpr int kMaxTextPixmapHeight = 600;
constexpr int kTextPixmapPadding = 16;
const QColor kStickyNoteBackground{255, 255, 240};  // Light yellow
const QColor kTextAnnotationForeground{40, 40, 40}; // Dark gray text

// Toast timeout
constexpr int kUIToastTimeout = 2000;
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
    , m_fullScreenRecordingAction(nullptr)
    , m_settingsDialog(nullptr)
{
}

MainApplication::~MainApplication()
{
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
            GlobalToast::instance().showToast(GlobalToast::Success,
                MainApplication::tr("Recording Saved"),
                QString("Saved to: %1").arg(path));
        });

    connect(m_recordingManager, &RecordingManager::recordingError,
        this, [](const QString& error) {
            GlobalToast::instance().showToast(GlobalToast::Error,
                MainApplication::tr("Recording Error"),
                error, 5000);
        });

    // Connect screenshot save signals from CaptureManager
    connect(m_captureManager, &CaptureManager::saveCompleted,
        this, [](const QPixmap&, const QString& path) {
            GlobalToast::instance().showToast(GlobalToast::Success,
                MainApplication::tr("Screenshot Saved"),
                QString("Saved to: %1").arg(path));
        });
    connect(m_captureManager, &CaptureManager::saveFailed,
        this, [](const QString& path, const QString& error) {
            GlobalToast::instance().showToast(GlobalToast::Error,
                MainApplication::tr("Screenshot Save Failed"),
                QString("%1\n%2").arg(error).arg(path), 5000);
        });

    // Connect screenshot save signals from PinWindowManager
    connect(m_pinWindowManager, &PinWindowManager::saveCompleted,
        this, [](const QPixmap&, const QString& path) {
            GlobalToast::instance().showToast(GlobalToast::Success,
                MainApplication::tr("Screenshot Saved"),
                QString("Saved to: %1").arg(path));
        });
    connect(m_pinWindowManager, &PinWindowManager::saveFailed,
        this, [](const QString& path, const QString& error) {
            GlobalToast::instance().showToast(GlobalToast::Error,
                MainApplication::tr("Screenshot Save Failed"),
                QString("%1\n%2").arg(error).arg(path), 5000);
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

    m_regionCaptureAction = m_trayMenu->addAction("Region Capture");
    connect(m_regionCaptureAction, &QAction::triggered, this, &MainApplication::onRegionCapture);

    m_screenCanvasAction = m_trayMenu->addAction("Screen Canvas");
    connect(m_screenCanvasAction, &QAction::triggered, this, &MainApplication::onScreenCanvas);

    m_pinFromImageAction = m_trayMenu->addAction("Pin from Image...");
    connect(m_pinFromImageAction, &QAction::triggered, this, &MainApplication::onPinFromImage);

    QAction* closeAllPinsAction = m_trayMenu->addAction("Close All Pins");
    connect(closeAllPinsAction, &QAction::triggered, this, &MainApplication::onCloseAllPins);

    m_fullScreenRecordingAction = m_trayMenu->addAction("Record Full Screen");
    connect(m_fullScreenRecordingAction, &QAction::triggered, this, &MainApplication::onFullScreenRecording);

    m_trayMenu->addSeparator();

    QAction* settingsAction = m_trayMenu->addAction("Settings");
    connect(settingsAction, &QAction::triggered, this, &MainApplication::onSettings);

    m_trayMenu->addSeparator();

    QAction* exitAction = m_trayMenu->addAction("Exit");
    connect(exitAction, &QAction::triggered, qApp, &QCoreApplication::quit);

    // Set menu and show tray icon
    m_trayIcon->setContextMenu(m_trayMenu);
    m_trayIcon->setToolTip("SnapTray - Screenshot Utility");
    m_trayIcon->show();

    // Connect OCR completion signal (must be after m_trayIcon is created)
    connect(m_pinWindowManager, &PinWindowManager::ocrCompleted,
        this, [](bool success, const QString& message) {
            GlobalToast::instance().showToast(
                success ? GlobalToast::Success : GlobalToast::Error,
                success ? MainApplication::tr("OCR Success") : MainApplication::tr("OCR Failed"),
                message);
        });

    // Initialize hotkey manager and connect action signal
    SnapTray::HotkeyManager::instance().initialize();
    connect(&SnapTray::HotkeyManager::instance(), &SnapTray::HotkeyManager::actionTriggered,
            this, &MainApplication::onHotkeyAction);

    // Update tray menu with current hotkey text
    updateTrayMenuHotkeyText();
}

void MainApplication::onRegionCapture()
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
    m_captureManager->startRegionCapture();
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
        return;
    }

    // Don't trigger if recording is active
    if (m_recordingManager->isActive()) {
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
    GlobalToast::instance().showToast(
        GlobalToast::Info,
        tr("Loading Image"),
        QFileInfo(filePath).fileName()
    );

    // Load image asynchronously (QImage is thread-safe, QPixmap is not)
    QPointer<MainApplication> guard(this);
    (void)QtConcurrent::run([filePath, guard]() {
        QImage image(filePath);

        // Call back to UI thread
        if (guard) {
            QMetaObject::invokeMethod(guard, "onImageLoaded",
                Qt::QueuedConnection,
                Q_ARG(QString, filePath),
                Q_ARG(QImage, image));
        }
    });
}

void MainApplication::onImageLoaded(const QString &filePath, const QImage &image)
{
    if (image.isNull()) {
        QFileInfo fileInfo(filePath);
        GlobalToast::instance().showToast(
            GlobalToast::Error,
            tr("Failed to Load Image"),
            fileInfo.fileName()
        );
        return;
    }

    // Convert QImage to QPixmap (must be on UI thread)
    QPixmap pixmap = QPixmap::fromImage(image);

    // Get screen geometry
    QScreen *screen = QGuiApplication::primaryScreen();
    QRect screenGeometry = screen->availableGeometry();

    // Calculate logical image size (HiDPI support)
    QSize logicalSize = pixmap.size() / pixmap.devicePixelRatio();

    // Calculate zoom level to fit screen (with margin)
    constexpr qreal kScreenMargin = 0.9;
    qreal zoomLevel = 1.0;

    if (logicalSize.width() > screenGeometry.width() * kScreenMargin ||
        logicalSize.height() > screenGeometry.height() * kScreenMargin) {
        qreal scaleX = (screenGeometry.width() * kScreenMargin) / logicalSize.width();
        qreal scaleY = (screenGeometry.height() * kScreenMargin) / logicalSize.height();
        zoomLevel = qMin(scaleX, scaleY);
        zoomLevel = qMax(zoomLevel, 0.1);
    }

    // Calculate display size and center position
    QSize displaySize = logicalSize * zoomLevel;
    QPoint position(
        screenGeometry.center().x() - displaySize.width() / 2,
        screenGeometry.center().y() - displaySize.height() / 2
    );

    // Create pin window and apply zoom if needed
    PinWindow *pinWindow = m_pinWindowManager->createPinWindow(pixmap, position);
    if (pinWindow && zoomLevel < 1.0) {
        pinWindow->setZoomLevel(zoomLevel);
    }
}

void MainApplication::onSettings()
{
    // If dialog already open, bring it to front
    if (m_settingsDialog) {
        m_settingsDialog->raise();
        m_settingsDialog->activateWindow();
        return;
    }

    // Create non-modal dialog
    m_settingsDialog = new SettingsDialog();
    m_settingsDialog->setAttribute(Qt::WA_DeleteOnClose);

    // Connect OCR languages change signal
    connect(m_settingsDialog, &SettingsDialog::ocrLanguagesChanged,
        m_pinWindowManager, &PinWindowManager::updateOcrLanguages);

    // Update tray menu when hotkeys change
    connect(&SnapTray::HotkeyManager::instance(), &SnapTray::HotkeyManager::hotkeyChanged,
        this, [this](SnapTray::HotkeyAction, const SnapTray::HotkeyConfig&) {
            updateTrayMenuHotkeyText();
        });

    // Clean up pointer when dialog is destroyed (WA_DeleteOnClose triggers this)
    connect(m_settingsDialog, &QDialog::destroyed,
        this, [this]() {
            m_settingsDialog = nullptr;
        });

    // Show non-modal
    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

void MainApplication::onHotkeyAction(SnapTray::HotkeyAction action)
{
    using namespace SnapTray;

    switch (action) {
    case HotkeyAction::RegionCapture:
        onRegionCapture();
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
    case HotkeyAction::RecordFullScreen:
        onFullScreenRecording();
        break;
    default:
        break;
    }
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

    QPixmap pixmap(pixmapSize * dpr);
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
        QRect screenGeometry = screen->geometry();

        // Use logical size for centering (HiDPI support)
        QSize logicalSize = pixmap.size() / pixmap.devicePixelRatio();
        QPoint position = screenGeometry.center() - QPoint(logicalSize.width() / 2, logicalSize.height() / 2);

        m_pinWindowManager->createPinWindow(pixmap, position);
    }
}

void MainApplication::showRecordingPreview(const QString& videoPath)
{
    // Prevent multiple preview windows
    if (m_previewWindow) {
        m_previewWindow->raise();
        m_previewWindow->activateWindow();
        return;
    }

    m_previewWindow = new RecordingPreviewWindow(videoPath);

    // Connect save/discard signals
    connect(m_previewWindow, &RecordingPreviewWindow::saveRequested,
        this, &MainApplication::onPreviewSaveRequested);
    connect(m_previewWindow, &RecordingPreviewWindow::discardRequested,
        this, &MainApplication::onPreviewDiscardRequested);
    connect(m_previewWindow, &RecordingPreviewWindow::closed,
        this, [this](bool saved) {
            m_recordingManager->onPreviewClosed(saved);
            m_previewWindow = nullptr;
        });

    m_previewWindow->show();
    m_previewWindow->raise();
    m_previewWindow->activateWindow();
}

void MainApplication::onPreviewSaveRequested(const QString& videoPath)
{
    // Trigger the save dialog on RecordingManager
    // Note: The preview window closes itself via close() after emitting saveRequested,
    // and the closed signal handler already sets m_previewWindow = nullptr
    m_recordingManager->triggerSaveDialog(videoPath);
}

void MainApplication::onPreviewDiscardRequested()
{
    // Get the video path before closing window
    QString videoPath;
    if (m_previewWindow) {
        videoPath = m_previewWindow->videoPath();
    }

    // Delete temp file
    if (!videoPath.isEmpty() && QFile::exists(videoPath)) {
        if (!QFile::remove(videoPath)) {
            qWarning() << "MainApplication: Failed to delete temp file:" << videoPath;
        }
    }

    // Close will happen via RecordingPreviewWindow::onDiscardClicked()
}

void MainApplication::updateTrayMenuHotkeyText()
{
    using namespace SnapTray;
    auto& mgr = HotkeyManager::instance();

    if (m_regionCaptureAction) {
        auto config = mgr.getConfig(HotkeyAction::RegionCapture);
        QString displayHotkey = HotkeyManager::formatKeySequence(config.keySequence);
        if (!displayHotkey.isEmpty()) {
            m_regionCaptureAction->setText(QString("Region Capture (%1)").arg(displayHotkey));
        } else {
            m_regionCaptureAction->setText(QStringLiteral("Region Capture"));
        }
    }

    if (m_screenCanvasAction) {
        auto config = mgr.getConfig(HotkeyAction::ScreenCanvas);
        QString displayHotkey = HotkeyManager::formatKeySequence(config.keySequence);
        if (!displayHotkey.isEmpty()) {
            m_screenCanvasAction->setText(QString("Screen Canvas (%1)").arg(displayHotkey));
        } else {
            m_screenCanvasAction->setText(QStringLiteral("Screen Canvas"));
        }
    }

    if (m_pinFromImageAction) {
        auto config = mgr.getConfig(HotkeyAction::PinFromImage);
        QString displayHotkey = HotkeyManager::formatKeySequence(config.keySequence);
        if (!displayHotkey.isEmpty()) {
            m_pinFromImageAction->setText(QString("Pin from Image... (%1)").arg(displayHotkey));
        } else {
            m_pinFromImageAction->setText(QStringLiteral("Pin from Image..."));
        }
    }

    if (m_fullScreenRecordingAction) {
        auto config = mgr.getConfig(HotkeyAction::RecordFullScreen);
        QString displayHotkey = HotkeyManager::formatKeySequence(config.keySequence);
        if (!displayHotkey.isEmpty()) {
            m_fullScreenRecordingAction->setText(QString("Record Full Screen (%1)").arg(displayHotkey));
        } else {
            m_fullScreenRecordingAction->setText(QStringLiteral("Record Full Screen"));
        }
    }
}
