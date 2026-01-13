#include "MainApplication.h"
#include "SettingsDialog.h"
#include "CaptureManager.h"
#include "PinWindowManager.h"
#include "ScreenCanvasManager.h"
#include "RecordingManager.h"
#include "PlatformFeatures.h"
#include "settings/Settings.h"
#include "video/RecordingPreviewWindow.h"

#include <QFile>
#include <QPointer>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QApplication>
#include <QScreen>
#include <QHotkey>
#include <QTimer>
#include <QDebug>
#include <QClipboard>
#include <QMimeData>
#include <QPainter>
#include <QFont>
#include <QFontMetrics>

MainApplication::MainApplication(QObject* parent)
    : QObject(parent)
    , m_trayIcon(nullptr)
    , m_trayMenu(nullptr)
    , m_regionHotkey(nullptr)
    , m_screenCanvasHotkey(nullptr)
    , m_pasteHotkey(nullptr)
    , m_captureManager(nullptr)
    , m_pinWindowManager(nullptr)
    , m_screenCanvasManager(nullptr)
    , m_recordingManager(nullptr)
    , m_regionCaptureAction(nullptr)
    , m_screenCanvasAction(nullptr)
    , m_fullScreenRecordingAction(nullptr)
    , m_settingsDialog(nullptr)
{
}

MainApplication::~MainApplication()
{
    delete m_trayMenu;
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
        this, [this](const QString& path) {
            m_trayIcon->showMessage("Recording Saved",
                QString("Saved to: %1").arg(path),
                QSystemTrayIcon::Information, 3000);
        });

    connect(m_recordingManager, &RecordingManager::recordingError,
        this, [this](const QString& error) {
            m_trayIcon->showMessage("Recording Error",
                error,
                QSystemTrayIcon::Warning, 5000);
        });

    // Connect preview request signal
    connect(m_recordingManager, &RecordingManager::previewRequested,
        this, &MainApplication::showRecordingPreview);
    connect(m_recordingManager, &RecordingManager::annotatePreviewRequested,
        this, &MainApplication::showRecordingPreviewForAnnotation);

    // Connect capture manager's recording request to recording manager
    connect(m_captureManager, &CaptureManager::recordingRequested,
        m_recordingManager, &RecordingManager::startRegionSelectionWithPreset);

    // Connect scrolling capture request from RegionSelector toolbar
    connect(m_captureManager, &CaptureManager::scrollingCaptureRequested,
        m_captureManager, &CaptureManager::startScrollingCaptureWithRegion);

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

#ifdef SNAPTRAY_ENABLE_DEV_FEATURES
    m_trayMenu->addSeparator();

    m_fullScreenRecordingAction = m_trayMenu->addAction("Record Full Screen");
    connect(m_fullScreenRecordingAction, &QAction::triggered, this, &MainApplication::onFullScreenRecording);
#endif

    m_trayMenu->addSeparator();

    QAction* closeAllPinsAction = m_trayMenu->addAction("Close All Pins");
    connect(closeAllPinsAction, &QAction::triggered, this, &MainApplication::onCloseAllPins);

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
        this, [this](bool success, const QString& message) {
            qDebug() << "MainApplication: OCR completed, success=" << success << ", message=" << message;
            m_trayIcon->showMessage(
                success ? tr("OCR Success") : tr("OCR Failed"),
                message,
                success ? QSystemTrayIcon::Information : QSystemTrayIcon::Warning,
                3000);
        });

    // Setup global hotkeys
    setupHotkey();

    qDebug() << "SnapTray initialized and running in system tray";
}

void MainApplication::onRegionCapture()
{
    // Don't trigger if screen canvas is active
    if (m_screenCanvasManager->isActive()) {
        qDebug() << "Region capture blocked: Screen canvas is active";
        return;
    }

    // Don't trigger if recording is active
    if (m_recordingManager->isActive()) {
        qDebug() << "Region capture blocked: Recording is active";
        return;
    }

    // Note: Don't close popup menus - allow capturing them (like Snipaste)
    qDebug() << "Region capture triggered";
    m_captureManager->startRegionCapture();
}

void MainApplication::onScreenCanvas()
{
    // Don't trigger if capture is active
    if (m_captureManager->isActive()) {
        qDebug() << "Screen canvas blocked: Capture is active";
        return;
    }

    // Don't trigger if recording is active
    if (m_recordingManager->isActive()) {
        qDebug() << "Screen canvas blocked: Recording is active";
        return;
    }

    // Close any open popup menus to prevent focus conflicts
    if (QWidget* popup = QApplication::activePopupWidget()) {
        popup->close();
    }

    qDebug() << "Screen canvas triggered";
    m_screenCanvasManager->toggle();
}

