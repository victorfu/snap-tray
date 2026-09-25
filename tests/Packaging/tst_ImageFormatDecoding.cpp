#include <QtTest>
#include <QFile>
#include <QImageReader>
#include <QTemporaryDir>

class tst_ImageFormatDecoding : public QObject
{
    Q_OBJECT

private slots:
    void decode_data();
    void decode();
};

void tst_ImageFormatDecoding::decode_data()
{
    QTest::addColumn<QString>("extension");
    QTest::addColumn<QByteArray>("encoded");

    // Fixed 2x2 four-color fixtures, independently encoded with Pillow:
    // lossless WebP and uncompressed TIFF. Do not generate with QImageWriter:
    // the decoder must be tested even when the Qt encoder is unavailable.
    // Keep the WebP at least 40 bytes: Qt's QWebpHandler::ensureScanned()
    // probes sizeof(WebPBitstreamFeatures) bytes and rejects smaller files.
    const QByteArray webp = QByteArray::fromBase64(
        "UklGRiYAAABXRUJQVlA4TBoAAAAvAUAAAB9wkZlHzHRm/oPbQDZAmSrMIvofOw==");
    const QByteArray tiff = QByteArray::fromBase64(
        "SUkqAAgAAAAKAAABBAABAAAAAgAAAAEBBAABAAAAAgAAAAIBAwADAAAAhgAAAAMBAwABAAAAAQAAAAYBAwABAAAAAgAAABEBBAABAAAAjAAAABUBAwABAAAAAwAAABYBBAABAAAAAgAAABcBBAABAAAADAAAABwBAwABAAAAAQAAAAAAAAAIAAgACAARIjNEVWZ3iJmqu8w=");
    QTest::newRow("webp") << QStringLiteral("webp") << webp;
    QTest::newRow("tiff") << QStringLiteral("tiff") << tiff;
    QTest::newRow("tif") << QStringLiteral("tif") << tiff;
}

void tst_ImageFormatDecoding::decode()
{
    QFETCH(QString, extension);
    QFETCH(QByteArray, encoded);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("image.") + extension);
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(encoded), qint64(encoded.size()));
    file.close();

    // Match MainApplication::loadImageForPin: infer the format from the file
    // and apply its orientation metadata. A missing plugin must fail, not skip.
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    QVERIFY2(!image.isNull(), qPrintable(reader.errorString()));
    QCOMPARE(image.size(), QSize(2, 2));
    const QColor expectedPixels[2][2] = {
        {QColor(17, 34, 51), QColor(68, 85, 102)},
        {QColor(119, 136, 153), QColor(170, 187, 204)}
    };
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            QCOMPARE(image.pixelColor(x, y), expectedPixels[y][x]);
        }
    }
}

QTEST_GUILESS_MAIN(tst_ImageFormatDecoding)
#include "tst_ImageFormatDecoding.moc"
