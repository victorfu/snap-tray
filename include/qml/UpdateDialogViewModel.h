#pragma once

#include <QObject>
#include <QString>

class UpdateDialogViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int mode READ mode CONSTANT)
    Q_PROPERTY(QString errorMessage READ errorMessage CONSTANT)

public:
    enum Mode { Error = 0, Info = 1 };
    Q_ENUM(Mode)

    static UpdateDialogViewModel* createError(const QString& message, QObject* parent = nullptr);
    static UpdateDialogViewModel* createInfo(const QString& message, QObject* parent = nullptr);

    int mode() const;
    QString errorMessage() const;
    Q_INVOKABLE void retry();
    Q_INVOKABLE void close();

signals:
    void retryRequested();
    void closeRequested();

private:
    explicit UpdateDialogViewModel(QObject* parent = nullptr);

    Mode m_mode = Error;
    QString m_errorMessage;
};
