#include "widgets/HotkeyEdit.h"
#include "settings/SettingsTheme.h"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QMenu>
#include <QKeyEvent>
#include <QInputDialog>
#include <QMessageBox>
#include <QPalette>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// Special key definitions (Windows only)
struct SpecialKey {
    QString displayName;
    QString qtKeyString;
    Qt::KeyboardModifiers modifiers;
};

#ifdef Q_OS_WIN
static const QList<SpecialKey> kSpecialKeys = {
    {"Print Screen", "Print", Qt::NoModifier},
    {"Ctrl+Print Screen", "Print", Qt::ControlModifier},
    {"Alt+Print Screen", "Print", Qt::AltModifier},
    {"Shift+Print Screen", "Print", Qt::ShiftModifier},
};
#endif

HotkeyEdit::HotkeyEdit(QWidget *parent)
    : QWidget(parent)
    , m_lineEdit(nullptr)
    , m_specialKeysBtn(nullptr)
    , m_specialKeysMenu(nullptr)
    , m_nativeKeyCode(0)
    , m_isRecording(false)
{
    setupUi();
}

HotkeyEdit::~HotkeyEdit() = default;

void HotkeyEdit::setupUi()
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    // Line edit for displaying/recording hotkey
    m_lineEdit = new QLineEdit(this);
    m_lineEdit->setReadOnly(true);
    m_lineEdit->setPlaceholderText("Click to record hotkey...");
    m_lineEdit->setFocusPolicy(Qt::StrongFocus);
    m_lineEdit->installEventFilter(this);
    layout->addWidget(m_lineEdit, 1);

#ifdef Q_OS_WIN
    // Special keys button (Windows only)
    m_specialKeysBtn = new QPushButton("...", this);
    m_specialKeysBtn->setFixedWidth(30);
    m_specialKeysBtn->setToolTip("Special Keys (Print Screen, Custom Key Code)");
    layout->addWidget(m_specialKeysBtn);

    createSpecialKeysMenu();
    connect(m_specialKeysBtn, &QPushButton::clicked, this, [this]() {
        m_specialKeysMenu->exec(m_specialKeysBtn->mapToGlobal(
            QPoint(0, m_specialKeysBtn->height())));
    });
#endif

    setFocusProxy(m_lineEdit);
}

void HotkeyEdit::createSpecialKeysMenu()
{
#ifdef Q_OS_WIN
    m_specialKeysMenu = new QMenu(this);

    // Add Print Screen variants
    for (const auto &key : kSpecialKeys) {
        QAction *action = m_specialKeysMenu->addAction(key.displayName);
        action->setData(QVariant::fromValue(key.qtKeyString));
        action->setProperty("modifiers", static_cast<int>(key.modifiers));
    }

    m_specialKeysMenu->addSeparator();

    // Add custom key code option
    QAction *customAction = m_specialKeysMenu->addAction("Custom Key Code...");
    connect(customAction, &QAction::triggered, this, &HotkeyEdit::onCustomKeyCodeTriggered);

    // Connect menu actions (except custom which has its own connection)
    connect(m_specialKeysMenu, &QMenu::triggered, this, &HotkeyEdit::onSpecialKeySelected);
#endif
}

void HotkeyEdit::onSpecialKeySelected(QAction *action)
{
    // Skip the custom key code action
    if (action->text() == "Custom Key Code...") {
        return;
    }

    QString keyString = action->data().toString();
    Qt::KeyboardModifiers modifiers = static_cast<Qt::KeyboardModifiers>(
        action->property("modifiers").toInt());

    // Build the full key sequence string
    QString fullSequence;
    if (modifiers & Qt::ControlModifier) fullSequence += "Ctrl+";
    if (modifiers & Qt::AltModifier) fullSequence += "Alt+";
    if (modifiers & Qt::ShiftModifier) fullSequence += "Shift+";
    if (modifiers & Qt::MetaModifier) fullSequence += "Meta+";
    fullSequence += keyString;

    m_keySequence = fullSequence;
    m_nativeKeyCode = 0;
    updateDisplay();
    emit keySequenceChanged(m_keySequence);
}

void HotkeyEdit::onCustomKeyCodeTriggered()
{
#ifdef Q_OS_WIN
    bool ok;
    QString text = QInputDialog::getText(this, "Custom Key Code",
        "Enter the native key code (decimal or hex with 0x prefix).\n\n"
        "Use KeyboardStateView (nirsoft.net) to find your key's code.\n"
        "Example: 44 or 0x2C for Print Screen",
        QLineEdit::Normal, "", &ok);

    if (ok && !text.isEmpty()) {
        bool parseOk;
        quint32 keyCode;

        if (text.startsWith("0x", Qt::CaseInsensitive)) {
            keyCode = text.mid(2).toUInt(&parseOk, 16);
        } else {
            keyCode = text.toUInt(&parseOk, 10);
        }

        if (parseOk && keyCode > 0 && keyCode < 256) {
            m_nativeKeyCode = keyCode;
            m_keySequence = QString("Native:0x%1").arg(keyCode, 2, 16, QChar('0')).toUpper();
            updateDisplay();
            emit keySequenceChanged(m_keySequence);
        } else {
            QMessageBox::warning(this, "Invalid Key Code",
                "Please enter a valid key code between 1 and 255.");
        }
    }
#endif
}

