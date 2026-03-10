#pragma once

#include <QObject>

class SharePasswordViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString password READ password WRITE setPassword NOTIFY passwordChanged)
    Q_PROPERTY(int maxLength READ maxLength CONSTANT)

public:
    explicit SharePasswordViewModel(QObject* parent = nullptr);

    QString password() const;
    void setPassword(const QString& password);
    int maxLength() const;

    Q_INVOKABLE void accept();
    Q_INVOKABLE void cancel();

signals:
    void passwordChanged();
    void accepted();
    void rejected();

private:
    QString m_password;
};
