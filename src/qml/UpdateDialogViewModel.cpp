#include "qml/UpdateDialogViewModel.h"
#include "update/UpdateSettingsManager.h"
#include "version.h"

#include <QDesktopServices>
#include <QUrl>

UpdateDialogViewModel::UpdateDialogViewModel(QObject* parent)
    : QObject(parent)
{
}

UpdateDialogViewModel* UpdateDialogViewModel::createForRelease(const ReleaseInfo& release, QObject* parent)
{
    auto* vm = new UpdateDialogViewModel(parent);
    vm->m_mode = UpdateAvailable;
    vm->m_release = release;
    return vm;
}

UpdateDialogViewModel* UpdateDialogViewModel::createUpToDate(QObject* parent)
{
    auto* vm = new UpdateDialogViewModel(parent);
    vm->m_mode = UpToDate;
    return vm;
}

UpdateDialogViewModel* UpdateDialogViewModel::createError(const QString& message, QObject* parent)
{
    auto* vm = new UpdateDialogViewModel(parent);
    vm->m_mode = Error;
    vm->m_errorMessage = message;
    return vm;
}

UpdateDialogViewModel* UpdateDialogViewModel::createInfo(const QString& message, QObject* parent)
{
    auto* vm = new UpdateDialogViewModel(parent);
    vm->m_mode = Info;
    vm->m_errorMessage = message;
    return vm;
}

int UpdateDialogViewModel::mode() const { return static_cast<int>(m_mode); }
QString UpdateDialogViewModel::version() const { return m_release.version; }
QString UpdateDialogViewModel::currentVersion() const { return UpdateChecker::currentVersion(); }
QString UpdateDialogViewModel::releaseNotes() const { return m_release.releaseNotes; }
QString UpdateDialogViewModel::errorMessage() const { return m_errorMessage; }
QString UpdateDialogViewModel::appName() const { return QStringLiteral(SNAPTRAY_APP_NAME); }

void UpdateDialogViewModel::download()
{
    QString url = m_release.htmlUrl;
    if (url.isEmpty()) {
        url = QString("https://github.com/%1/%2/releases")
                  .arg(UpdateChecker::kGitHubOwner, UpdateChecker::kGitHubRepo);
    }
    QDesktopServices::openUrl(QUrl(url));
    emit closeRequested();
}

void UpdateDialogViewModel::remindLater()
{
    emit closeRequested();
}

void UpdateDialogViewModel::skipVersion()
{
    if (!m_release.version.isEmpty()) {
        UpdateSettingsManager::instance().setSkippedVersion(m_release.version);
    }
    emit closeRequested();
}

void UpdateDialogViewModel::retry()
{
    emit retryRequested();
    emit closeRequested();
}

void UpdateDialogViewModel::close()
{
    emit closeRequested();
}
