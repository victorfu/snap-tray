#include "update/AppImageUpdateService.h"
#include "update/UpdateCoordinator.h"
#include "update/UpdateSettingsManager.h"
#include "qml/QmlDialog.h"
#include "qml/QmlToast.h"
#include "version.h"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>
#include <QVersionNumber>

namespace {
constexpr int kNetworkTimeoutMs = 30000;
constexpr int kAutoCheckPollMs = 60000;
QString stagePath(const QString& image) { return image + QStringLiteral(".snaptray-update"); }
}

AppImageUpdateService::AppImageUpdateService(QObject* parent) : QObject(parent)
{
    connect(this, &AppImageUpdateService::changed, &UpdateCoordinator::instance(),
            &UpdateCoordinator::updateStateChanged);
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &AppImageUpdateService::consumeOutput);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            setState("error", tr("The update helper could not be started."));
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        consumeOutput();
        if (m_cancelled) { setState("available", tr("Download cancelled. Your current version is unchanged.")); return; }
        if ((status != QProcess::NormalExit || code != 0 || !m_terminalEvent) && m_state != "error")
            setState("error", tr("The update could not be verified or completed. Your current version is unchanged."));
    });
    m_timer.setInterval(kAutoCheckPollMs);
    connect(&m_timer, &QTimer::timeout, this, [this]() {
        if (!m_autoCheck || isBusy() || m_state == "ready" || m_state == "available") return;
        if (m_lastAttempt.isValid() && m_lastAttempt.secsTo(QDateTime::currentDateTimeUtc()) < 3600) return;
        const auto last = UpdateCoordinator::instance().lastCheckTime();
        if (!last.isValid() || last.secsTo(QDateTime::currentDateTimeUtc()) >= qint64(m_intervalHours) * 3600)
            check(false);
    });
}
AppImageUpdateService::~AppImageUpdateService()
{
    if (m_reply) m_reply->abort();
    if (m_process.state() != QProcess::NotRunning) { m_process.kill(); m_process.waitForFinished(3000); }
    if (m_dialog) delete m_dialog;
}
bool AppImageUpdateService::initialize(QString* error)
{
    m_image = QFileInfo(qEnvironmentVariable("APPIMAGE")).canonicalFilePath();
    m_helper = QCoreApplication::applicationDirPath() + "/snaptray-appimage-updater";
    if (m_image.isEmpty() || !QFileInfo(m_helper).isExecutable()) {
        const auto message = tr("This installation does not include the AppImage updater. Download a newer AppImage from GitHub Releases.");
        if (error) *error = message;
        setState("error", message);
        return false;
    }
    if (error) error->clear();
    return true;
}
bool AppImageUpdateService::isBusy() const
{
    return m_state == "checking" || m_state == "downloading" || m_state == "verifying" || m_state == "installing";
}
void AppImageUpdateService::setState(const QString& state, const QString& message)
{
    m_state = state; m_message = message; emit changed();
}
void AppImageUpdateService::syncSettings(bool enabled, int hours)
{
    m_autoCheck = enabled; m_intervalHours = qMax(1, hours);
}
void AppImageUpdateService::startAutomaticChecks()
{
    m_timer.start(); // Do not interrupt application startup with network requests.
}
UpdateCheckResult AppImageUpdateService::checkForUpdatesInteractive()
{
    showDialog();
    if (!isBusy() && m_state != "ready" && m_state != "available") check(true);
    return UpdateCheckResult::started();
}
void AppImageUpdateService::showDialog()
{
    if (m_dialog) { m_dialog->showCenteredOnScreen(QGuiApplication::primaryScreen()); return; }
    m_dialog = new SnapTray::QmlDialog(QUrl("qrc:/SnapTrayQml/dialogs/AppImageUpdateDialog.qml"), this, "viewModel", this, SnapTray::QmlDialog::ViewModelOwnership::Borrowed);
    m_dialog->showCenteredOnScreen(QGuiApplication::primaryScreen());
}
void AppImageUpdateService::close() { if (m_dialog) m_dialog->close(); }
void AppImageUpdateService::retry() { if (!isBusy()) check(true); }
void AppImageUpdateService::openDownloads() { QDesktopServices::openUrl(QUrl("https://github.com/victorfu/snap-tray/releases/latest")); }
void AppImageUpdateService::check(bool interactive)
{
    QString error;
    if (!initialize(&error)) return;
    m_interactive = interactive;
    m_lastAttempt = QDateTime::currentDateTimeUtc();
    if (QFileInfo::exists(stagePath(m_image) + "/pending.json")) {
        setState("verifying", tr("Verifying the downloaded update…"));
        startHelper({"pending", m_image});
        return;
    }
    setState("checking", tr("Checking for updates…"));
    QNetworkRequest request(QUrl("https://api.github.com/repos/victorfu/snap-tray/releases/latest"));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "SnapTray-AppImage-Updater");
    request.setTransferTimeout(kNetworkTimeoutMs);
    m_reply = m_network.get(request);
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        auto* reply = m_reply.data(); m_reply = nullptr;
        if (!reply) return;
        const auto error = reply->error();
        const auto bytes = reply->readAll(); reply->deleteLater();
        if (error != QNetworkReply::NoError) { setState("error", tr("Could not check for updates. Check your connection and try again.")); return; }
        const auto release = QJsonDocument::fromJson(bytes).object();
        QString tag = release.value("tag_name").toString();
        const QRegularExpression versionPattern("^v([0-9]+\\.[0-9]+\\.[0-9]+)$");
        const auto match = versionPattern.match(tag);
        if (!match.hasMatch() || release.value("draft").toBool() || release.value("prerelease").toBool()) {
            setState("error", tr("The release information is invalid.")); return;
        }
        const auto version = match.captured(1);
        if (QVersionNumber::fromString(version) <= QVersionNumber::fromString(SNAPTRAY_VERSION)) {
            UpdateCoordinator::instance().recordSuccessfulCheck();
            setState("current", tr("You are running the latest version.")); return;
        }
        const QString name = "SnapTray-" + version + "-x86_64.AppImage";
        const QString base = "https://github.com/victorfu/snap-tray/releases/download/" + tag + "/";
        bool imageFound = false, syncFound = false;
        for (const auto& value : release.value("assets").toArray()) {
            const auto asset = value.toObject();
            if (asset.value("name").toString() == name && asset.value("browser_download_url").toString() == base + name) imageFound = true;
            if (asset.value("name").toString() == name + ".zsync" && asset.value("browser_download_url").toString() == base + name + ".zsync") syncFound = true;
        }
        if (!imageFound || !syncFound) { setState("error", tr("This release does not yet contain the Linux update files. Try again later.")); return; }
        m_candidate = {{"version", version}, {"notes", release.value("body").toString().left(64000)}, {"url", base + name + ".zsync"}};
        UpdateCoordinator::instance().recordSuccessfulCheck();
        setState("available", tr("SnapTray %1 is available.").arg(version));
        if (!m_interactive)
            SnapTray::QmlToast::screenToast().showToast(SnapTray::QmlToast::Level::Info, tr("Update available"), tr("SnapTray %1 is available. Choose Check for Updates from the tray menu.").arg(version));
    });
}
void AppImageUpdateService::startHelper(const QStringList& arguments)
{
    m_output.clear(); m_cancelled = false; m_terminalEvent = false;
    m_process.setProgram(m_helper); m_process.setArguments(arguments); m_process.start();
}
void AppImageUpdateService::consumeOutput()
{
    m_output += m_process.readAllStandardOutput();
    if (m_output.size() > 1024 * 1024) { m_process.kill(); return; }
    while (m_output.contains('\n')) {
        const int end = m_output.indexOf('\n');
        const auto event = QJsonDocument::fromJson(m_output.left(end)).object(); m_output.remove(0, end + 1);
        const auto type = event.value("event").toString();
        if (type == "progress") { m_progress = qBound(0.0, event.value("progress").toDouble(), 1.0); emit changed(); }
        else if (type == "verifying") setState("verifying", tr("Verifying the update…"));
        else if (type == "ready") {
            m_terminalEvent = true;
            m_candidate["version"] = event.value("version");
            setState("ready", tr("The update is ready. Restart SnapTray to install it."));
        } else if (type == "discarded") {
            m_terminalEvent = true;
            m_candidate = {}; m_progress = 0;
            setState("idle", tr("Downloaded update discarded. Check again when you are ready."));
        } else if (type == "error") {
            m_terminalEvent = true; setState("error", event.value("message").toString());
        }
    }
}
void AppImageUpdateService::download()
{
    if (m_state != "available") return;
    m_progress = 0; setState("downloading", tr("Downloading update…"));
    startHelper({"download", m_image, m_candidate.value("url").toString(), version()});
}
void AppImageUpdateService::cancel()
{
    if (m_state != "downloading" && m_state != "verifying") return;
    m_cancelled = true; m_process.terminate();
    QTimer::singleShot(3000, this, [this]() { if (m_cancelled && m_process.state() != QProcess::NotRunning) m_process.kill(); });
}
void AppImageUpdateService::install()
{
    if (m_state != "ready") return;
    if (!UpdateCoordinator::canRequestApplicationShutdown()) {
        m_message = tr("Finish the current operation before restarting."); emit changed(); return;
    }
    const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto armed = stagePath(m_image) + "/armed-" + token;
    QProcess process;
    process.setProgram(m_helper);
    process.setArguments({"apply", m_image, QString::number(QCoreApplication::applicationPid()), token});
    process.setStandardOutputFile(QProcess::nullDevice()); process.setStandardErrorFile(QProcess::nullDevice());
    if (!process.startDetached()) { setState("error", tr("Could not start the installer.")); return; }
    setState("installing", tr("Preparing to restart…"));
    auto* timer = new QTimer(this); timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, [this, timer, armed, attempts = 0]() mutable {
        if (QFileInfo::exists(armed)) {
            timer->stop(); timer->deleteLater();
            if (UpdateCoordinator::canRequestApplicationShutdown()) UpdateCoordinator::requestApplicationShutdown();
            else setState("ready", tr("Finish the current operation before restarting."));
        } else if (++attempts >= 300) {
            timer->stop(); timer->deleteLater(); setState("error", tr("The installer could not prepare the update. Try again."));
        }
    });
    timer->start();
}

void AppImageUpdateService::discard()
{
    if (isBusy()) return;
    setState("verifying", tr("Discarding the downloaded update…"));
    startHelper({"discard", m_image});
}
