#pragma once

#include <QObject>
#include <QDateTime>

class ShareResultViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString url READ url NOTIFY resultChanged)
    Q_PROPERTY(QString password READ password NOTIFY resultChanged)
    Q_PROPERTY(QString expiresText READ expiresText NOTIFY resultChanged)
    Q_PROPERTY(bool isProtected READ isProtected NOTIFY resultChanged)
    Q_PROPERTY(bool hasPassword READ hasPassword NOTIFY resultChanged)
    Q_PROPERTY(QString hintText READ hintText NOTIFY resultChanged)
    Q_PROPERTY(QString protectedText READ protectedText NOTIFY resultChanged)
    Q_PROPERTY(QString subtitleText READ subtitleText NOTIFY resultChanged)

public:
    explicit ShareResultViewModel(QObject* parent = nullptr);

    void setResult(const QString& url, const QDateTime& expiresAt,
                   bool isProtected, const QString& password = QString());

    QString url() const;
    QString password() const;
    QString expiresText() const;
    bool isProtected() const;
    bool hasPassword() const;
    QString hintText() const;
    QString protectedText() const;
    QString subtitleText() const;

    Q_INVOKABLE void copyToClipboard();
    Q_INVOKABLE void openInBrowser();
    Q_INVOKABLE void close();

signals:
    void resultChanged();
    void dialogClosed();
    void linkCopied(const QString& url);
    void linkOpened(const QString& url);

private:
    QString clipboardText() const;

    QString m_url;
    QString m_password;
    QDateTime m_expiresAt;
    bool m_isProtected = false;
};
