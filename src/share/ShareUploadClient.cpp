#include "share/ShareUploadClient.h"

#include <QBuffer>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>

namespace {
constexpr const char* kUploadEndpoint = "https://x.snaptray.cc/api/upload";

QByteArray encodeImage(const QImage& image, const char* format, int quality = -1)
{
    QByteArray data;
    QBuffer buffer(&data);
    if (!buffer.open(QIODevice::WriteOnly)) {
        return {};
    }
    if (!image.save(&buffer, format, quality)) {
        return {};
    }
    return data;
}

QImage flattenAlphaToWhite(const QImage& source)
{
    if (!source.hasAlphaChannel()) {
        return source.convertToFormat(QImage::Format_RGB32);
    }

    QImage flattened(source.size(), QImage::Format_RGB32);
    flattened.fill(Qt::white);
    QPainter painter(&flattened);
    painter.drawImage(0, 0, source);
    return flattened;
}

QString fallbackStatusMessage(int statusCode)
{
    switch (statusCode) {
    case 400:
        return QObject::tr("Invalid upload request");
    case 405:
        return QObject::tr("Upload method not allowed");
    case 413:
        return QObject::tr("File too large (max 5MB)");
    case 500:
        return QObject::tr("Server error while uploading");
    case 503:
        return QObject::tr("Upload service unavailable, please retry");
    default:
        if (statusCode > 0) {
            return QObject::tr("Upload failed (HTTP %1)").arg(statusCode);
        }
        return QObject::tr("Upload failed");
    }
}
} // namespace

ShareUploadClient::ShareUploadClient(QObject* parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
}

void ShareUploadClient::uploadPixmap(const QPixmap& pixmap, const QString& password)
{
    if (m_reply) {
        emit uploadFailed(tr("Upload already in progress"));
        return;
    }

    if (password.size() > kMaxPasswordLength) {
        emit uploadFailed(tr("Password too long (max %1 characters)").arg(kMaxPasswordLength));
        return;
    }

    const PreparedPayload payload = preparePayload(pixmap, kMaxUploadBytes);
    if (!payload.isValid()) {
        emit uploadFailed(payload.errorMessage.isEmpty() ? tr("Failed to prepare image for upload") : payload.errorMessage);
        return;
    }

    QNetworkRequest request(QUrl(QString::fromLatin1(kUploadEndpoint)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QString::fromLatin1(payload.contentType));
    request.setHeader(QNetworkRequest::ContentLengthHeader, payload.body.size());

    const QByteArray passwordBytes = password.toUtf8();
    if (!passwordBytes.isEmpty()) {
        request.setRawHeader("X-Share-Password", passwordBytes);
    }

    emit uploadStarted();
    m_reply = m_networkManager->post(request, payload.body);
    connect(m_reply, &QNetworkReply::finished, this, &ShareUploadClient::onReplyFinished);
}

ShareUploadClient::PreparedPayload ShareUploadClient::preparePayload(const QPixmap& pixmap, int maxBytes)
{
    PreparedPayload payload;
    if (pixmap.isNull()) {
        payload.errorMessage = tr("No image available to share");
        return payload;
    }

    if (maxBytes <= 0) {
        payload.errorMessage = tr("Invalid upload size limit");
        return payload;
    }

    const QImage sourceImage = pixmap.toImage();
    if (sourceImage.isNull()) {
        payload.errorMessage = tr("Failed to read image data");
        return payload;
    }

    const QByteArray png = encodeImage(sourceImage, "PNG");
    if (png.isEmpty()) {
        payload.errorMessage = tr("Failed to encode PNG image");
        return payload;
    }
    if (png.size() <= maxBytes) {
        payload.body = png;
        payload.contentType = "image/png";
        payload.usedJpeg = false;
        payload.jpegQuality = -1;
        return payload;
    }

    const QImage jpegSource = flattenAlphaToWhite(sourceImage);
    const int qualities[] = {92, 85, 75, 65, 55, 45, 35, 25};
    for (int quality : qualities) {
        const QByteArray jpeg = encodeImage(jpegSource, "JPEG", quality);
        if (jpeg.isEmpty()) {
            continue;
        }
        if (jpeg.size() <= maxBytes) {
            payload.body = jpeg;
            payload.contentType = "image/jpeg";
            payload.usedJpeg = true;
            payload.jpegQuality = quality;
            return payload;
        }
    }

    payload.errorMessage = tr("Image exceeds 5MB even after compression. Please crop a smaller region.");
    return payload;
}

bool ShareUploadClient::parseSuccessResponse(const QByteArray& data,
                                             QString* url,
                                             QDateTime* expiresAt,
                                             bool* isProtected,
                                             QString* errorMessage)
{
    if (url) {
        url->clear();
    }
    if (expiresAt) {
        *expiresAt = QDateTime();
    }
    if (isProtected) {
        *isProtected = false;
    }
    if (errorMessage) {
        errorMessage->clear();
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorMessage) {
            *errorMessage = tr("Invalid upload response");
        }
        return false;
    }

    const QJsonObject obj = doc.object();
    const QString parsedUrl = obj.value("url").toString().trimmed();
    if (parsedUrl.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("Upload response missing URL");
        }
        return false;
    }

    const QString expiresAtString = obj.value("expiresAt").toString();
    const QDateTime parsedExpiresAt = QDateTime::fromString(expiresAtString, Qt::ISODate);

    if (url) {
        *url = parsedUrl;
    }
    if (expiresAt) {
        *expiresAt = parsedExpiresAt;
    }
    if (isProtected) {
        *isProtected = obj.value("protected").toBool(false);
    }

    return true;
}

QString ShareUploadClient::parseErrorMessage(const QByteArray& data, int statusCode)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
        const QString error = doc.object().value("error").toString().trimmed();
        if (!error.isEmpty()) {
            return error;
        }
    }
    return fallbackStatusMessage(statusCode);
}

void ShareUploadClient::onReplyFinished()
{
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;
    if (!reply) {
        return;
    }
    reply->deleteLater();

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();

    if (statusCode == 201) {
        QString url;
        QDateTime expiresAt;
        bool isProtected = false;
        QString parseError;
        if (parseSuccessResponse(body, &url, &expiresAt, &isProtected, &parseError)) {
            emit uploadSucceeded(url, expiresAt, isProtected);
            return;
        }
        emit uploadFailed(parseError.isEmpty() ? tr("Upload succeeded but response could not be parsed") : parseError);
        return;
    }

    if (reply->error() != QNetworkReply::NoError && statusCode <= 0) {
        emit uploadFailed(reply->errorString());
        return;
    }

    emit uploadFailed(parseErrorMessage(body, statusCode));
}
