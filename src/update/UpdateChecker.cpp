#include "update/UpdateChecker.h"
#include "update/UpdateSettingsManager.h"
#include "version.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QVersionNumber>
#include <QDebug>

namespace {
// Delay before first automatic check (10 seconds after app start)
constexpr int kInitialCheckDelayMs = 10000;

// Interval for periodic check timer (1 hour)
constexpr int kPeriodicCheckIntervalMs = 3600000;
}

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_periodicTimer(new QTimer(this))
    , m_installSource(InstallSourceDetector::detect())
    , m_isChecking(false)
    , m_silentCheck(true)
{
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &UpdateChecker::onNetworkReply);
    connect(m_periodicTimer, &QTimer::timeout,
            this, &UpdateChecker::onPeriodicCheck);
}

UpdateChecker::~UpdateChecker()
{
    stopPeriodicCheck();
}

void UpdateChecker::checkForUpdates(bool silent)
{
    if (m_isChecking) {
        qDebug() << "UpdateChecker: Check already in progress";
        return;
    }

    // Don't check for store installs or development builds in silent mode
    if (silent) {
        if (m_installSource == InstallSource::MicrosoftStore ||
            m_installSource == InstallSource::MacAppStore ||
            m_installSource == InstallSource::Development) {
            qDebug() << "UpdateChecker: Skipping check for"
                     << InstallSourceDetector::getSourceDisplayName(m_installSource);
            return;
        }
    }

    m_isChecking = true;
    m_silentCheck = silent;
    emit checkStarted();

    // Build GitHub API URL
    QString apiUrl = QString("https://api.github.com/repos/%1/%2/releases/latest")
                         .arg(kGitHubOwner, kGitHubRepo);

    QNetworkRequest request(apiUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QString("SnapTray/%1").arg(currentVersion()));
    request.setRawHeader("Accept", "application/vnd.github.v3+json");

    qDebug() << "UpdateChecker: Checking for updates at" << apiUrl;
    m_networkManager->get(request);
}

void UpdateChecker::startPeriodicCheck()
{
    // Don't start periodic checks for store installs or development builds
    if (m_installSource == InstallSource::MicrosoftStore ||
        m_installSource == InstallSource::MacAppStore ||
        m_installSource == InstallSource::Development) {
        qDebug() << "UpdateChecker: Periodic check disabled for"
                 << InstallSourceDetector::getSourceDisplayName(m_installSource);
        return;
    }

    if (!UpdateSettingsManager::instance().isAutoCheckEnabled()) {
        qDebug() << "UpdateChecker: Auto-check is disabled in settings";
        return;
    }

    // Schedule initial check after a delay
    QTimer::singleShot(kInitialCheckDelayMs, this, &UpdateChecker::onPeriodicCheck);

    // Start periodic timer
    m_periodicTimer->start(kPeriodicCheckIntervalMs);
    qDebug() << "UpdateChecker: Periodic check started";
}

void UpdateChecker::stopPeriodicCheck()
{
    m_periodicTimer->stop();
    qDebug() << "UpdateChecker: Periodic check stopped";
}

InstallSource UpdateChecker::installSource() const
{
    return m_installSource;
}

bool UpdateChecker::isChecking() const
{
    return m_isChecking;
}

QString UpdateChecker::currentVersion()
{
    return QStringLiteral(SNAPTRAY_VERSION);
}

bool UpdateChecker::isNewerVersion(const QString& remote, const QString& local)
{
    QVersionNumber remoteVersion = QVersionNumber::fromString(remote);
    QVersionNumber localVersion = QVersionNumber::fromString(local);

    if (remoteVersion.isNull() || localVersion.isNull()) {
        qWarning() << "UpdateChecker: Invalid version format - remote:" << remote
                   << "local:" << local;
        return false;
    }

    return remoteVersion > localVersion;
}

