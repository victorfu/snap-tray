/**
 * @file TypeHotkeyDialog.h
 * @brief Modal dialog for capturing hotkey input
 *
 * Features:
 * - Real-time key capture as user presses keys
 * - OK/Cancel buttons for confirmation
 * - Visual feedback with modifier key display
 */

#pragma once

#include <QDialog>

class QLabel;
class QPushButton;

namespace SnapTray {

/**
 * @brief Modal dialog for capturing hotkey input.
 *
 * Usage:
 * @code
 * TypeHotkeyDialog dialog(this);
 * dialog.setInitialKeySequence("Ctrl+F2");
 * if (dialog.exec() == QDialog::Accepted) {
 *     QString newKey = dialog.keySequence();
 *     // Apply the new hotkey...
 * }
 * @endcode
 */
class TypeHotkeyDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TypeHotkeyDialog(QWidget* parent = nullptr);
    ~TypeHotkeyDialog() override;

    /**
     * @brief Get the captured key sequence.
     */
    QString keySequence() const { return m_keySequence; }

    /**
     * @brief Set initial key sequence to display.
     */
    void setInitialKeySequence(const QString& sequence);

    /**
     * @brief Set the action name for display in the dialog.
     */
    void setActionName(const QString& name);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    bool event(QEvent* e) override;

private:
    void setupUi();
    void updateDisplay();

    QString modifiersToString(Qt::KeyboardModifiers modifiers) const;
    QString modifiersToDisplayString(Qt::KeyboardModifiers modifiers) const;
    QString keyToString(int key) const;
    bool isModifierKey(int key) const;

    // UI elements
    QLabel* m_titleLabel;
    QLabel* m_instructionLabel;
    QLabel* m_keyDisplayLabel;
    QLabel* m_hintLabel;
    QPushButton* m_okBtn;
    QPushButton* m_cancelBtn;

    // State
    QString m_keySequence;
    QString m_initialKeySequence;
    QString m_actionName;
    Qt::KeyboardModifiers m_currentModifiers;
    int m_currentKey;
    bool m_hasKey;

    // Constants
    static constexpr int kDialogWidth = 400;
    static constexpr int kDialogHeight = 240;
};

}  // namespace SnapTray
