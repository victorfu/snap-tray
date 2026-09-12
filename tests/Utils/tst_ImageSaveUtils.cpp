#include <QtTest>

#include <QColorSpace>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QPixmap>
#include <QScreen>
#include <QTemporaryDir>
#include <utility>
#include <QDir>
#include <QProcess>
#include <QImageWriter>
#include <condition_variable>
#include <future>
#include <mutex>
#ifndef Q_OS_WIN
#include <unistd.h>
#endif

#include "utils/ImageSaveUtils.h"
#include "ImageSaveUtilsTestAccess.h"

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
    void testSaveImageWithColorSpacePreservesProfile();
    void testUniqueConcurrentThreads();
    void testUniqueConcurrentProcesses();
    void testUniqueCounterAndUuidCollisions();
    void testUniquePreservesSymlink();
    void testUniqueFailureCleansTemporaryFile();
    void testUniqueEncodingFailure();
#ifdef Q_OS_MAC
    void testPixmapRoundTripPreservesTaggedColorSpace();
    void testSavePixmapPreservesTaggedColorSpace();
    void testSavePixmapAppliesScreenColorSpaceWhenMissing();
#endif
};

namespace {
QImage solidImage(Qt::GlobalColor color)
{
    QImage image(24, 24, QImage::Format_ARGB32);
    image.fill(color);
    return image;
}
void writeSentinel(const QString& path)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    QCOMPARE(file.write("sentinel"), qint64(8));
}
}

void tst_ImageSaveUtils::testUniqueConcurrentThreads()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ImageSaveUtils::UniqueSaveSpec spec{dir.path(), "same.png", {}};
    std::mutex mutex;
    std::condition_variable ready;
    int arrived = 0;
    auto worker = [&](Qt::GlobalColor color) {
        bool first = true;
        return ImageSaveUtilsTestAccess::save(solidImage(color), spec,
            [&](const QString& source, const QString& destination) {
                if (first) {
                    first = false;
                    std::unique_lock<std::mutex> lock(mutex);
                    ++arrived;
                    ready.notify_all();
                    if (!ready.wait_for(lock, std::chrono::seconds(10), [&] { return arrived == 2; }))
                        return SnapTray::FilePublishResult{SnapTray::FilePublishStatus::Failed, "barrier timeout"};
                }
                return SnapTray::publishFileNoReplace(source, destination);
            });
    };
    auto first = std::async(std::launch::async, worker, Qt::red);
    auto second = std::async(std::launch::async, worker, Qt::blue);
    const auto a = first.get();
    const auto b = second.get();
    QVERIFY2(a.success, qPrintable(a.error.message));
    QVERIFY2(b.success, qPrintable(b.error.message));
    QVERIFY(a.filePath != b.filePath);
    QCOMPARE(QImage(a.filePath), solidImage(Qt::red));
    QCOMPARE(QImage(b.filePath), solidImage(Qt::blue));
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).size(), 2);
}

void tst_ImageSaveUtils::testUniqueConcurrentProcesses()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString program = QCoreApplication::applicationDirPath() + "/Utils_UniqueImageSaveProcess";
    QProcess first, second;
    first.start(program, {dir.path(), "red"});
    second.start(program, {dir.path(), "blue"});
    for (QProcess* process : {&first, &second}) {
        QVERIFY(process->waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(process->canReadLine(), 10000);
        QCOMPARE(process->readLine().trimmed(), QByteArray("ready"));
    }
    for (QProcess* process : {&first, &second})
        QCOMPARE(process->write("go\n"), qint64(3));
    for (QProcess* process : {&first, &second}) {
        QVERIFY(process->waitForFinished(10000));
        QCOMPARE(process->exitStatus(), QProcess::NormalExit);
        QCOMPARE(process->exitCode(), 0);
    }
    const QString a = QString::fromUtf8(first.readAllStandardOutput()).trimmed();
    const QString b = QString::fromUtf8(second.readAllStandardOutput()).trimmed();
    QVERIFY(a != b);
    QCOMPARE(QImage(a), solidImage(Qt::red));
    QCOMPARE(QImage(b), solidImage(Qt::blue));
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).size(), 2);
}