QString HotkeyEdit::keySequence() const
{
    return m_keySequence;
}

void HotkeyEdit::setKeySequence(const QString &sequence)
{
    m_keySequence = sequence;

    // Check if it's a native keycode
    if (sequence.startsWith("Native:0x", Qt::CaseInsensitive)) {
        bool ok;
        m_nativeKeyCode = sequence.mid(9).toUInt(&ok, 16);
        if (!ok) m_nativeKeyCode = 0;
    } else {
        m_nativeKeyCode = 0;
    }

    updateDisplay();
}

bool HotkeyEdit::isNativeKeyCode() const
{
    return m_nativeKeyCode != 0;
}

quint32 HotkeyEdit::nativeKeyCode() const
{
    return m_nativeKeyCode;
}

void HotkeyEdit::updateDisplay()
{
    if (m_nativeKeyCode != 0) {
        m_lineEdit->setText(QString("Custom Key (0x%1)")
            .arg(m_nativeKeyCode, 2, 16, QChar('0')).toUpper());
    } else if (!m_keySequence.isEmpty()) {
        // Use QKeySequence for nice display formatting
        QKeySequence seq(m_keySequence);
        if (!seq.isEmpty()) {
            m_lineEdit->setText(seq.toString(QKeySequence::NativeText));
        } else {
            // Fallback for special keys that QKeySequence might not format nicely
            m_lineEdit->setText(m_keySequence);
        }
    } else {
        m_lineEdit->clear();
    }
}

bool HotkeyEdit::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_lineEdit) {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
            keyPressEvent(keyEvent);
            return true;  // Intercept the event, don't let QLineEdit handle it
        }
        if (event->type() == QEvent::FocusIn) {
            QFocusEvent *focusEvent = static_cast<QFocusEvent*>(event);
            focusInEvent(focusEvent);
        }
        if (event->type() == QEvent::FocusOut) {
            QFocusEvent *focusEvent = static_cast<QFocusEvent*>(event);
            focusOutEvent(focusEvent);
        }
    }
    return QWidget::eventFilter(watched, event);
}

void HotkeyEdit::keyPressEvent(QKeyEvent *event)
{
    if (!m_isRecording) {
        QWidget::keyPressEvent(event);
        return;
    }

    int key = event->key();

    // Ignore modifier-only keys
    if (key == Qt::Key_Control || key == Qt::Key_Shift ||
        key == Qt::Key_Alt || key == Qt::Key_Meta ||
        key == Qt::Key_unknown) {
        return;
    }

    // Handle Escape to clear
    if (key == Qt::Key_Escape) {
        m_keySequence.clear();
        m_nativeKeyCode = 0;
        updateDisplay();
        emit keySequenceChanged(m_keySequence);
        return;
    }

    // Build key sequence
    Qt::KeyboardModifiers modifiers = event->modifiers();
    QString sequence = modifiersToString(modifiers);

    // Get the key name
    QKeySequence keySeq(key);
    if (!keySeq.isEmpty()) {
        sequence += keySeq.toString();
    }

    m_keySequence = sequence;
    m_nativeKeyCode = 0;
    updateDisplay();
    emit keySequenceChanged(m_keySequence);
}

void HotkeyEdit::focusInEvent(QFocusEvent *event)
{
    QWidget::focusInEvent(event);
    m_isRecording = true;

    // Use QPalette for reliable background color on macOS (stylesheets can be overridden by native styling)
    QPalette pal = m_lineEdit->palette();
    pal.setColor(QPalette::Base, QColor(SnapTray::SettingsTheme::focusHighlight()));
    m_lineEdit->setAutoFillBackground(true);
    m_lineEdit->setPalette(pal);

    m_lineEdit->setPlaceholderText("Press a key combination...");
}

void HotkeyEdit::focusOutEvent(QFocusEvent *event)
{
    QWidget::focusOutEvent(event);
    m_isRecording = false;

    // Restore default palette
    m_lineEdit->setPalette(QPalette());
    m_lineEdit->setAutoFillBackground(false);

    m_lineEdit->setPlaceholderText("Click to record hotkey...");
}

QString HotkeyEdit::modifiersToString(Qt::KeyboardModifiers modifiers) const
{
    QString result;
    if (modifiers & Qt::ControlModifier) result += "Ctrl+";
    if (modifiers & Qt::AltModifier) result += "Alt+";
    if (modifiers & Qt::ShiftModifier) result += "Shift+";
    if (modifiers & Qt::MetaModifier) result += "Meta+";
    return result;
}
