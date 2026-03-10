#include "qml/ShareResultViewModel.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QLocale>
#include <QUrl>

ShareResultViewModel::ShareResultViewModel(QObject* parent)
    : QObject(parent)
{
}

void ShareResultViewModel::setResult(const QString& url, const QDateTime& expiresAt,
                                     bool isProtected, const QString& password)
{
    m_url = url.trimmed();
    m_expiresAt = expiresAt;
    m_isProtected = isProtected;
    m_password = password;
    emit resultChanged();
}

QString ShareResultViewModel::url() const { return m_url; }
QString ShareResultViewModel::password() const { return m_password; }
bool ShareResultViewModel::isProtected() const { return m_isProtected; }
bool ShareResultViewModel::hasPassword() const { return !m_password.isEmpty(); }

QString ShareResultViewModel::expiresText() const
{
    if (!m_expiresAt.isValid())
        return {};
    return tr("Expires: %1").arg(
        QLocale::system().toString(m_expiresAt.toLocalTime(), QLocale::ShortFormat));
}

QString ShareResultViewModel::subtitleText() const
{
    if (m_expiresAt.isValid()) {
        return tr("Valid until %1").arg(
            QLocale::system().toString(m_expiresAt.toLocalTime(), QLocale::ShortFormat));
    }
    return tr("Upload completed");
}

QString ShareResultViewModel::hintText() const
{
    if (!m_password.isEmpty())
        return tr("Copy includes both link and password.");
    return tr("Anyone with this link can access it until expiration.");
}

QString ShareResultViewModel::protectedText() const
{
    if (m_isProtected && !m_password.isEmpty())
        return tr("Password protected");
    if (m_isProtected)
        return tr("Password protected (hidden)");
    return tr("No password");
}

void ShareResultViewModel::copyToClipboard()
{
    const QString text = clipboardText();
    if (!text.isEmpty()) {
        QApplication::clipboard()->setText(text);
        emit linkCopied(m_url);
    }
}

void ShareResultViewModel::openInBrowser()
{
    if (!m_url.isEmpty()) {
        QDesktopServices::openUrl(QUrl(m_url));
        emit linkOpened(m_url);
    }
}

void ShareResultViewModel::close()
{
    emit dialogClosed();
}

QString ShareResultViewModel::clipboardText() const
{
    if (m_url.isEmpty())
        return {};
    if (!m_password.isEmpty())
        return tr("%1\nPassword: %2").arg(m_url, m_password);
    return m_url;
}
