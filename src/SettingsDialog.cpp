#include "SettingsDialog.h"

#include <QKeySequenceEdit>
#include <QSettings>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDebug>
#include <QMessageBox>

static const char* SETTINGS_KEY_HOTKEY = "hotkey";
static const char* DEFAULT_HOTKEY = "F2";
static const char* SETTINGS_KEY_CANVAS_HOTKEY = "canvasHotkey";
static const char* DEFAULT_CANVAS_HOTKEY = "F3";

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , m_hotkeyEdit(nullptr)
    , m_canvasHotkeyEdit(nullptr)
{
    setWindowTitle("SnapTray Settings");
    setFixedSize(350, 160);
    setupUi();
}

SettingsDialog::~SettingsDialog()
{
}

void SettingsDialog::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // Capture hotkey row
    QHBoxLayout *hotkeyLayout = new QHBoxLayout();
    QLabel *hotkeyLabel = new QLabel("Capture Hotkey:", this);
    hotkeyLabel->setFixedWidth(120);
    m_hotkeyEdit = new QKeySequenceEdit(this);
    m_hotkeyEdit->setKeySequence(QKeySequence(loadHotkey()));
    hotkeyLayout->addWidget(hotkeyLabel);
    hotkeyLayout->addWidget(m_hotkeyEdit);
    mainLayout->addLayout(hotkeyLayout);

    // Canvas hotkey row
    QHBoxLayout *canvasHotkeyLayout = new QHBoxLayout();
    QLabel *canvasHotkeyLabel = new QLabel("Canvas Hotkey:", this);
    canvasHotkeyLabel->setFixedWidth(120);
    m_canvasHotkeyEdit = new QKeySequenceEdit(this);
    m_canvasHotkeyEdit->setKeySequence(QKeySequence(loadCanvasHotkey()));
    canvasHotkeyLayout->addWidget(canvasHotkeyLabel);
    canvasHotkeyLayout->addWidget(m_canvasHotkeyEdit);
    mainLayout->addLayout(canvasHotkeyLayout);

    mainLayout->addStretch();

    // Buttons row
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

    // 驗證不為空
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

    // 驗證兩個熱鍵不相同
    if (newHotkey == newCanvasHotkey) {
        QMessageBox::warning(this, "Hotkey Conflict",
            "Capture hotkey and Canvas hotkey cannot be the same.");
        return;
    }

    saveHotkey(newHotkey);
    saveCanvasHotkey(newCanvasHotkey);
    emit hotkeyChangeRequested(newHotkey);
    emit canvasHotkeyChangeRequested(newCanvasHotkey);
    // 注意：不直接呼叫 accept()，由 MainApplication 根據註冊結果決定是否關閉
}

void SettingsDialog::showHotkeyError(const QString &message)
{
    QMessageBox::warning(this, "Hotkey Registration Failed", message);
}

void SettingsDialog::onCancel()
{
    reject();
}