void MainApplication::onFullScreenRecording()
{
    // Don't trigger if screen canvas is active
    if (m_screenCanvasManager->isActive()) {
        qDebug() << "Full-screen recording blocked: Screen canvas is active";
        return;
    }

    // Don't trigger if already recording
    if (m_recordingManager->isActive()) {
        qDebug() << "Full-screen recording blocked: Recording is already active";
        return;
    }

    // Don't trigger if capture is active
    if (m_captureManager->isActive()) {
        qDebug() << "Full-screen recording blocked: Capture is active";
        return;
    }

    // Close any open popup menus to prevent focus conflicts
    if (QWidget* popup = QApplication::activePopupWidget()) {
        popup->close();
    }

    qDebug() << "Full-screen recording triggered";
    m_recordingManager->startFullScreenRecording();
}

void MainApplication::onCloseAllPins()
{
    qDebug() << "Closing all pin windows";
    m_pinWindowManager->closeAllWindows();
}

void MainApplication::onSettings()
{
    qDebug() << "Settings triggered";

    // If dialog already open, bring it to front
    if (m_settingsDialog) {
        m_settingsDialog->raise();
        m_settingsDialog->activateWindow();
        return;
    }

    // Create non-modal dialog
    m_settingsDialog = new SettingsDialog();
    m_settingsDialog->setAttribute(Qt::WA_DeleteOnClose);

    // Show current hotkey registration status
    m_settingsDialog->updateCaptureHotkeyStatus(m_regionHotkey->isRegistered());
    m_settingsDialog->updateScreenCanvasHotkeyStatus(m_screenCanvasHotkey->isRegistered());
    m_settingsDialog->updatePasteHotkeyStatus(m_pasteHotkey->isRegistered());

    // Connect hotkey change signal
    connect(m_settingsDialog, &SettingsDialog::hotkeyChangeRequested,
        this, [this](const QString& newHotkey) {
            bool success = updateHotkey(newHotkey);
            if (m_settingsDialog) {
                m_settingsDialog->updateCaptureHotkeyStatus(success);
                if (!success) {
                    m_settingsDialog->showHotkeyError(
                        "Failed to register capture hotkey. It may be in use by another application.");
                }
            }
        });

    // Connect screen canvas hotkey change signal
    connect(m_settingsDialog, &SettingsDialog::screenCanvasHotkeyChangeRequested,
        this, [this](const QString& newHotkey) {
            bool success = updateScreenCanvasHotkey(newHotkey);
            if (m_settingsDialog) {
                m_settingsDialog->updateScreenCanvasHotkeyStatus(success);
                if (!success) {
                    m_settingsDialog->showHotkeyError(
                        "Failed to register screen canvas hotkey. It may be in use by another application.");
                }
            }
        });

    // Connect paste hotkey change signal
    connect(m_settingsDialog, &SettingsDialog::pasteHotkeyChangeRequested,
        this, [this](const QString& newHotkey) {
            bool success = updatePasteHotkey(newHotkey);
            if (m_settingsDialog) {
                m_settingsDialog->updatePasteHotkeyStatus(success);
                if (!success) {
                    m_settingsDialog->showHotkeyError(
                        "Failed to register paste hotkey. It may be in use by another application.");
                }
            }
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

void MainApplication::setupHotkey()
{
    QStringList failedHotkeys;

    // Load region capture hotkey from settings (default is F2)
    QString regionKeySequence = SettingsDialog::loadHotkey();
    m_regionHotkey = new QHotkey(QKeySequence(regionKeySequence), true, this);

    if (m_regionHotkey->isRegistered()) {
        qDebug() << "Region hotkey registered:" << regionKeySequence;
    }
    else {
        qDebug() << "Failed to register region hotkey:" << regionKeySequence;
        failedHotkeys << QString("Region Capture (%1)").arg(regionKeySequence);
    }

    // Connect region hotkey directly to region capture
    connect(m_regionHotkey, &QHotkey::activated, this, &MainApplication::onRegionCapture);

    // Load Screen Canvas hotkey from settings (default is Ctrl+F2)
    QString screenCanvasKeySequence = SettingsDialog::loadScreenCanvasHotkey();
    m_screenCanvasHotkey = new QHotkey(QKeySequence(screenCanvasKeySequence), true, this);

    if (m_screenCanvasHotkey->isRegistered()) {
        qDebug() << "Screen canvas hotkey registered:" << screenCanvasKeySequence;
    }
    else {
        qDebug() << "Failed to register screen canvas hotkey:" << screenCanvasKeySequence;
        failedHotkeys << QString("Screen Canvas (%1)").arg(screenCanvasKeySequence);
    }

    connect(m_screenCanvasHotkey, &QHotkey::activated, this, &MainApplication::onScreenCanvas);

    // Load Paste hotkey from settings (default is F8)
    QString pasteKeySequence = SettingsDialog::loadPasteHotkey();
    m_pasteHotkey = new QHotkey(QKeySequence(pasteKeySequence), true, this);

    if (m_pasteHotkey->isRegistered()) {
        qDebug() << "Paste hotkey registered:" << pasteKeySequence;
    }
    else {
        qDebug() << "Failed to register paste hotkey:" << pasteKeySequence;
        failedHotkeys << QString("Paste (%1)").arg(pasteKeySequence);
    }

    connect(m_pasteHotkey, &QHotkey::activated, this, &MainApplication::onPasteFromClipboard);

    // Update tray menu text with current hotkey
    updateTrayMenuHotkeyText(regionKeySequence);

    // Show notification if any hotkeys failed to register
    if (!failedHotkeys.isEmpty()) {
        QString message = failedHotkeys.join(", ") +
            " hotkey failed to register. It may be in use by another application.";
        m_trayIcon->showMessage("Hotkey Registration Failed", message,
            QSystemTrayIcon::Warning, 5000);
    }
}

bool MainApplication::updateHotkey(const QString& newHotkey)
{
    qDebug() << "Updating hotkey to:" << newHotkey;

    // 保存舊熱鍵以便回復
    QKeySequence oldShortcut = m_regionHotkey->shortcut();

    // Unregister old hotkey
    m_regionHotkey->setRegistered(false);

    // Set new key sequence
    m_regionHotkey->setShortcut(QKeySequence(newHotkey));

    // Re-register
    m_regionHotkey->setRegistered(true);

    if (m_regionHotkey->isRegistered()) {
        qDebug() << "Hotkey updated and registered:" << newHotkey;
        updateTrayMenuHotkeyText(newHotkey);

        // Save only after successful registration
        auto settings = SnapTray::getSettings();
        settings.setValue("hotkey", newHotkey);

        return true;
    }
    else {
        qDebug() << "Failed to register new hotkey:" << newHotkey << ", reverting...";

        // 回復舊熱鍵
        m_regionHotkey->setShortcut(oldShortcut);
        m_regionHotkey->setRegistered(true);

        if (m_regionHotkey->isRegistered()) {
            qDebug() << "Reverted to old hotkey:" << oldShortcut.toString();
        }
        else {
            qDebug() << "Critical: Failed to restore old hotkey!";
        }

        return false;
    }
}

bool MainApplication::updateScreenCanvasHotkey(const QString& newHotkey)
{
    qDebug() << "Updating screen canvas hotkey to:" << newHotkey;

    // Save old hotkey for reverting
    QKeySequence oldShortcut = m_screenCanvasHotkey->shortcut();

    // Unregister old hotkey
    m_screenCanvasHotkey->setRegistered(false);

    // Set new key sequence
    m_screenCanvasHotkey->setShortcut(QKeySequence(newHotkey));

    // Re-register
    m_screenCanvasHotkey->setRegistered(true);

    if (m_screenCanvasHotkey->isRegistered()) {
        qDebug() << "Screen canvas hotkey updated and registered:" << newHotkey;

        // Update tray menu
        if (m_screenCanvasAction) {
            m_screenCanvasAction->setText(QString("Screen Canvas (%1)").arg(newHotkey));
        }

        // Save only after successful registration
        auto settings = SnapTray::getSettings();
        settings.setValue("screenCanvasHotkey", newHotkey);

        return true;
    }
    else {
        qDebug() << "Failed to register new screen canvas hotkey:" << newHotkey << ", reverting...";

        // Revert to old hotkey
        m_screenCanvasHotkey->setShortcut(oldShortcut);
        m_screenCanvasHotkey->setRegistered(true);

        if (m_screenCanvasHotkey->isRegistered()) {
            qDebug() << "Reverted to old screen canvas hotkey:" << oldShortcut.toString();
        }
        else {
            qDebug() << "Critical: Failed to restore old screen canvas hotkey!";
        }

        return false;
    }
}

bool MainApplication::updatePasteHotkey(const QString& newHotkey)
{
    qDebug() << "Updating paste hotkey to:" << newHotkey;

    // Save old hotkey for reverting
    QKeySequence oldShortcut = m_pasteHotkey->shortcut();

    // Unregister old hotkey
    m_pasteHotkey->setRegistered(false);

    // Set new key sequence
    m_pasteHotkey->setShortcut(QKeySequence(newHotkey));

    // Re-register
    m_pasteHotkey->setRegistered(true);

    if (m_pasteHotkey->isRegistered()) {
        qDebug() << "Paste hotkey updated and registered:" << newHotkey;

        // Save only after successful registration
        auto settings = SnapTray::getSettings();
        settings.setValue("pasteHotkey", newHotkey);

        return true;
    }
    else {
        qDebug() << "Failed to register new paste hotkey:" << newHotkey << ", reverting...";

        // Revert to old hotkey
        m_pasteHotkey->setShortcut(oldShortcut);
        m_pasteHotkey->setRegistered(true);

        if (m_pasteHotkey->isRegistered()) {
            qDebug() << "Reverted to old paste hotkey:" << oldShortcut.toString();
        }
        else {
            qDebug() << "Critical: Failed to restore old paste hotkey!";
        }

        return false;
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
    const int kMaxPixmapWidth = 800;
    const int kMaxPixmapHeight = 600;
    maxWidth = qMin(maxWidth, kMaxPixmapWidth);
    totalHeight = qMin(totalHeight, kMaxPixmapHeight);

    // Create pixmap with padding
    int padding = 16;
    qreal dpr = qApp->devicePixelRatio();
    QSize pixmapSize(maxWidth + 2 * padding, totalHeight + 2 * padding);

    QPixmap pixmap(pixmapSize * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(QColor(255, 255, 240));  // Light yellow background (sticky note style)

    // Draw text
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setFont(font);
    painter.setPen(QColor(40, 40, 40));  // Dark text

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
    qDebug() << "Paste hotkey activated";

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
        qDebug() << "Created pin window from clipboard at screen center" << position;
    }
}

void MainApplication::showRecordingPreview(const QString& videoPath)
{
    qDebug() << "MainApplication: Showing recording preview for:" << videoPath;

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

void MainApplication::showRecordingPreviewForAnnotation(const QString& videoPath)
{
    showRecordingPreview(videoPath);
    if (m_previewWindow) {
        QTimer::singleShot(0, m_previewWindow, [this]() {
            if (m_previewWindow) {
                m_previewWindow->switchToAnnotateMode();
            }
        });
    }
}

void MainApplication::onPreviewSaveRequested(const QString& videoPath)
{
    qDebug() << "MainApplication: Preview save requested for:" << videoPath;

    // Close preview window - this will trigger closed(true) signal
    // which will call onPreviewClosed(true) on RecordingManager
    // Then we trigger the save dialog
    if (m_previewWindow) {
        m_previewWindow = nullptr;  // Clear pointer before close
    }

    // Trigger the save dialog on RecordingManager
    m_recordingManager->triggerSaveDialog(videoPath);
}

void MainApplication::onPreviewDiscardRequested()
{
    qDebug() << "MainApplication: Preview discard requested";

    // Get the video path before closing window
    QString videoPath;
    if (m_previewWindow) {
        videoPath = m_previewWindow->videoPath();
    }

    // Delete temp file
    if (!videoPath.isEmpty() && QFile::exists(videoPath)) {
        if (QFile::remove(videoPath)) {
            qDebug() << "MainApplication: Deleted temp file:" << videoPath;
        }
        else {
            qWarning() << "MainApplication: Failed to delete temp file:" << videoPath;
        }
    }

    // Close will happen via RecordingPreviewWindow::onDiscardClicked()
}

void MainApplication::updateTrayMenuHotkeyText(const QString& hotkey)
{
    if (m_regionCaptureAction) {
        // Convert to native display format (⌘ on macOS, Ctrl on Windows)
        QString displayHotkey = QKeySequence(hotkey).toString(QKeySequence::NativeText);
        m_regionCaptureAction->setText(QString("Region Capture (%1)").arg(displayHotkey));
    }
    if (m_screenCanvasAction) {
        QString screenCanvasHotkey = SettingsDialog::loadScreenCanvasHotkey();
        QString displayHotkey = QKeySequence(screenCanvasHotkey).toString(QKeySequence::NativeText);
        m_screenCanvasAction->setText(QString("Screen Canvas (%1)").arg(displayHotkey));
    }
}
