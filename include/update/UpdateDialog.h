#ifndef UPDATEDIALOG_H
#define UPDATEDIALOG_H

#include "update/UpdateChecker.h"
#include "update/InstallSourceDetector.h"
#include <QDialog>

class QLabel;
class QPushButton;
class QTextBrowser;
class QVBoxLayout;

/**
 * @brief Dialog for displaying update information to the user.
 *
 * This dialog adapts its UI based on the installation source:
 * - DirectDownload: Shows release notes with Download/Remind/Skip options
 * - Store: Shows guidance to update via the store
 *
 * It also supports showing "up to date" and "error" states.
 */
class UpdateDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief Dialog mode for different scenarios.
     */
    enum class Mode {
        UpdateAvailable,  // New version available (DirectDownload)
        StoreGuidance,    // Installed from store, guide user there
        UpToDate,         // No update available
        Error             // Check failed
    };

    /**
     * @brief Create dialog for update available (DirectDownload mode).
     */
    explicit UpdateDialog(const ReleaseInfo& release,
                          InstallSource source,
                          QWidget* parent = nullptr);

    /**
     * @brief Create dialog for store guidance mode.
     */
    explicit UpdateDialog(InstallSource source,
                          QWidget* parent = nullptr);

    /**
     * @brief Create dialog for up-to-date state.
     */
    static UpdateDialog* createUpToDateDialog(QWidget* parent = nullptr);

    /**
     * @brief Create dialog for error state.
     */
    static UpdateDialog* createErrorDialog(const QString& error, QWidget* parent = nullptr);

signals:
    void retryRequested();

private slots:
    void onDownload();
    void onOpenStore();
    void onRemindLater();
    void onSkipVersion();
    void onRetry();

private:
    // Private constructor for factory methods (no UI setup)
    explicit UpdateDialog(Mode mode, QWidget* parent);

    void setupUpdateAvailableUI();
    void setupStoreGuidanceUI();
    void setupUpToDateUI();
    void setupErrorUI(const QString& error);

    void setupCommonStyles();
    QPushButton* createPrimaryButton(const QString& text);
    QPushButton* createSecondaryButton(const QString& text);
    QPushButton* createTertiaryButton(const QString& text);

    Mode m_mode;
    ReleaseInfo m_release;
    InstallSource m_source;

    // UI elements
    QLabel* m_iconLabel;
    QLabel* m_titleLabel;
    QLabel* m_messageLabel;
    QTextBrowser* m_releaseNotes;
    QPushButton* m_primaryButton;
    QPushButton* m_secondaryButton;
    QPushButton* m_tertiaryButton;
};

#endif // UPDATEDIALOG_H
