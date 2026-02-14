#include <QtTest>

#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QTemporaryDir>

#include "utils/ImageSaveUtils.h"

class tst_ImageSaveUtils : public QObject
{
    Q_OBJECT

private slots:
    void testSavePngSuccess();
    void testSaveWithoutExtensionDefaultsToPng();
    void testUnsupportedExtensionFails();
    void testOpenFailureReportsError();
    void testOverwriteExistingFile();
    void testOverwriteWithReadOnlyDirectoryUsesFallback();
};

void tst_ImageSaveUtils::testSavePngSuccess()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QImage image(24, 24, QImage::Format_ARGB32);
    image.fill(QColor(255, 0, 0, 255));

    const QString filePath = tempDir.filePath("capture.png");
    ImageSaveUtils::Error error;
    QVERIFY(ImageSaveUtils::saveImageAtomically(image, filePath, QByteArrayLiteral("PNG"), &error));

    QVERIFY(QFile::exists(filePath));
    QImage loaded(filePath);
    QVERIFY(!loaded.isNull());
    QCOMPARE(loaded.pixelColor(0, 0), QColor(255, 0, 0, 255));
}

void tst_ImageSaveUtils::testSaveWithoutExtensionDefaultsToPng()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QImage image(16, 16, QImage::Format_ARGB32);
    image.fill(QColor(0, 255, 0, 255));

    const QString filePath = tempDir.filePath("capture_no_ext");
    ImageSaveUtils::Error error;
    QVERIFY(ImageSaveUtils::saveImageAtomically(image, filePath, QByteArray(), &error));

    QVERIFY(QFile::exists(filePath));
    QImageReader reader(filePath);
    QVERIFY(reader.canRead());
    QCOMPARE(reader.format().toLower(), QByteArrayLiteral("png"));
}

void tst_ImageSaveUtils::testUnsupportedExtensionFails()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(Qt::blue);

    const QString filePath = tempDir.filePath("capture.unsupported_ext");
    ImageSaveUtils::Error error;
    QVERIFY(!ImageSaveUtils::saveImageAtomically(image, filePath, QByteArray(), &error));
    QCOMPARE(error.stage, QStringLiteral("format"));
    QVERIFY(error.message.contains("Unsupported image format"));
    QVERIFY(!QFile::exists(filePath));
}

void tst_ImageSaveUtils::testOpenFailureReportsError()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(Qt::yellow);

    // Writing to an existing directory path should fail at open stage.
    ImageSaveUtils::Error error;
    QVERIFY(!ImageSaveUtils::saveImageAtomically(image, tempDir.path(), QByteArrayLiteral("PNG"), &error));
    QCOMPARE(error.stage, QStringLiteral("open"));
    QVERIFY(!error.message.isEmpty());
}

void tst_ImageSaveUtils::testOverwriteExistingFile()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString filePath = tempDir.filePath("overwrite.png");

    QImage red(12, 12, QImage::Format_ARGB32);
    red.fill(QColor(255, 0, 0, 255));
    ImageSaveUtils::Error firstError;
    QVERIFY(ImageSaveUtils::saveImageAtomically(red, filePath, QByteArray(), &firstError));

    QImage green(12, 12, QImage::Format_ARGB32);
    green.fill(QColor(0, 255, 0, 255));
    ImageSaveUtils::Error secondError;
    QVERIFY(ImageSaveUtils::saveImageAtomically(green, filePath, QByteArray(), &secondError));

    QImage loaded(filePath);
    QVERIFY(!loaded.isNull());
    QCOMPARE(loaded.pixelColor(0, 0), QColor(0, 255, 0, 255));
}

void tst_ImageSaveUtils::testOverwriteWithReadOnlyDirectoryUsesFallback()
{
#ifndef Q_OS_UNIX
    QSKIP("Permission-based fallback scenario is Unix-only");
#else
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dirPath = tempDir.path();
    const QString filePath = tempDir.filePath("overwrite_readonly_dir.png");

    QImage base(10, 10, QImage::Format_ARGB32);
    base.fill(QColor(255, 0, 0, 255));
    ImageSaveUtils::Error baseError;
    QVERIFY(ImageSaveUtils::saveImageAtomically(base, filePath, QByteArrayLiteral("PNG"), &baseError));

    const QFileDevice::Permissions originalDirPerms = QFileInfo(dirPath).permissions();
    struct DirPermsGuard {
        QString path;
        QFileDevice::Permissions perms;
        ~DirPermsGuard() { QFile::setPermissions(path, perms); }
    } guard{dirPath, originalDirPerms};

    QFileDevice::Permissions readOnlyDirPerms = originalDirPerms;
    readOnlyDirPerms &= ~(QFileDevice::WriteOwner | QFileDevice::WriteGroup | QFileDevice::WriteOther);
    if (!QFile::setPermissions(dirPath, readOnlyDirPerms)) {
        QSKIP("Unable to adjust directory permissions for fallback test");
    }

    QImage updated(10, 10, QImage::Format_ARGB32);
    updated.fill(QColor(0, 0, 255, 255));
    ImageSaveUtils::Error saveError;
    const bool ok = ImageSaveUtils::saveImageAtomically(
        updated, filePath, QByteArrayLiteral("PNG"), &saveError);
    QVERIFY2(ok, qPrintable(QStringLiteral("stage=%1 message=%2").arg(saveError.stage, saveError.message)));

    QImage loaded(filePath);
    QVERIFY(!loaded.isNull());
    QCOMPARE(loaded.pixelColor(0, 0), QColor(0, 0, 255, 255));
#endif
}

QTEST_MAIN(tst_ImageSaveUtils)
#include "tst_ImageSaveUtils.moc"
