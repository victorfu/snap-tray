/**
 * @file TypeHotkeyDialog.cpp
 * @brief TypeHotkeyDialog implementation
 */

#include "widgets/TypeHotkeyDialog.h"
#include "settings/SettingsTheme.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>

namespace SnapTray {

TypeHotkeyDialog::TypeHotkeyDialog(QWidget* parent)
    : QDialog(parent, Qt::FramelessWindowHint | Qt::Dialog)
    , m_titleLabel(nullptr)
    , m_instructionLabel(nullptr)
    , m_keyDisplayLabel(nullptr)
    , m_hintLabel(nullptr)
    , m_okBtn(nullptr)
    , m_cancelBtn(nullptr)
    , m_currentModifiers(Qt::NoModifier)
    , m_currentKey(0)
    , m_hasKey(false)
{
    setupUi();

    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);
    setFocusPolicy(Qt::StrongFocus);
}

TypeHotkeyDialog::~TypeHotkeyDialog() = default;

void TypeHotkeyDialog::setupUi()
{
    setFixedSize(kDialogWidth, kDialogHeight);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 20, 24, 20);
    mainLayout->setSpacing(12);

    // Title
    m_titleLabel = new QLabel(this);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setStyleSheet(QStringLiteral(
        "QLabel { font-size: 15px; font-weight: 600; color: %1; }")
        .arg(SettingsTheme::primaryText()));
    mainLayout->addWidget(m_titleLabel);

    // Instruction
    m_instructionLabel = new QLabel(tr("Press a key combination..."), this);
    m_instructionLabel->setAlignment(Qt::AlignCenter);
    m_instructionLabel->setStyleSheet(QStringLiteral(
        "QLabel { font-size: 13px; color: %1; }").arg(SettingsTheme::secondaryText()));
    mainLayout->addWidget(m_instructionLabel);

    mainLayout->addSpacing(8);

    // Key display box
    m_keyDisplayLabel = new QLabel(this);
    m_keyDisplayLabel->setAlignment(Qt::AlignCenter);
    m_keyDisplayLabel->setMinimumHeight(52);
    m_keyDisplayLabel->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  font-family: 'SF Mono', 'Consolas', 'Monaco', monospace;"
        "  font-size: 20px;"
        "  font-weight: 600;"
        "  color: %1;"
        "  background-color: %2;"
        "  border: 2px solid %3;"
        "  border-radius: 8px;"
        "  padding: 10px 20px;"
        "}").arg(SettingsTheme::selectionText(), SettingsTheme::inputFocusBg(), SettingsTheme::inputFocusBorder()));
    mainLayout->addWidget(m_keyDisplayLabel);

    // Hint label
    m_hintLabel = new QLabel(this);
    m_hintLabel->setAlignment(Qt::AlignCenter);
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setStyleSheet(QStringLiteral(
        "QLabel { font-size: 11px; color: %1; }").arg(SettingsTheme::secondaryText()));
    mainLayout->addWidget(m_hintLabel);

    mainLayout->addStretch();

    // Button row
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(12);

    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    m_cancelBtn->setFixedHeight(32);
    m_cancelBtn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background-color: %1;"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 0 20px;"
        "  font-size: 13px;"
        "  color: %2;"
        "}"
        "QPushButton:hover {"
        "  background-color: %3;"
        "}"
        "QPushButton:pressed {"
        "  background-color: %4;"
        "}").arg(SettingsTheme::buttonBg(), SettingsTheme::buttonText(),
                 SettingsTheme::buttonHoverBg(), SettingsTheme::buttonPressedBg()));

    m_okBtn = new QPushButton(tr("OK"), this);
    m_okBtn->setFixedHeight(32);
    m_okBtn->setEnabled(false);  // Disabled until key is captured
    m_okBtn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background-color: %1;"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 0 20px;"
        "  font-size: 13px;"
        "  color: white;"
        "}"
        "QPushButton:hover {"
        "  background-color: %2;"
        "}"
        "QPushButton:pressed {"
        "  background-color: %3;"
        "}"
        "QPushButton:disabled {"
        "  background-color: %4;"
        "  color: %5;"
        "}").arg(SettingsTheme::primaryButtonBg(), SettingsTheme::primaryButtonHoverBg(),
                 SettingsTheme::primaryButtonPressedBg(), SettingsTheme::disabledButtonBg(),
                 SettingsTheme::disabledButtonText()));

    buttonLayout->addStretch();
    buttonLayout->addWidget(m_cancelBtn);
    buttonLayout->addWidget(m_okBtn);
    mainLayout->addLayout(buttonLayout);

    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_okBtn, &QPushButton::clicked, this, &QDialog::accept);

    updateDisplay();
}

