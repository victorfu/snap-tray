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

    // Fixed 2x2 RGB(17, 34, 51) fixtures, independently encoded with Pillow:
    // lossless WebP and uncompressed TIFF. Do not generate with QImageWriter:
    // the decoder must be tested even when the Qt encoder is unavailable.
    const QByteArray webp = QByteArray::fromBase64(
        "UklGRh4AAABXRUJQVlA4TBEAAAAvAUAAAAdQkUZ0pv+BiOh/AAA=");
    const QByteArray tiff = QByteArray::fromBase64(
        "SUkqAAgAAAAKAAABBAABAAAAAgAAAAEBBAABAAAAAgAAAAIBAwADAAAAhgAAAAMBAwABAAAAAQAAAAYBAwABAAAAAgAAABEBBAABAAAAjAAAABUBAwABAAAAAwAAABYBBAABAAAAAgAAABcBBAABAAAADAAAABwBAwABAAAAAQAAAAAAAAAIAAgACAARIjMRIjMRIjMRIjM=");
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
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            QCOMPARE(image.pixelColor(x, y), QColor(17, 34, 51));
        }
    }
}

QTEST_GUILESS_MAIN(tst_ImageFormatDecoding)
#include "tst_ImageFormatDecoding.moc"
