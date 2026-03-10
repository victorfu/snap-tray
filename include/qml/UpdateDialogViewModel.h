#pragma once

#include "update/UpdateChecker.h"
#include <QObject>

class UpdateDialogViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int mode READ mode NOTIFY modeChanged)
    Q_PROPERTY(QString version READ version NOTIFY modeChanged)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY modeChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY modeChanged)
    Q_PROPERTY(QString appName READ appName CONSTANT)

public:
    enum Mode { UpdateAvailable = 0, UpToDate = 1, Error = 2 };
    Q_ENUM(Mode)

    static UpdateDialogViewModel* createForRelease(const ReleaseInfo& release, QObject* parent = nullptr);
    static UpdateDialogViewModel* createUpToDate(QObject* parent = nullptr);
    static UpdateDialogViewModel* createError(const QString& message, QObject* parent = nullptr);

    int mode() const;
    QString version() const;
    QString currentVersion() const;
    QString releaseNotes() const;
    QString errorMessage() const;
    QString appName() const;

    Q_INVOKABLE void download();
    Q_INVOKABLE void remindLater();
    Q_INVOKABLE void skipVersion();
    Q_INVOKABLE void retry();
    Q_INVOKABLE void close();

signals:
    void modeChanged();
    void retryRequested();
    void closeRequested();

private:
    explicit UpdateDialogViewModel(QObject* parent = nullptr);

    Mode m_mode = UpdateAvailable;
    ReleaseInfo m_release;
    QString m_errorMessage;
};
