#pragma once

#include "update/IUpdateService.h"
#include <QObject>
#include <QDateTime>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QProcess>
#include <QTimer>

class QNetworkReply;

namespace SnapTray { class QmlDialog; }

class AppImageUpdateService final : public QObject, public IUpdateService
{
    Q_OBJECT
    friend class TestAppImageUpdateService;
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString version READ version NOTIFY changed)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
public:
    explicit AppImageUpdateService(QObject* parent = nullptr);
    ~AppImageUpdateService() override;
    UpdateServiceKind kind() const override { return UpdateServiceKind::AppImageUpdate; }
    InstallSource installSource() const override { return InstallSource::AppImage; }
    bool isExternallyManaged() const override { return false; }
    QString managementMessage() const override { return {}; }
    bool initialize(QString* error) override;
    void startAutomaticChecks() override;
    void syncSettings(bool enabled, int hours) override;
    UpdateCheckResult checkForUpdatesInteractive() override;
    bool isBusy() const override;
    QString state() const { return m_state; }
    QString message() const { return m_message; }
    QString version() const { return m_candidate.value("version").toString(); }
    QString releaseNotes() const { return m_candidate.value("notes").toString(); }
    double progress() const { return m_progress; }
    Q_INVOKABLE void download();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void install();
    Q_INVOKABLE void close();
    Q_INVOKABLE void retry();
    Q_INVOKABLE void discard();
    Q_INVOKABLE void openDownloads();
signals:
    void changed();
private:
    void check(bool interactive);
    void showDialog();
    void setState(const QString& state, const QString& message = {});
    void startHelper(const QStringList& arguments);
    void consumeOutput();
    QString m_image;
    QString m_helper;
    QString m_state = QStringLiteral("idle");
    QString m_message;
    QJsonObject m_candidate;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QProcess m_process;
    QTimer m_timer;
    QPointer<SnapTray::QmlDialog> m_dialog;
    QByteArray m_output;
    double m_progress = 0;
    bool m_autoCheck = true;
    bool m_interactive = false;
    bool m_cancelled = false;
    bool m_terminalEvent = false;
    int m_intervalHours = 24;
    QDateTime m_lastAttempt;
};