void TypeHotkeyDialog::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Dialog background with rounded corners
    QPainterPath path;
    QRectF dialogRect = rect().adjusted(8, 8, -8, -8);
    path.addRoundedRect(dialogRect, 12, 12);

    const bool dark = SettingsTheme::isDark();

    // Shadow effect (simple offset)
    painter.fillPath(path.translated(2, 2), QColor(0, 0, 0, dark ? 60 : 30));

    // Background
    painter.fillPath(path, QColor(SettingsTheme::dialogBackground()));

    // Border
    painter.setPen(QPen(QColor(SettingsTheme::borderColor()), 1));
    painter.drawPath(path);
}

void TypeHotkeyDialog::setInitialKeySequence(const QString& sequence)
{
    m_initialKeySequence = sequence;
    m_keySequence = sequence;

    if (!sequence.isEmpty()) {
        // Parse the sequence to set current state
        QKeySequence keySeq(sequence);
        if (!keySeq.isEmpty()) {
            m_hasKey = true;
        }
    }

    updateDisplay();
}

void TypeHotkeyDialog::setActionName(const QString& name)
{
    m_actionName = name;
    if (m_titleLabel) {
        m_titleLabel->setText(tr("Set Hotkey for: %1").arg(name));
    }
}

void TypeHotkeyDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);

    // Center on parent
    if (parentWidget()) {
        QPoint center = parentWidget()->mapToGlobal(parentWidget()->rect().center());
        move(center.x() - width() / 2, center.y() - height() / 2);
    }

    setFocus();
    activateWindow();
}

bool TypeHotkeyDialog::event(QEvent* e)
{
    // Intercept shortcut override events to prevent global shortcuts
    if (e->type() == QEvent::ShortcutOverride) {
        e->accept();
        return true;
    }
    return QDialog::event(e);
}

void TypeHotkeyDialog::keyPressEvent(QKeyEvent* event)
{
    int key = event->key();
    Qt::KeyboardModifiers modifiers = event->modifiers();

    // Escape always closes dialog immediately
    if (key == Qt::Key_Escape) {
        event->accept();
        m_keySequence = m_initialKeySequence;  // Restore original
        reject();
        return;
    }

    // Enter confirms if we have a key captured
    if ((key == Qt::Key_Return || key == Qt::Key_Enter) && m_hasKey) {
        event->accept();
        accept();
        return;
    }

    // Ignore Enter/Return if no key captured yet
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        event->accept();
        return;
    }

    // Modifier-only press: just update display
    if (isModifierKey(key)) {
        m_currentModifiers = modifiers;
        updateDisplay();
        event->accept();
        return;
    }

    // Regular key press: build the sequence
    m_currentModifiers = modifiers;
    m_currentKey = key;
    m_hasKey = true;

    m_keySequence = modifiersToString(modifiers) + keyToString(key);

    updateDisplay();
    event->accept();
}

void TypeHotkeyDialog::keyReleaseEvent(QKeyEvent* event)
{
    int key = event->key();

    // Update modifier display on release
    if (isModifierKey(key)) {
        m_currentModifiers = QApplication::keyboardModifiers();
        updateDisplay();
    }

    event->accept();
}

void TypeHotkeyDialog::updateDisplay()
{
    QString displayText;

    if (m_hasKey && !m_keySequence.isEmpty()) {
        // Convert stored portable format to native display format
        displayText = QKeySequence(m_keySequence).toString(QKeySequence::NativeText);
        m_keyDisplayLabel->setStyleSheet(QStringLiteral(
            "QLabel {"
            "  font-family: 'SF Mono', 'Consolas', 'Monaco', monospace;"
            "  font-size: 20px;"
            "  font-weight: 600;"
            "  color: %1;"
            "  background-color: %2;"
            "  border: 2px solid %1;"
            "  border-radius: 8px;"
            "  padding: 10px 20px;"
            "}").arg(SettingsTheme::selectionText(), SettingsTheme::inputFocusBg()));
        m_okBtn->setEnabled(true);
    } else if (m_currentModifiers != Qt::NoModifier) {
        // Use display-friendly format for modifier preview
        displayText = modifiersToDisplayString(m_currentModifiers) + QStringLiteral("...");
        m_keyDisplayLabel->setStyleSheet(QStringLiteral(
            "QLabel {"
            "  font-family: 'SF Mono', 'Consolas', 'Monaco', monospace;"
            "  font-size: 20px;"
            "  font-weight: 600;"
            "  color: %1;"
            "  background-color: %2;"
            "  border: 2px dashed %3;"
            "  border-radius: 8px;"
            "  padding: 10px 20px;"
            "}").arg(SettingsTheme::secondaryText(), SettingsTheme::hoverBackground(), SettingsTheme::mutedText()));
        m_okBtn->setEnabled(false);
    } else {
        displayText = tr("(Press keys)");
        m_keyDisplayLabel->setStyleSheet(QStringLiteral(
            "QLabel {"
            "  font-family: 'SF Mono', 'Consolas', 'Monaco', monospace;"
            "  font-size: 20px;"
            "  font-weight: 600;"
            "  color: %1;"
            "  background-color: %2;"
            "  border: 2px solid %3;"
            "  border-radius: 8px;"
            "  padding: 10px 20px;"
            "}").arg(SettingsTheme::mutedText(), SettingsTheme::panelBackground(), SettingsTheme::borderColor()));
        m_okBtn->setEnabled(false);
    }

    m_keyDisplayLabel->setText(displayText);

    // Update hint text
    if (m_hasKey) {
        m_hintLabel->setText(tr("Press Enter or click OK to confirm"));
    } else {
        m_hintLabel->setText(tr("Press a key combination, then press Enter to confirm"));
    }
}

