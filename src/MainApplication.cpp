#include "MainApplication.h"
#include "SettingsDialog.h"
#include "CaptureManager.h"
#include "PinWindowManager.h"
#include "ScreenCanvasManager.h"

#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QPixmap>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QHotkey>
#include <QDebug>

MainApplication::MainApplication(QObject *parent)
    : QObject(parent)
    , m_trayIcon(nullptr)
    , m_trayMenu(nullptr)
    , m_regionHotkey(nullptr)
    , m_canvasHotkey(nullptr)
    , m_captureManager(nullptr)
    , m_pinWindowManager(nullptr)
    , m_screenCanvasManager(nullptr)
    , m_regionCaptureAction(nullptr)
    , m_screenCanvasAction(nullptr)
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

    // Create tray icon with cutout lightning bolt (32x32)
    const int size = 32;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    // Create capsule/pill shape (fully rounded corners like iOS icon)
    QPainterPath bgPath;
    bgPath.addRoundedRect(0, 0, size, size, size / 2, size / 2);

    // Create bold lightning bolt path (larger and more prominent)
    QPainterPath lightningPath;
    lightningPath.moveTo(19, 3);    // top
    lightningPath.lineTo(8, 17);    // middle-left
    lightningPath.lineTo(15, 17);   // middle
    lightningPath.lineTo(13, 29);   // bottom
    lightningPath.lineTo(24, 14);   // middle-right
    lightningPath.lineTo(17, 14);   // back
    lightningPath.closeSubpath();

    // Subtract lightning from background (cutout effect)
    QPainterPath finalPath = bgPath.subtracted(lightningPath);

    // Draw the white shape with lightning cutout
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawPath(finalPath);

    painter.end();
    QIcon icon(pixmap);

    // Create system tray icon
    m_trayIcon = new QSystemTrayIcon(icon, this);

    // Create context menu
    m_trayMenu = new QMenu();

    m_regionCaptureAction = m_trayMenu->addAction("Region Capture");
    connect(m_regionCaptureAction, &QAction::triggered, this, &MainApplication::onRegionCapture);

    m_screenCanvasAction = m_trayMenu->addAction("Screen Canvas");
    connect(m_screenCanvasAction, &QAction::triggered, this, &MainApplication::onScreenCanvas);

    m_trayMenu->addSeparator();

    QAction *closeAllPinsAction = m_trayMenu->addAction("Close All Pins");
    connect(closeAllPinsAction, &QAction::triggered, this, &MainApplication::onCloseAllPins);

    m_trayMenu->addSeparator();

    QAction *settingsAction = m_trayMenu->addAction("Settings");
    connect(settingsAction, &QAction::triggered, this, &MainApplication::onSettings);

    m_trayMenu->addSeparator();

    QAction *exitAction = m_trayMenu->addAction("Exit");
    connect(exitAction, &QAction::triggered, qApp, &QCoreApplication::quit);

    // Set menu and show tray icon
    m_trayIcon->setContextMenu(m_trayMenu);
    m_trayIcon->setToolTip("SnapTray - Screenshot Utility");
    m_trayIcon->show();

    // Setup global hotkeys
    setupHotkey();
    setupCanvasHotkey();

    qDebug() << "SnapTray initialized and running in system tray";
}

void MainApplication::onRegionCapture()
{
    // Don't trigger if screen canvas is active
    if (m_screenCanvasManager->isActive()) {
        qDebug() << "Region capture blocked: Screen canvas is active";
        return;
    }
    qDebug() << "Region capture triggered";
    m_captureManager->startRegionCapture();
}

void MainApplication::onScreenCanvas()
{
    qDebug() << "Screen canvas triggered";
    m_screenCanvasManager->toggle();
}

void MainApplication::onCloseAllPins()
{
    qDebug() << "Closing all pin windows";
    m_pinWindowManager->closeAllWindows();
}

