#ifndef GLOBALTOAST_H
#define GLOBALTOAST_H

#include <QWidget>

class QLabel;
class QTimer;

/**
 * @brief A global floating toast notification that doesn't require an active window.
 *
 * This widget displays notifications in the top-right corner of the screen.
 * It doesn't steal focus and works even when no application window is active.
 */
class GlobalToast : public QWidget
{
    Q_OBJECT

public:
    enum Type {
        Success,
        Error,
        Info
    };

    /**
     * @brief Get the singleton instance
     */
    static GlobalToast& instance();

    /**
     * @brief Show a toast notification
     * @param type The type of notification (Success, Error, Info)
     * @param title The title text
     * @param message The message text
     * @param durationMs How long to show the toast (default 3000ms)
     */
    void showToast(Type type, const QString& title, const QString& message, int durationMs = 3000);

private:
    GlobalToast();
    ~GlobalToast() override = default;

    GlobalToast(const GlobalToast&) = delete;
    GlobalToast& operator=(const GlobalToast&) = delete;

    void setupUi();
    void positionOnScreen();
    void updateStyle(Type type);

    QLabel* m_titleLabel;
    QLabel* m_messageLabel;
    QTimer* m_hideTimer;
    QWidget* m_contentWidget;
};

#endif // GLOBALTOAST_H
