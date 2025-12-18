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

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , m_hotkeyEdit(nullptr)
{
    setWindowTitle("SnapTray Settings");
    setFixedSize(350, 120);
    setupUi();
}

SettingsDialog::~SettingsDialog()
{
}

void SettingsDialog::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // Hotkey row
    QHBoxLayout *hotkeyLayout = new QHBoxLayout();
    QLabel *hotkeyLabel = new QLabel("Capture Hotkey:", this);
    m_hotkeyEdit = new QKeySequenceEdit(this);
    m_hotkeyEdit->setKeySequence(QKeySequence(loadHotkey()));
    hotkeyLayout->addWidget(hotkeyLabel);
    hotkeyLayout->addWidget(m_hotkeyEdit);
    mainLayout->addLayout(hotkeyLayout);

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

void SettingsDialog::onSave()
{
    QString newHotkey = m_hotkeyEdit->keySequence().toString();

    // 驗證不為空
    if (newHotkey.isEmpty()) {
        QMessageBox::warning(this, "Invalid Hotkey",
            "Hotkey cannot be empty. Please set a valid key combination.");
        return;
    }

    saveHotkey(newHotkey);
    emit hotkeyChangeRequested(newHotkey);
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
