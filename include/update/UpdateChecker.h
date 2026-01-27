#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

#include "update/InstallSourceDetector.h"
#include <QObject>
#include <QDateTime>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

/**
 * @brief Information about a GitHub release.
 */
struct ReleaseInfo {
    QString version;           // "1.2.0"
    QString tagName;           // "v1.2.0"
    QString releaseNotes;      // Markdown format
    QString downloadUrlWin;    // Windows download link (.exe)
    QString downloadUrlMac;    // macOS download link (.dmg)
    QString htmlUrl;           // GitHub Release page URL
    QDateTime publishedAt;
    bool isPrerelease = false;

    bool isValid() const { return !version.isEmpty(); }
};

/**
 * @brief Class responsible for checking for application updates.
 *
 * This class handles:
 * - Checking GitHub releases API for new versions
 * - Periodic update checking based on user settings
 * - Version comparison
 * - Respecting install source (don't check for store installs)
 */
class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    explicit UpdateChecker(QObject* parent = nullptr);
    ~UpdateChecker() override;

    /**
     * @brief Check for updates.
     * @param silent If true, don't emit signals for "no update" or errors (used for auto-check).
     */
    void checkForUpdates(bool silent = true);

    /**
     * @brief Start periodic update checking based on user settings.
     */
    void startPeriodicCheck();

    /**
     * @brief Stop periodic update checking.
     */
    void stopPeriodicCheck();

    /**
     * @brief Get the installation source detected for this application.
     */
    InstallSource installSource() const;

    /**
     * @brief Check if an update check is currently in progress.
     */
    bool isChecking() const;

    /**
     * @brief Get the current application version.
     */
    static QString currentVersion();

    /**
     * @brief Compare two version strings.
     * @param remote The remote version string (e.g., "1.2.0")
     * @param local The local version string (e.g., "1.1.0")
     * @return true if remote > local
     */
    static bool isNewerVersion(const QString& remote, const QString& local);

    // GitHub repository configuration
    static constexpr const char* kGitHubOwner = "victorfu";
    static constexpr const char* kGitHubRepo = "snap-tray";

signals:
    /**
     * @brief Emitted when an update check starts.
     */
    void checkStarted();

    /**
     * @brief Emitted when a new update is available.
     * @param release Information about the available release.
     */
    void updateAvailable(const ReleaseInfo& release);

    /**
     * @brief Emitted when no update is available (only for non-silent checks).
     */
    void noUpdateAvailable();

    /**
     * @brief Emitted when an update check fails (only for non-silent checks).
     * @param error Description of the error.
     */
    void checkFailed(const QString& error);

private slots:
    void onNetworkReply(QNetworkReply* reply);
    void onPeriodicCheck();

private:
    void parseReleaseResponse(const QByteArray& data);
    ReleaseInfo parseReleaseJson(const QJsonObject& json);
    QString extractVersionFromTag(const QString& tagName);

    QNetworkAccessManager* m_networkManager;
    QTimer* m_periodicTimer;
    InstallSource m_installSource;
    bool m_isChecking;
    bool m_silentCheck;
};

#endif // UPDATECHECKER_H
