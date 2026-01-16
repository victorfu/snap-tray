#ifndef HOTKEYEDIT_H
#define HOTKEYEDIT_H

#include <QWidget>
#include <QKeySequence>

class QLineEdit;
class QPushButton;
class QMenu;

/**
 * @brief Custom hotkey input widget with special key support
 *
 * On Windows: Provides a dropdown menu for special keys (Print Screen, custom keycodes)
 * that cannot be captured via standard Qt keyboard events.
 *
 * On macOS: Standard hotkey input only (no Print Screen key exists).
 */
class HotkeyEdit : public QWidget
{
    Q_OBJECT

public:
    explicit HotkeyEdit(QWidget *parent = nullptr);
    ~HotkeyEdit() override;

    /**
     * @brief Get the current key sequence as a string
     * @return Key sequence string (e.g., "F2", "Print", "Ctrl+Print")
     */
    QString keySequence() const;

    /**
     * @brief Set the key sequence from a string
     * @param sequence Key sequence string
     */
    void setKeySequence(const QString &sequence);

    /**
     * @brief Check if using a native keycode (for SR/gaming keys)
     * @return true if using native keycode
     */
    bool isNativeKeyCode() const;

    /**
     * @brief Get the native keycode (if using one)
     * @return Native keycode, or 0 if using standard key sequence
     */
    quint32 nativeKeyCode() const;

signals:
    /**
     * @brief Emitted when the key sequence changes
     * @param sequence New key sequence string
     */
    void keySequenceChanged(const QString &sequence);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private slots:
    void onSpecialKeySelected(QAction *action);
    void onCustomKeyCodeTriggered();

private:
    void setupUi();
    void createSpecialKeysMenu();
    void updateDisplay();
    QString modifiersToString(Qt::KeyboardModifiers modifiers) const;

    QLineEdit *m_lineEdit;
    QPushButton *m_specialKeysBtn;
    QMenu *m_specialKeysMenu;

    QString m_keySequence;
    quint32 m_nativeKeyCode;
    bool m_isRecording;
};

#endif // HOTKEYEDIT_H
