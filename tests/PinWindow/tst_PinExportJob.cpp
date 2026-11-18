// #353: a pin's save, Beautify save and Beautify copy are composed and written
// off the GUI thread from an immutable snapshot, one job at a time, and are
// reported only once the file is published.
#include <QtTest/QtTest>

#include <QColorSpace>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>

#include <memory>

#include "beautify/BeautifyRenderer.h"
#include "pinwindow/PinExportJob.h"

using SnapTray::PinExportRequest;
using SnapTray::PinExportResult;
using SnapTray::PinExportRunner;

namespace {

QImage solidImage(const QSize& size, const QColor& color, qreal dpr = 1.0)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(color);
    image.setDevicePixelRatio(dpr);
    return image;
}

PinExportRequest uniqueRequest(const QImage& source, const QString& dir)
{
    PinExportRequest request;
    request.source = source;
    request.destination = PinExportRequest::Destination::UniqueFile;
    request.uniqueSave.outputDir = dir;
    request.uniqueSave.filenameTemplate = QStringLiteral("same.png");
    request.uniqueSave.context.ext = QStringLiteral("png");
    request.uniqueSave.context.outputDir = dir;
    return request;
}

BeautifySettings look()
{
    BeautifySettings settings;
    settings.backgroundType = BeautifyBackgroundType::Solid;
    settings.backgroundColor = QColor(10, 20, 30);
    settings.padding = 8;
    settings.cornerRadius = 4;
    settings.shadowEnabled = false;
    return settings;
}

QByteArray fileBytes(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

class TestPinExportJob : public QObject
{
    Q_OBJECT

private slots:
    void uniqueSaveNeverReplacesAFile();
    void exactSaveFailureKeepsTheExistingFile();
    void beautifiedSaveMatchesTheRenderer();
    void colorSpaceTagFollowsTheExportRules();
    void clipboardImageIsNormalized();
    void nullSourceIsARenderFailure();

    void runnerDeliversOnItsThreadOnce();
    void runnerRejectsASecondJobWhileBusy();
    void runnerWritesTheSnapshotNotLaterChanges();
    void runnerFinishNowDeliversImmediately();
    void destroyedRunnerFinishesTheFileWithoutCallback();
};

void TestPinExportJob::uniqueSaveNeverReplacesAFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const PinExportResult first =
        SnapTray::runPinExport(uniqueRequest(solidImage({20, 20}, Qt::red), dir.path()));
    const PinExportResult second =
        SnapTray::runPinExport(uniqueRequest(solidImage({20, 20}, Qt::blue), dir.path()));

    QVERIFY(first.success);
    QVERIFY(second.success);
    QVERIFY(first.filePath != second.filePath);
    QCOMPARE(QImage(first.filePath).pixelColor(5, 5), QColor(Qt::red));
    QCOMPARE(QImage(second.filePath).pixelColor(5, 5), QColor(Qt::blue));
}

void TestPinExportJob::exactSaveFailureKeepsTheExistingFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("keep.unknownformat"));
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("original");
    }

    PinExportRequest request;
    request.source = solidImage({20, 20}, Qt::red);
    request.destination = PinExportRequest::Destination::ExactFile;
    request.filePath = path;
    const PinExportResult result = SnapTray::runPinExport(request);

    QVERIFY(!result.success);
    QVERIFY(!result.renderFailed);
    QCOMPARE(result.filePath, path);
    QVERIFY(!result.error.message.isEmpty());
    QCOMPARE(fileBytes(path), QByteArray("original"));
}

void TestPinExportJob::beautifiedSaveMatchesTheRenderer()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QImage source = solidImage({40, 30}, Qt::red, 2.0);

    PinExportRequest request;
    request.source = source;
    request.beautify = look();
    request.destination = PinExportRequest::Destination::ExactFile;
    request.filePath = dir.filePath(QStringLiteral("beautified.png"));
    const PinExportResult result = SnapTray::runPinExport(request);

    QVERIFY(result.success);
    const QImage expected = BeautifyRenderer::applyToImage(source, look());
    QCOMPARE(result.image, expected);
    QCOMPARE(result.image.devicePixelRatio(), 2.0);
    QCOMPARE(QImage(result.filePath).convertToFormat(QImage::Format_ARGB32_Premultiplied).size(),
             expected.size());
}

void TestPinExportJob::colorSpaceTagFollowsTheExportRules()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // An untagged picture takes the screen's colour space...
    PinExportRequest request;
    request.source = solidImage({8, 8}, Qt::green);
    request.screenColorSpace = QColorSpace(QColorSpace::DisplayP3);
    request.destination = PinExportRequest::Destination::ExactFile;
    request.filePath = dir.filePath(QStringLiteral("p3.png"));
    QVERIFY(SnapTray::runPinExport(request).success);
    QCOMPARE(QImage(request.filePath).colorSpace(), QColorSpace(QColorSpace::DisplayP3));

    // ...a tagged one keeps its own.
    request.source.setColorSpace(QColorSpace(QColorSpace::AdobeRgb));
    request.filePath = dir.filePath(QStringLiteral("adobe.png"));
    QVERIFY(SnapTray::runPinExport(request).success);
    QCOMPARE(QImage(request.filePath).colorSpace(), QColorSpace(QColorSpace::AdobeRgb));
}