void MainApplication::onSettings()
{
    qDebug() << "Settings triggered";

    SettingsDialog dialog;
    bool captureHotkeySuccess = true;
    bool canvasHotkeySuccess = true;

    connect(&dialog, &SettingsDialog::hotkeyChangeRequested,
            this, [this, &captureHotkeySuccess](const QString &newHotkey) {
        captureHotkeySuccess = updateHotkey(newHotkey);
    });

    connect(&dialog, &SettingsDialog::canvasHotkeyChangeRequested,
            this, [this, &dialog, &captureHotkeySuccess, &canvasHotkeySuccess](const QString &newHotkey) {
        canvasHotkeySuccess = updateCanvasHotkey(newHotkey);

        // Check both hotkey results
        if (!captureHotkeySuccess) {
            dialog.showHotkeyError("Failed to register capture hotkey. It may be in use by another application.");
        } else if (!canvasHotkeySuccess) {
            dialog.showHotkeyError("Failed to register canvas hotkey. It may be in use by another application.");
        } else {
            dialog.accept();
        }
    });
    dialog.exec();
}

void MainApplication::setupHotkey()
{
    // Load hotkey from settings (default is F2)
    QString regionKeySequence = SettingsDialog::loadHotkey();
    m_regionHotkey = new QHotkey(QKeySequence(regionKeySequence), true, this);

    if (m_regionHotkey->isRegistered()) {
        qDebug() << "Region hotkey registered:" << regionKeySequence;
    } else {
        qDebug() << "Failed to register region hotkey:" << regionKeySequence;
    }

    connect(m_regionHotkey, &QHotkey::activated, this, &MainApplication::onRegionCapture);

    // Update tray menu text with current hotkey
    updateTrayMenuHotkeyText(regionKeySequence);
}

bool MainApplication::updateHotkey(const QString &newHotkey)
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
        return true;
    } else {
        qDebug() << "Failed to register new hotkey:" << newHotkey << ", reverting...";

        // 回復舊熱鍵
        m_regionHotkey->setShortcut(oldShortcut);
        m_regionHotkey->setRegistered(true);

        if (m_regionHotkey->isRegistered()) {
            qDebug() << "Reverted to old hotkey:" << oldShortcut.toString();
        } else {
            qDebug() << "Critical: Failed to restore old hotkey!";
        }

        return false;
    }
}

void MainApplication::updateTrayMenuHotkeyText(const QString &hotkey)
{
    if (m_regionCaptureAction) {
        m_regionCaptureAction->setText(QString("Region Capture (%1)").arg(hotkey));
    }
}

void MainApplication::setupCanvasHotkey()
{
    // Load canvas hotkey from settings (default is F3)
    QString canvasKeySequence = SettingsDialog::loadCanvasHotkey();
    m_canvasHotkey = new QHotkey(QKeySequence(canvasKeySequence), true, this);

    if (m_canvasHotkey->isRegistered()) {
        qDebug() << "Canvas hotkey registered:" << canvasKeySequence;
    } else {
        qDebug() << "Failed to register canvas hotkey:" << canvasKeySequence;
    }

    connect(m_canvasHotkey, &QHotkey::activated, this, &MainApplication::onScreenCanvas);

    // Update tray menu text with current hotkey
    updateTrayMenuCanvasHotkeyText(canvasKeySequence);
}

bool MainApplication::updateCanvasHotkey(const QString &newHotkey)
{
    qDebug() << "Updating canvas hotkey to:" << newHotkey;

    QKeySequence oldShortcut = m_canvasHotkey->shortcut();

    m_canvasHotkey->setRegistered(false);
    m_canvasHotkey->setShortcut(QKeySequence(newHotkey));
    m_canvasHotkey->setRegistered(true);

    if (m_canvasHotkey->isRegistered()) {
        qDebug() << "Canvas hotkey updated and registered:" << newHotkey;
        updateTrayMenuCanvasHotkeyText(newHotkey);
        return true;
    } else {
        qDebug() << "Failed to register new canvas hotkey:" << newHotkey << ", reverting...";

        m_canvasHotkey->setShortcut(oldShortcut);
        m_canvasHotkey->setRegistered(true);

        if (m_canvasHotkey->isRegistered()) {
            qDebug() << "Reverted to old canvas hotkey:" << oldShortcut.toString();
        } else {
            qDebug() << "Critical: Failed to restore old canvas hotkey!";
        }

        return false;
    }
}

void MainApplication::updateTrayMenuCanvasHotkeyText(const QString &hotkey)
{
    if (m_screenCanvasAction) {
        m_screenCanvasAction->setText(QString("Screen Canvas (%1)").arg(hotkey));
    }
}
