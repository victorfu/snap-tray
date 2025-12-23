#include "MainApplication.h"
#include "SettingsDialog.h"
#include "CaptureManager.h"
#include "PinWindowManager.h"
#include "ScreenCanvasManager.h"
#include "RecordingManager.h"
#include "PlatformFeatures.h"

#include <QSettings>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QApplication>
#include <QScreen>
#include <QHotkey>
#include <QDebug>

MainApplication::MainApplication(QObject* parent)
    : QObject(parent)
    , m_trayIcon(nullptr)
    , m_trayMenu(nullptr)
    , m_regionHotkey(nullptr)
    , m_screenCanvasHotkey(nullptr)
    , m_recordingHotkey(nullptr)
    , m_captureManager(nullptr)
    , m_pinWindowManager(nullptr)
    , m_screenCanvasManager(nullptr)
    , m_recordingManager(nullptr)
    , m_regionCaptureAction(nullptr)
    , m_screenCanvasAction(nullptr)
    , m_screenRecordingAction(nullptr)
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
            this, [this](const QString &path) {
                m_trayIcon->showMessage("Recording Saved",
                    QString("Saved to: %1").arg(path),
                    QSystemTrayIcon::Information, 3000);
            });

    connect(m_recordingManager, &RecordingManager::recordingError,
            this, [this](const QString &error) {
                m_trayIcon->showMessage("Recording Error",
                    error,
                    QSystemTrayIcon::Warning, 5000);
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

    m_screenRecordingAction = m_trayMenu->addAction("Screen Recording (F3)");
    connect(m_screenRecordingAction, &QAction::triggered, this, &MainApplication::onScreenRecording);

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

    // Close any open popup menus to prevent focus conflicts
    // (e.g., PinWindow context menu open when F2 is pressed)
    if (QWidget *popup = QApplication::activePopupWidget()) {
        popup->close();
    }

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
    if (QWidget *popup = QApplication::activePopupWidget()) {
        popup->close();
    }

    qDebug() << "Screen canvas triggered";
    m_screenCanvasManager->toggle();
}

void MainApplication::onScreenRecording()
{
    // Don't trigger if capture is active
    if (m_captureManager->isActive()) {
        qDebug() << "Screen recording blocked: Capture is active";
        return;
    }

    // Don't trigger if screen canvas is active
    if (m_screenCanvasManager->isActive()) {
        qDebug() << "Screen recording blocked: Screen canvas is active";
        return;
    }

    // Close any open popup menus to prevent focus conflicts
    if (QWidget *popup = QApplication::activePopupWidget()) {
        popup->close();
    }

    // Toggle recording
    if (m_recordingManager->isRecording()) {
        qDebug() << "Stopping screen recording";
        m_recordingManager->stopRecording();
    } else {
        qDebug() << "Starting screen recording";
        m_recordingManager->startRegionSelection();
    }
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

    // Setup Screen Recording hotkey (F3)
    m_recordingHotkey = new QHotkey(QKeySequence("F3"), true, this);

    if (m_recordingHotkey->isRegistered()) {
        qDebug() << "Screen recording hotkey registered: F3";
    }
    else {
        qDebug() << "Failed to register screen recording hotkey: F3";
    }

    connect(m_recordingHotkey, &QHotkey::activated, this, &MainApplication::onScreenRecording);

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
        QSettings settings("Victor Fu", "SnapTray");
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
        QSettings settings("Victor Fu", "SnapTray");
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

void MainApplication::updateTrayMenuHotkeyText(const QString& hotkey)
{
    if (m_regionCaptureAction) {
        m_regionCaptureAction->setText(QString("Region Capture (%1)").arg(hotkey));
    }
    if (m_screenCanvasAction) {
        QString screenCanvasHotkey = SettingsDialog::loadScreenCanvasHotkey();
        m_screenCanvasAction->setText(QString("Screen Canvas (%1)").arg(screenCanvasHotkey));
    }
}