void tst_ImageSaveUtils::testUniqueCounterAndUuidCollisions()
{
    for (const QString& templ : {QString("same.png"), QString("same_{#}.png")}) {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ImageSaveUtils::UniqueSaveSpec spec{dir.path(), templ, {}};
        spec.context.outputDir = dir.path();
        const QString initial = FilenameTemplateEngine::renderFilename(templ, spec.context).filename;
        for (int i = 0; i <= 100; ++i)
            writeSentinel(dir.filePath(FilenameTemplateEngine::collisionFilename(templ, spec.context, initial, i)));
        int attempts = 0;
        QString temporaryPath;
        auto publish = [&](const QString& source, const QString& destination) {
            if (temporaryPath.isEmpty()) temporaryPath = source;
            if (source != temporaryPath || QImage(source) != solidImage(Qt::red))
                return SnapTray::FilePublishResult{SnapTray::FilePublishStatus::Failed, "incomplete or re-encoded source"};
            ++attempts;
            return SnapTray::publishFileNoReplace(source, destination);
        };
        auto saved = ImageSaveUtilsTestAccess::save(solidImage(Qt::red), spec, publish, "fixed");
        QVERIFY2(saved.success, qPrintable(saved.error.message));
        QCOMPARE(attempts, 102);
        QVERIFY(saved.filePath.endsWith("same_fixed.png"));
        QVERIFY(!QFile::exists(temporaryPath));
        saved = ImageSaveUtilsTestAccess::save(solidImage(Qt::red), spec, {}, "fixed");
        QVERIFY(!saved.success);
        QCOMPARE(saved.error.stage, QString("commit"));
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).size(), 102);
        QFile sentinel(dir.filePath(initial));
        QVERIFY(sentinel.open(QIODevice::ReadOnly));
        QCOMPARE(sentinel.readAll(), QByteArray("sentinel"));
    }
}

void tst_ImageSaveUtils::testUniquePreservesSymlink()
{
#ifdef Q_OS_WIN
    QSKIP("Native Windows symlink creation requires developer mode or elevated privileges");
#else
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString target = dir.filePath("missing-target");
    const QString link = dir.filePath("same.png");
    QCOMPARE(::symlink(QFile::encodeName(target).constData(), QFile::encodeName(link).constData()), 0);
    const auto saved = ImageSaveUtils::saveImageUnique(solidImage(Qt::red), {dir.path(), "same.png", {}});
    QVERIFY(saved.success);
    QVERIFY(saved.filePath.endsWith("same_1.png"));
    QVERIFY(QFileInfo(link).isSymLink());
    QVERIFY(!QFile::exists(target));
#endif
}

void tst_ImageSaveUtils::testUniqueFailureCleansTemporaryFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeSentinel(dir.filePath("existing.png"));
    const auto saved = ImageSaveUtilsTestAccess::save(solidImage(Qt::red), {dir.path(), "new.png", {}},
        [](const QString&, const QString&) {
            return SnapTray::FilePublishResult{SnapTray::FilePublishStatus::Failed, "unsupported publication"};
        });
    QVERIFY(!saved.success);
    QCOMPARE(saved.error.stage, QString("commit"));
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden), QStringList{"existing.png"});
}

void tst_ImageSaveUtils::testUniqueEncodingFailure()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    if (!QImageWriter::supportedImageFormats().contains("jpeg"))
        QSKIP("JPEG writer unavailable");
    QImage tooWide(65536, 1, QImage::Format_RGB32);
    tooWide.fill(Qt::red);
    const auto saved = ImageSaveUtils::saveImageUnique(tooWide, {dir.path(), "new.jpg", {}}, "JPEG");
    QVERIFY(!saved.success);
    QCOMPARE(saved.error.stage, QString("write"));
    QVERIFY(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
}

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

void tst_ImageSaveUtils::testSaveImageWithColorSpacePreservesProfile()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QImage image(20, 20, QImage::Format_ARGB32);
    image.fill(QColor(64, 128, 255, 255));

    QColorSpace sourceColorSpace = QColorSpace(QColorSpace::DisplayP3);
    if (!sourceColorSpace.isValid()) {
        sourceColorSpace = QColorSpace(QColorSpace::SRgb);
    }
    QVERIFY(sourceColorSpace.isValid());
    image.setColorSpace(sourceColorSpace);

    const QString filePath = tempDir.filePath("profile_preserve.png");
    ImageSaveUtils::Error error;
    QVERIFY(ImageSaveUtils::saveImageAtomically(image, filePath, QByteArrayLiteral("PNG"), &error));

    QImage loaded(filePath);
    QVERIFY(!loaded.isNull());
    QVERIFY(loaded.colorSpace().isValid());

    const QByteArray sourceIcc = sourceColorSpace.iccProfile();
    if (!sourceIcc.isEmpty()) {
        QCOMPARE(loaded.colorSpace().iccProfile(), sourceIcc);
    } else {
        QCOMPARE(loaded.colorSpace(), sourceColorSpace);
    }
}

