#include "SettingsDialog.h"
#include "AutoLaunchManager.h"

#include <QKeySequenceEdit>
#include <QSettings>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDebug>
#include <QMessageBox>
#include <QTabWidget>
#include <QCheckBox>

static const char* SETTINGS_KEY_HOTKEY = "hotkey";
static const char* DEFAULT_HOTKEY = "F2";
static const char* SETTINGS_KEY_CANVAS_HOTKEY = "canvasHotkey";
static const char* DEFAULT_CANVAS_HOTKEY = "Shift+F2";

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , m_tabWidget(nullptr)
    , m_startOnLoginCheckbox(nullptr)
    , m_hotkeyEdit(nullptr)
    , m_canvasHotkeyEdit(nullptr)
    , m_captureHotkeyStatus(nullptr)
    , m_canvasHotkeyStatus(nullptr)
    , m_restoreDefaultsBtn(nullptr)
{
    setWindowTitle("SnapTray Settings");
    setFixedSize(420, 240);
    setupUi();
}

SettingsDialog::~SettingsDialog()
{
}

void SettingsDialog::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // Create tab widget
    m_tabWidget = new QTabWidget(this);

    // Tab 1: General
    QWidget *generalTab = new QWidget();
    setupGeneralTab(generalTab);
    m_tabWidget->addTab(generalTab, "General");

    // Tab 2: Hotkeys
    QWidget *hotkeysTab = new QWidget();
    setupHotkeysTab(hotkeysTab);
    m_tabWidget->addTab(hotkeysTab, "Hotkeys");

    mainLayout->addWidget(m_tabWidget);

    // Buttons row (outside tabs)
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    QPushButton *saveButton = new QPushButton("Save", this);
    QPushButton *cancelButton = new QPushButton("Cancel", this);

    connect(saveButton, &QPushButton::clicked, this, &SettingsDialog::onSave);
    connect(cancelButton, &QPushButton::clicked, this, &SettingsDialog::onCancel);

    buttonLayout->addWidget(saveButton);
    buttonLayout->addWidget(cancelButton);
    mainLayout->addLayout(buttonLayout);
}

void SettingsDialog::setupGeneralTab(QWidget *tab)
{
    QVBoxLayout *layout = new QVBoxLayout(tab);

    m_startOnLoginCheckbox = new QCheckBox("Start on login", tab);
    m_startOnLoginCheckbox->setChecked(AutoLaunchManager::isEnabled());
    layout->addWidget(m_startOnLoginCheckbox);

    layout->addStretch();
}

void SettingsDialog::setupHotkeysTab(QWidget *tab)
{
    QVBoxLayout *layout = new QVBoxLayout(tab);

    // Region Capture hotkey row
    QHBoxLayout *captureLayout = new QHBoxLayout();
    QLabel *captureLabel = new QLabel("Region Capture:", tab);
    captureLabel->setFixedWidth(120);
    m_hotkeyEdit = new QKeySequenceEdit(tab);
    m_hotkeyEdit->setKeySequence(QKeySequence(loadHotkey()));
    m_captureHotkeyStatus = new QLabel(tab);
    m_captureHotkeyStatus->setFixedSize(24, 24);
    m_captureHotkeyStatus->setAlignment(Qt::AlignCenter);
    captureLayout->addWidget(captureLabel);
    captureLayout->addWidget(m_hotkeyEdit);
    captureLayout->addWidget(m_captureHotkeyStatus);
    layout->addLayout(captureLayout);

    // Screen Canvas hotkey row
    QHBoxLayout *canvasLayout = new QHBoxLayout();
    QLabel *canvasLabel = new QLabel("Screen Canvas:", tab);
    canvasLabel->setFixedWidth(120);
    m_canvasHotkeyEdit = new QKeySequenceEdit(tab);
    m_canvasHotkeyEdit->setKeySequence(QKeySequence(loadCanvasHotkey()));
    m_canvasHotkeyStatus = new QLabel(tab);
    m_canvasHotkeyStatus->setFixedSize(24, 24);
    m_canvasHotkeyStatus->setAlignment(Qt::AlignCenter);
    canvasLayout->addWidget(canvasLabel);
    canvasLayout->addWidget(m_canvasHotkeyEdit);
    canvasLayout->addWidget(m_canvasHotkeyStatus);
    layout->addLayout(canvasLayout);

    layout->addStretch();

    // Restore Defaults button
    QHBoxLayout *defaultsLayout = new QHBoxLayout();
    defaultsLayout->addStretch();
    m_restoreDefaultsBtn = new QPushButton("Restore Defaults", tab);
    connect(m_restoreDefaultsBtn, &QPushButton::clicked,
            this, &SettingsDialog::onRestoreDefaults);
    defaultsLayout->addWidget(m_restoreDefaultsBtn);
    layout->addLayout(defaultsLayout);
}