void TestPinExportJob::clipboardImageIsNormalized()
{
    PinExportRequest request;
    request.source = solidImage({40, 30}, Qt::red, 2.0);
    request.beautify = look();
    request.screenColorSpace = QColorSpace(QColorSpace::DisplayP3);
    request.destination = PinExportRequest::Destination::Clipboard;
    const PinExportResult result = SnapTray::runPinExport(request);

    QVERIFY(result.success);
    QVERIFY(result.filePath.isEmpty());
    QCOMPARE(result.image.devicePixelRatio(), 2.0);
    QCOMPARE(result.clipboardImage.devicePixelRatio(), 1.0);
    QCOMPARE(result.clipboardImage.format(), QImage::Format_ARGB32_Premultiplied);
    QCOMPARE(result.clipboardImage.size(), result.image.size());
    QCOMPARE(result.clipboardImage.colorSpace(), QColorSpace(QColorSpace::DisplayP3));
}

void TestPinExportJob::nullSourceIsARenderFailure()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PinExportRequest request = uniqueRequest(QImage(), dir.path());
    request.beautify = look();
    const PinExportResult result = SnapTray::runPinExport(request);
    QVERIFY(!result.success);
    QVERIFY(result.renderFailed);
    QVERIFY(QDir(dir.path()).entryList(QDir::Files).isEmpty());
}

void TestPinExportJob::runnerDeliversOnItsThreadOnce()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PinExportRunner runner;
    int calls = 0;
    QThread* deliveredOn = nullptr;
    PinExportResult delivered;

    QVERIFY(runner.start(uniqueRequest(solidImage({16, 16}, Qt::red), dir.path()),
                         [&](const PinExportResult& result) {
                             ++calls;
                             deliveredOn = QThread::currentThread();
                             delivered = result;
                         }));
    // Nothing is reported before the event loop delivers the result.
    QCOMPARE(calls, 0);
    QVERIFY(runner.isBusy());

    QTRY_COMPARE(calls, 1);
    QCOMPARE(deliveredOn, QThread::currentThread());
    QVERIFY(delivered.success);
    QVERIFY(QFile::exists(delivered.filePath));
    QVERIFY(!runner.isBusy());
    QCoreApplication::processEvents();
    QCOMPARE(calls, 1);
}

void TestPinExportJob::runnerRejectsASecondJobWhileBusy()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PinExportRunner runner;
    int first = 0;
    int second = 0;

    QVERIFY(runner.start(uniqueRequest(solidImage({16, 16}, Qt::red), dir.path()),
                         [&](const PinExportResult&) { ++first; }));
    QVERIFY(!runner.start(uniqueRequest(solidImage({16, 16}, Qt::blue), dir.path()),
                          [&](const PinExportResult&) { ++second; }));

    QTRY_COMPARE(first, 1);
    QCoreApplication::processEvents();
    QCOMPARE(second, 0);
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 1);

    // Once idle, the next job runs.
    QVERIFY(runner.start(uniqueRequest(solidImage({16, 16}, Qt::blue), dir.path()),
                         [&](const PinExportResult&) { ++second; }));
    QTRY_COMPARE(second, 1);
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 2);
}

void TestPinExportJob::runnerWritesTheSnapshotNotLaterChanges()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PinExportRunner runner;
    QImage live = solidImage({16, 16}, Qt::red);
    QString path;

    QVERIFY(runner.start(uniqueRequest(live, dir.path()),
                         [&](const PinExportResult& result) { path = result.filePath; }));
    live.fill(Qt::blue); // the pin keeps being edited

    QTRY_VERIFY(!path.isEmpty());
    QCOMPARE(QImage(path).pixelColor(8, 8), QColor(Qt::red));
}

void TestPinExportJob::runnerFinishNowDeliversImmediately()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PinExportRunner runner;
    int calls = 0;
    QString path;

    QVERIFY(runner.start(uniqueRequest(solidImage({16, 16}, Qt::red), dir.path()),
                         [&](const PinExportResult& result) {
                             ++calls;
                             path = result.filePath;
                         }));
    runner.finishNow();
    QCOMPARE(calls, 1);
    QVERIFY(QFile::exists(path));
    QVERIFY(!runner.isBusy());

    // The queued notification does not report it a second time.
    QTest::qWait(50);
    QCOMPARE(calls, 1);
}

void TestPinExportJob::destroyedRunnerFinishesTheFileWithoutCallback()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    int calls = 0;
    {
        auto runner = std::make_unique<PinExportRunner>();
        QVERIFY(runner->start(uniqueRequest(solidImage({600, 600}, Qt::red), dir.path()),
                              [&](const PinExportResult&) { ++calls; }));
    }
    // The file was published before the runner went away; nothing is
    // reported to an owner that no longer exists.
    const QStringList files = QDir(dir.path()).entryList(QDir::Files);
    QCOMPARE(files.size(), 1);
    QCOMPARE(QImage(dir.filePath(files.first())).pixelColor(300, 300), QColor(Qt::red));
    QTest::qWait(50);
    QCOMPARE(calls, 0);
}

QTEST_MAIN(TestPinExportJob)
#include "tst_PinExportJob.moc"