QString TypeHotkeyDialog::modifiersToString(Qt::KeyboardModifiers modifiers) const
{
    // Return Qt portable format for storage/registration
    // This uses standard Qt modifier names that QKeySequence understands
    QString result;

    if (modifiers & Qt::ControlModifier) result += QStringLiteral("Ctrl+");
    if (modifiers & Qt::AltModifier) result += QStringLiteral("Alt+");
    if (modifiers & Qt::ShiftModifier) result += QStringLiteral("Shift+");
    if (modifiers & Qt::MetaModifier) result += QStringLiteral("Meta+");

    return result;
}

QString TypeHotkeyDialog::modifiersToDisplayString(Qt::KeyboardModifiers modifiers) const
{
    // Return user-friendly display format (platform-specific)
    QString result;

#ifdef Q_OS_MAC
    // macOS: Use familiar names
    // Order follows macOS convention: Ctrl, Option, Shift, Cmd
    if (modifiers & Qt::ControlModifier) result += QStringLiteral("Ctrl+");
    if (modifiers & Qt::AltModifier) result += QStringLiteral("Option+");
    if (modifiers & Qt::ShiftModifier) result += QStringLiteral("Shift+");
    if (modifiers & Qt::MetaModifier) result += QStringLiteral("Cmd+");
#else
    // Windows/Linux
    if (modifiers & Qt::ControlModifier) result += QStringLiteral("Ctrl+");
    if (modifiers & Qt::AltModifier) result += QStringLiteral("Alt+");
    if (modifiers & Qt::ShiftModifier) result += QStringLiteral("Shift+");
    if (modifiers & Qt::MetaModifier) result += QStringLiteral("Win+");
#endif

    return result;
}

QString TypeHotkeyDialog::keyToString(int key) const
{
    // Special keys
    switch (key) {
    case Qt::Key_Space:      return QStringLiteral("Space");
    case Qt::Key_Tab:        return QStringLiteral("Tab");
    case Qt::Key_Backspace:  return QStringLiteral("Backspace");
    case Qt::Key_Delete:     return QStringLiteral("Delete");
    case Qt::Key_Insert:     return QStringLiteral("Insert");
    case Qt::Key_Home:       return QStringLiteral("Home");
    case Qt::Key_End:        return QStringLiteral("End");
    case Qt::Key_PageUp:     return QStringLiteral("PageUp");
    case Qt::Key_PageDown:   return QStringLiteral("PageDown");
    case Qt::Key_Left:       return QStringLiteral("Left");
    case Qt::Key_Right:      return QStringLiteral("Right");
    case Qt::Key_Up:         return QStringLiteral("Up");
    case Qt::Key_Down:       return QStringLiteral("Down");
    case Qt::Key_Print:      return QStringLiteral("Print");
    case Qt::Key_Pause:      return QStringLiteral("Pause");
    case Qt::Key_CapsLock:   return QStringLiteral("CapsLock");
    case Qt::Key_NumLock:    return QStringLiteral("NumLock");
    case Qt::Key_ScrollLock: return QStringLiteral("ScrollLock");
    default: break;
    }

    // F1-F24
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        return QStringLiteral("F%1").arg(key - Qt::Key_F1 + 1);
    }

    // Use Qt's key sequence for other keys
    QKeySequence seq(key);
    if (!seq.isEmpty()) {
        return seq.toString(QKeySequence::PortableText);
    }

    return QString();
}

bool TypeHotkeyDialog::isModifierKey(int key) const
{
    return key == Qt::Key_Control ||
           key == Qt::Key_Shift ||
           key == Qt::Key_Alt ||
           key == Qt::Key_Meta ||
           key == Qt::Key_AltGr;
}

}  // namespace SnapTray