void UpdateChecker::onNetworkReply(QNetworkReply* reply)
{
    m_isChecking = false;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        QString errorString = reply->errorString();
        qWarning() << "UpdateChecker: Network error:" << errorString;
        if (!m_silentCheck) {
            emit checkFailed(errorString);
        }
        return;
    }

    QByteArray data = reply->readAll();
    parseReleaseResponse(data);

    // Update last check time
    UpdateSettingsManager::instance().setLastCheckTime(QDateTime::currentDateTime());
}

void UpdateChecker::onPeriodicCheck()
{
    auto& settings = UpdateSettingsManager::instance();

    if (!settings.isAutoCheckEnabled()) {
        return;
    }

    if (!settings.shouldCheckForUpdates()) {
        qDebug() << "UpdateChecker: Not time to check yet";
        return;
    }

    checkForUpdates(true);  // Silent check
}

void UpdateChecker::parseReleaseResponse(const QByteArray& data)
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "UpdateChecker: JSON parse error:" << parseError.errorString();
        if (!m_silentCheck) {
            emit checkFailed(tr("Failed to parse update information"));
        }
        return;
    }

    if (!doc.isObject()) {
        qWarning() << "UpdateChecker: Unexpected JSON format";
        if (!m_silentCheck) {
            emit checkFailed(tr("Invalid update response"));
        }
        return;
    }

    ReleaseInfo release = parseReleaseJson(doc.object());

    if (!release.isValid()) {
        qWarning() << "UpdateChecker: Could not extract release info";
        if (!m_silentCheck) {
            emit checkFailed(tr("Could not parse release information"));
        }
        return;
    }

    // Check if prerelease and user doesn't want prereleases
    if (release.isPrerelease && !UpdateSettingsManager::instance().includePrerelease()) {
        qDebug() << "UpdateChecker: Skipping prerelease" << release.version;
        if (!m_silentCheck) {
            emit noUpdateAvailable();
        }
        return;
    }

    // Check if this version is skipped
    QString skippedVersion = UpdateSettingsManager::instance().skippedVersion();
    if (!skippedVersion.isEmpty() && release.version == skippedVersion) {
        qDebug() << "UpdateChecker: Version" << release.version << "is skipped";
        if (!m_silentCheck) {
            emit noUpdateAvailable();
        }
        return;
    }

    // Compare versions
    QString localVersion = currentVersion();
    if (isNewerVersion(release.version, localVersion)) {
        qDebug() << "UpdateChecker: New version available:" << release.version
                 << "(current:" << localVersion << ")";
        emit updateAvailable(release);
    } else {
        qDebug() << "UpdateChecker: No update available (remote:" << release.version
                 << "local:" << localVersion << ")";
        if (!m_silentCheck) {
            emit noUpdateAvailable();
        }
    }
}

ReleaseInfo UpdateChecker::parseReleaseJson(const QJsonObject& json)
{
    ReleaseInfo release;

    release.tagName = json["tag_name"].toString();
    release.version = extractVersionFromTag(release.tagName);
    release.releaseNotes = json["body"].toString();
    release.htmlUrl = json["html_url"].toString();
    release.isPrerelease = json["prerelease"].toBool();

    QString publishedAtStr = json["published_at"].toString();
    release.publishedAt = QDateTime::fromString(publishedAtStr, Qt::ISODate);

    // Parse assets to find download URLs
    QJsonArray assets = json["assets"].toArray();
    for (const QJsonValue& assetVal : assets) {
        QJsonObject asset = assetVal.toObject();
        QString name = asset["name"].toString().toLower();
        QString downloadUrl = asset["browser_download_url"].toString();

        if (name.endsWith(".exe") || name.contains("windows")) {
            release.downloadUrlWin = downloadUrl;
        } else if (name.endsWith(".dmg") || name.contains("macos") || name.contains("mac")) {
            release.downloadUrlMac = downloadUrl;
        }
    }

    return release;
}

QString UpdateChecker::extractVersionFromTag(const QString& tagName)
{
    QString version = tagName;

    // Remove "v" prefix (e.g. "v1.2.3" -> "1.2.3")
    if (version.startsWith("v", Qt::CaseInsensitive)) {
        version = version.mid(1);
    }

    return version.trimmed();
}
