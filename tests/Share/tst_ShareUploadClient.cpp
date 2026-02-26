#include <QtTest>

#include <QBuffer>
#include <QDateTime>
#include <QImage>
#include <QSignalSpy>

#include "share/ShareUploadClient.h"

namespace {
QByteArray encodePng(const QImage& image)
{
    QByteArray data;
    QBuffer buffer(&data);
    if (!buffer.open(QIODevice::WriteOnly)) {
        return {};
    }
    if (!image.save(&buffer, "PNG")) {
        return {};
    }
    return data;
}
} // namespace

class tst_ShareUploadClient : public QObject
{
    Q_OBJECT

private slots:
    void testPreparePayload_PngWithinLimit();
    void testPreparePayload_FallsBackToJpeg();
    void testPreparePayload_FailsWhenStillTooLarge();
    void testParseSuccessResponse();
    void testParseErrorMessage();
    void testUploadPixmap_RejectsLongPassword();
};

void tst_ShareUploadClient::testPreparePayload_PngWithinLimit()
{
    QPixmap pixmap(120, 80);
    pixmap.fill(QColor(255, 0, 0, 180));

    const ShareUploadClient::PreparedPayload payload = ShareUploadClient::preparePayload(pixmap);
    QVERIFY(payload.isValid());
    QCOMPARE(payload.contentType, QByteArray("image/png"));
    QVERIFY(!payload.usedJpeg);
    QVERIFY(payload.body.size() <= ShareUploadClient::kMaxUploadBytes);
}

void tst_ShareUploadClient::testPreparePayload_FallsBackToJpeg()
{
    QImage image(2200, 2200, QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            quint32 v = static_cast<quint32>(y * image.width() + x);
            v ^= (v >> 17);
            v *= 0xed5ad4bbU;
            v ^= (v >> 11);
            v *= 0xac4c1b51U;
            v ^= (v >> 15);
            v *= 0x31848babU;
            v ^= (v >> 14);
            const int r = static_cast<int>(v & 0xffU);
            const int g = static_cast<int>((v >> 8) & 0xffU);
            const int b = static_cast<int>((v >> 16) & 0xffU);
            line[x] = qRgba(r, g, b, 255);
        }
    }

    const QByteArray png = encodePng(image);
    QVERIFY(!png.isEmpty());
    QVERIFY(png.size() > ShareUploadClient::kMaxUploadBytes);

    const ShareUploadClient::PreparedPayload payload = ShareUploadClient::preparePayload(
        QPixmap::fromImage(image));
    QVERIFY(payload.isValid());
    QCOMPARE(payload.contentType, QByteArray("image/jpeg"));
    QVERIFY(payload.usedJpeg);
    QVERIFY(payload.jpegQuality > 0);
    QVERIFY(payload.body.size() <= ShareUploadClient::kMaxUploadBytes);
}

void tst_ShareUploadClient::testPreparePayload_FailsWhenStillTooLarge()
{
    QPixmap pixmap(120, 80);
    pixmap.fill(Qt::black);

    const ShareUploadClient::PreparedPayload payload = ShareUploadClient::preparePayload(pixmap, 64);
    QVERIFY(!payload.isValid());
    QVERIFY(!payload.errorMessage.isEmpty());
}

void tst_ShareUploadClient::testParseSuccessResponse()
{
    const QByteArray json = R"({
        "shortId": "Ner6W54X",
        "url": "https://c.snaptray.cc/s/Ner6W54X",
        "expiresAt": "2026-02-26T15:00:00.000Z",
        "size": 123456,
        "protected": true
    })";

    QString url;
    QDateTime expiresAt;
    bool isProtected = false;
    QString error;
    QVERIFY(ShareUploadClient::parseSuccessResponse(json, &url, &expiresAt, &isProtected, &error));
    QCOMPARE(url, QString("https://c.snaptray.cc/s/Ner6W54X"));
    QVERIFY(expiresAt.isValid());
    QVERIFY(isProtected);
    QVERIFY(error.isEmpty());
}

void tst_ShareUploadClient::testParseErrorMessage()
{
    const QByteArray json = R"({"error":"File too large"})";
    QCOMPARE(ShareUploadClient::parseErrorMessage(json, 413), QString("File too large"));

    const QString fallback = ShareUploadClient::parseErrorMessage("{}", 503);
    QVERIFY(!fallback.isEmpty());
}

void tst_ShareUploadClient::testUploadPixmap_RejectsLongPassword()
{
    ShareUploadClient client;
    QSignalSpy failSpy(&client, &ShareUploadClient::uploadFailed);

    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::blue);
    client.uploadPixmap(pixmap, QString(ShareUploadClient::kMaxPasswordLength + 1, QLatin1Char('a')));

    QCOMPARE(failSpy.count(), 1);
    const QString message = failSpy.at(0).at(0).toString();
    QVERIFY(message.contains("Password", Qt::CaseInsensitive));
}

QTEST_MAIN(tst_ShareUploadClient)
#include "tst_ShareUploadClient.moc"