void SettingsDialog::updateHotkeyStatus(QLabel *statusLabel, bool isRegistered)
{
    if (isRegistered) {
        statusLabel->setText("✓");
        statusLabel->setStyleSheet("color: green; font-weight: bold; font-size: 16px;");
    } else {
        statusLabel->setText("✗");
        statusLabel->setStyleSheet("color: red; font-weight: bold; font-size: 16px;");
    }
}

void SettingsDialog::updateCaptureHotkeyStatus(bool isRegistered)
{
    updateHotkeyStatus(m_captureHotkeyStatus, isRegistered);
}

void SettingsDialog::updateCanvasHotkeyStatus(bool isRegistered)
{
    updateHotkeyStatus(m_canvasHotkeyStatus, isRegistered);
}

void SettingsDialog::onRestoreDefaults()
{
    m_hotkeyEdit->setKeySequence(QKeySequence(DEFAULT_HOTKEY));
    m_canvasHotkeyEdit->setKeySequence(QKeySequence(DEFAULT_CANVAS_HOTKEY));
}

QString SettingsDialog::defaultHotkey()
{
    return QString(DEFAULT_HOTKEY);
}

QString SettingsDialog::loadHotkey()
{
    QSettings settings("MySoft", "SnapTray");
    return settings.value(SETTINGS_KEY_HOTKEY, DEFAULT_HOTKEY).toString();
}

void SettingsDialog::saveHotkey(const QString &keySequence)
{
    QSettings settings("MySoft", "SnapTray");
    settings.setValue(SETTINGS_KEY_HOTKEY, keySequence);
    qDebug() << "Hotkey saved:" << keySequence;
}

QString SettingsDialog::defaultCanvasHotkey()
{
    return QString(DEFAULT_CANVAS_HOTKEY);
}

QString SettingsDialog::loadCanvasHotkey()
{
    QSettings settings("MySoft", "SnapTray");
    return settings.value(SETTINGS_KEY_CANVAS_HOTKEY, DEFAULT_CANVAS_HOTKEY).toString();
}

void SettingsDialog::saveCanvasHotkey(const QString &keySequence)
{
    QSettings settings("MySoft", "SnapTray");
    settings.setValue(SETTINGS_KEY_CANVAS_HOTKEY, keySequence);
    qDebug() << "Canvas hotkey saved:" << keySequence;
}

void SettingsDialog::onSave()
{
    QString newHotkey = m_hotkeyEdit->keySequence().toString();
    QString newCanvasHotkey = m_canvasHotkeyEdit->keySequence().toString();

    // Validate hotkeys are not empty
    if (newHotkey.isEmpty()) {
        QMessageBox::warning(this, "Invalid Hotkey",
            "Capture hotkey cannot be empty. Please set a valid key combination.");
        return;
    }

    if (newCanvasHotkey.isEmpty()) {
        QMessageBox::warning(this, "Invalid Hotkey",
            "Canvas hotkey cannot be empty. Please set a valid key combination.");
        return;
    }

    // Validate hotkeys are different
    if (newHotkey == newCanvasHotkey) {
        QMessageBox::warning(this, "Hotkey Conflict",
            "Capture hotkey and Canvas hotkey cannot be the same.");
        return;
    }

    // Request hotkey changes (saving happens only after successful registration)
    emit hotkeyChangeRequested(newHotkey);
    emit canvasHotkeyChangeRequested(newCanvasHotkey);

    // Handle start on login
    bool startOnLogin = m_startOnLoginCheckbox->isChecked();
    bool currentState = AutoLaunchManager::isEnabled();
    if (startOnLogin != currentState) {
        bool success = AutoLaunchManager::setEnabled(startOnLogin);
        if (!success) {
            QMessageBox::warning(this, "Auto Launch Error",
                "Failed to update auto-launch setting.");
        }
        emit startOnLoginChanged(startOnLogin);
    }
    // Note: dialog close is handled by MainApplication based on hotkey registration result
}

void SettingsDialog::showHotkeyError(const QString &message)
{
    QMessageBox::warning(this, "Hotkey Registration Failed", message);
}

void SettingsDialog::onCancel()
{
    reject();
}