#ifdef Q_OS_MAC
void tst_ImageSaveUtils::testPixmapRoundTripPreservesTaggedColorSpace()
{
    QColorSpace sourceColorSpace = QColorSpace(QColorSpace::DisplayP3);
    if (!sourceColorSpace.isValid()) {
        sourceColorSpace = QColorSpace(QColorSpace::SRgb);
    }
    QVERIFY(sourceColorSpace.isValid());

    QImage taggedImage(18, 18, QImage::Format_ARGB32);
    taggedImage.fill(QColor(80, 160, 255, 255));
    taggedImage.setColorSpace(sourceColorSpace);

    const QPixmap pixmap = QPixmap::fromImage(
        std::move(taggedImage), Qt::NoOpaqueDetection);
    QVERIFY(!pixmap.isNull());

    const QImage roundTrip = pixmap.toImage();
    QVERIFY(roundTrip.colorSpace().isValid());
    QCOMPARE(roundTrip.pixelColor(0, 0), QColor(80, 160, 255, 255));

    const QByteArray sourceIcc = sourceColorSpace.iccProfile();
    if (!sourceIcc.isEmpty()) {
        QCOMPARE(roundTrip.colorSpace().iccProfile(), sourceIcc);
    } else {
        QCOMPARE(roundTrip.colorSpace(), sourceColorSpace);
    }
}

void tst_ImageSaveUtils::testSavePixmapPreservesTaggedColorSpace()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QColorSpace sourceColorSpace = QColorSpace(QColorSpace::DisplayP3);
    if (!sourceColorSpace.isValid()) {
        sourceColorSpace = QColorSpace(QColorSpace::SRgb);
    }
    QVERIFY(sourceColorSpace.isValid());

    QImage taggedImage(20, 20, QImage::Format_ARGB32);
    taggedImage.fill(QColor(255, 120, 80, 255));
    taggedImage.setColorSpace(sourceColorSpace);

    const QPixmap pixmap = QPixmap::fromImage(taggedImage);
    QVERIFY(!pixmap.isNull());

    const QString filePath = tempDir.filePath("tagged_pixmap.png");
    ImageSaveUtils::Error error;
    QVERIFY(ImageSaveUtils::savePixmapAtomically(pixmap, filePath, QByteArrayLiteral("PNG"), &error));

    QImage loaded(filePath);
    QVERIFY(!loaded.isNull());
    QVERIFY(loaded.colorSpace().isValid());

    const QByteArray sourceIcc = sourceColorSpace.iccProfile();
    if (!sourceIcc.isEmpty()) {
        QCOMPARE(loaded.colorSpace().iccProfile(), sourceIcc);
    } else {
        QCOMPARE(loaded.colorSpace(), sourceColorSpace);
    }
}

void tst_ImageSaveUtils::testSavePixmapAppliesScreenColorSpaceWhenMissing()
{
    QScreen* sourceScreen = QGuiApplication::primaryScreen();
    if (!sourceScreen) {
        QSKIP("No primary screen available");
    }

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QImage untaggedImage(20, 20, QImage::Format_ARGB32);
    untaggedImage.fill(QColor(255, 80, 40, 255));
    untaggedImage.setColorSpace(QColorSpace());
    QVERIFY(!untaggedImage.colorSpace().isValid());

    const QPixmap pixmap = QPixmap::fromImage(untaggedImage);
    QVERIFY(!pixmap.isNull());

    const QString filePath = tempDir.filePath("screen_tagged.png");
    ImageSaveUtils::Error error;
    QVERIFY(ImageSaveUtils::savePixmapAtomically(
        pixmap, filePath, QByteArrayLiteral("PNG"), &error, sourceScreen));

    QImage loaded(filePath);
    QVERIFY(!loaded.isNull());
    QVERIFY(loaded.colorSpace().isValid());
}
#endif

QTEST_MAIN(tst_ImageSaveUtils)
#include "tst_ImageSaveUtils.moc"
