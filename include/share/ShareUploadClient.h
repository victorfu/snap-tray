#ifndef SHAREUPLOADCLIENT_H
#define SHAREUPLOADCLIENT_H

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QPixmap>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

class ShareUploadClient : public QObject
{
    Q_OBJECT

public:
    struct PreparedPayload {
        QByteArray body;
        QByteArray contentType;
        QString errorMessage;
        bool usedJpeg = false;
        int jpegQuality = -1;

        bool isValid() const { return !body.isEmpty() && !contentType.isEmpty() && errorMessage.isEmpty(); }
    };

    static constexpr int kMaxUploadBytes = 5242880;
    static constexpr int kMaxPasswordLength = 128;

    explicit ShareUploadClient(QObject* parent = nullptr);

    void uploadPixmap(const QPixmap& pixmap, const QString& password = QString());
    bool isUploading() const { return m_reply != nullptr; }

    static PreparedPayload preparePayload(const QPixmap& pixmap, int maxBytes = kMaxUploadBytes);
    static bool parseSuccessResponse(const QByteArray& data,
                                     QString* url,
                                     QDateTime* expiresAt,
                                     bool* isProtected,
                                     QString* errorMessage = nullptr);
    static QString parseErrorMessage(const QByteArray& data, int statusCode);

signals:
    void uploadStarted();
    void uploadSucceeded(const QString& url, const QDateTime& expiresAt, bool isProtected);
    void uploadFailed(const QString& errorMessage);

private slots:
    void onReplyFinished();

private:
    QNetworkAccessManager* m_networkManager = nullptr;
    QNetworkReply* m_reply = nullptr;
};

#endif // SHAREUPLOADCLIENT_H
