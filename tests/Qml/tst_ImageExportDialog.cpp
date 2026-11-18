#include <QtTest>

#include <QImageReader>
#include <QPainter>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>

#include <atomic>
#include <condition_variable>
#include <mutex>

#include "qml/ImageExportDialogController.h"
#include "qml/ImageExportViewModel.h"
#include "settings/FileSettingsManager.h"
#include "settings/Settings.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

using SnapTray::ImageExportDialogController;
using SnapTray::ImageExportViewModel;
using Resize = ImageExportOptions::Resize;
using Format = ImageExportOptions::Format;

namespace {

QImage sampleImage(const QSize& size = QSize(800, 600))
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    QLinearGradient gradient(0, 0, size.width(), size.height());
    gradient.setColorAt(0, QColor(20, 120, 220));
    gradient.setColorAt(1, QColor(250, 200, 60));
    painter.fillRect(QRect(0, 0, size.width(), size.height() / 2), gradient);
    painter.setPen(Qt::black);
    for (int x = 0; x < size.width(); x += 9) {
        painter.drawLine(x, size.height() / 2, x + 4, size.height());
    }
    return image;
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// An encoder that waits until the test lets each call through.
struct GatedEncoder
{
    std::mutex mutex;
    std::condition_variable condition;
    int released = 0;
    int entered = 0;
    std::atomic<int> concurrent{0};
    std::atomic<int> maxConcurrent{0};

    ImageExport::Encoded operator()(const QImage& image, const ImageExportOptions& options)
    {
        const int now = ++concurrent;
        int seen = maxConcurrent.load();
        while (now > seen && !maxConcurrent.compare_exchange_weak(seen, now)) {}
        {
            std::unique_lock<std::mutex> lock(mutex);
            const int ticket = ++entered;
            condition.notify_all();
            condition.wait(lock, [&] { return released >= ticket; });
        }
        const ImageExport::Encoded encoded = ImageExport::encode(image, options);
        --concurrent;
        return encoded;
    }

    void releaseOne()
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++released;
        condition.notify_all();
    }

    void releaseAll()
    {
        std::lock_guard<std::mutex> lock(mutex);
        released = 1 << 20;
        condition.notify_all();
    }

    int enteredCount()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return entered;
    }
};

} // namespace

class tst_ImageExportDialog : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void estimateIsTheRealEncodedSize();
    void optionsUpdateOutputSizeAtOnce();
    void longEdgeIsLimitedToTheSource();
    void invalidValuesAreIgnoredOrClamped();
    void noteExplainsLossyAndTransparency();
    void staleResultIsDropped();
    void rapidChangesEncodeOneAtATime();
    void destroyingWhileEncodingIsSafe();
    void encodeFailureIsShownAndBlocksSave();

    void exportWritesExactlyTheShownCandidate();
    void exportKeepsCandidateChosenBeforeTheFileDialog();
    void exportAddsTheFormatSuffix();
    void exportRefusesToReplaceAnUnconfirmedFile();
    void cancelledFileDialogWritesNothing();
    void failedExportStaysOpenAndCanBeRetried();
    void cancelClosesWithoutWriting();
    void exportRemembersOptions();
    void reopeningReplacesTheImage();
    void nullImageIsRefused();

    void dialogControlsFollowTheViewModel();

private:
    struct SavedSetting { QString key; bool existed = false; QVariant value; };
    QList<SavedSetting> m_savedSettings;
    QTemporaryDir m_dir;
};

void tst_ImageExportDialog::init()
{
    static const QStringList keys = {
        QStringLiteral("files/imageExport/resize"),
        QStringLiteral("files/imageExport/percent"),
        QStringLiteral("files/imageExport/longEdge"),
        QStringLiteral("files/imageExport/format"),
        QStringLiteral("files/imageExport/jpegQuality"),
        QStringLiteral("files/useLastScreenshotSaveLocation"),
        QStringLiteral("files/lastScreenshotSaveDirectory"),
        QStringLiteral("files/filenameTemplate"),
    };
    auto settings = SnapTray::getSettings();
    m_savedSettings.clear();
    for (const QString& key : keys) {
        m_savedSettings.append({key, settings.contains(key), settings.value(key)});
        settings.remove(key);
    }
    settings.setValue(QStringLiteral("files/filenameTemplate"),
                      FileSettingsManager::defaultFilenameTemplate());
    settings.sync();
    QVERIFY(m_dir.isValid());
}

void tst_ImageExportDialog::cleanup()
{
    auto settings = SnapTray::getSettings();
    for (const SavedSetting& saved : std::as_const(m_savedSettings)) {
        if (saved.existed) {
            settings.setValue(saved.key, saved.value);
        } else {
            settings.remove(saved.key);
        }
    }
    settings.sync();
    const QDir dir(m_dir.path());
    for (const QString& name : dir.entryList(QDir::Files | QDir::Hidden)) {
        QFile::remove(dir.filePath(name));
    }
}

// ---- view model ----

void tst_ImageExportDialog::estimateIsTheRealEncodedSize()
{
    const QImage source = sampleImage();
    ImageExportOptions options;
    options.format = Format::Jpeg;
    options.jpegQuality = 60;
    options.resize = Resize::Percent;
    options.percent = 50;
    ImageExportViewModel vm(source, options);

    // Until the encode is done there is no size and nothing to save.
    QVERIFY(vm.estimating());
    QVERIFY(!vm.canSave());
    QVERIFY(vm.fileSizeText().contains(QStringLiteral("Calculating")));

    QTRY_VERIFY(vm.canSave());
    QVERIFY(!vm.estimating());
    const ImageExport::Encoded candidate = *vm.candidate();
    QCOMPARE(candidate.options, vm.options());
    QCOMPARE(candidate.options.jpegQuality, 60);
    QCOMPARE(candidate.pixelSize, QSize(400, 300));
    QCOMPARE(candidate.data, ImageExport::encode(source, options).data);
    QVERIFY(vm.fileSizeText().contains(QLocale().toString(qint64(candidate.data.size()))));
}

void tst_ImageExportDialog::optionsUpdateOutputSizeAtOnce()
{
    ImageExportViewModel vm(sampleImage(QSize(2560, 1440)), ImageExportOptions());
    QCOMPARE(vm.sourceSizeText(), QStringLiteral("2560 \u00D7 1440 px"));
    QCOMPARE(vm.outputSizeText(), QStringLiteral("2560 \u00D7 1440 px"));

    QSignalSpy changed(&vm, &ImageExportViewModel::optionsChanged);
    vm.setResizeMode(int(Resize::Percent));
    vm.setPercent(25);
    QCOMPARE(vm.outputSizeText(), QStringLiteral("640 \u00D7 360 px"));
    vm.setResizeMode(int(Resize::LongEdge));
    vm.setLongEdge(1280);
    QCOMPARE(vm.outputSizeText(), QStringLiteral("1280 \u00D7 720 px"));
    QCOMPARE(changed.count(), 4);
    vm.setLongEdge(1280);   // unchanged
    QCOMPARE(changed.count(), 4);

    QTRY_VERIFY(vm.canSave());
    QCOMPARE(vm.candidate()->pixelSize, QSize(1280, 720));
}

void tst_ImageExportDialog::longEdgeIsLimitedToTheSource()
{
    ImageExportOptions stored;
    stored.resize = Resize::LongEdge;
    stored.longEdge = 8000;
    ImageExportViewModel vm(sampleImage(QSize(1200, 300)), stored);
    QCOMPARE(vm.maxLongEdge(), 1200);
    QCOMPARE(vm.longEdge(), 1200);
    QCOMPARE(vm.outputSizeText(), QStringLiteral("1200 \u00D7 300 px"));
    // Only the display is limited: the preference for larger images stays.
    QCOMPARE(vm.options().longEdge, 8000);
    vm.setLongEdge(100000);
    QCOMPARE(vm.longEdge(), 1200);
    QCOMPARE(vm.outputSizeText(), QStringLiteral("1200 \u00D7 300 px"));
    vm.setLongEdge(600);
    QCOMPARE(vm.outputSizeText(), QStringLiteral("600 \u00D7 150 px"));
}

void tst_ImageExportDialog::invalidValuesAreIgnoredOrClamped()
{
    ImageExportViewModel vm(sampleImage(), ImageExportOptions());
    vm.setResizeMode(17);
    QCOMPARE(vm.resizeMode(), int(Resize::Original));
    vm.setFormat(9);
    QCOMPARE(vm.format(), int(Format::Png));
    vm.setJpegQuality(500);
    QCOMPARE(vm.jpegQuality(), ImageExportOptions::kMaxJpegQuality);
    vm.setPercent(-20);
    QCOMPARE(vm.percent(), ImageExportOptions::kMinPercent);
    vm.setResizeMode(int(Resize::Percent));
    QVERIFY(vm.outputPixelSize().width() >= 1);
}

void tst_ImageExportDialog::noteExplainsLossyAndTransparency()
{
    QImage source = sampleImage(QSize(200, 100));
    source.setPixelColor(3, 3, QColor(0, 0, 0, 0));
    ImageExportViewModel vm(source, ImageExportOptions());
    QVERIFY(!vm.lossy());
    QVERIFY(vm.noteText().contains(QStringLiteral("lossless")));

    vm.setFormat(int(Format::Jpeg));
    QVERIFY(vm.lossy());
    // At once, not only when the encode is done: never "lossless" for JPEG.
    QVERIFY(vm.estimating());
    QVERIFY(vm.noteText().contains(QStringLiteral("lossy")));
    QVERIFY(!vm.noteText().contains(QStringLiteral("lossless")));
    QTRY_VERIFY(vm.canSave());
    QVERIFY(vm.candidate()->transparencyDropped);
    QVERIFY(vm.noteText().contains(QStringLiteral("transparent")));
    QVERIFY(vm.noteText().contains(QStringLiteral("white")));

    ImageExportViewModel opaque(sampleImage(QSize(200, 100)), vm.options());
    QTRY_VERIFY(opaque.canSave());
    QVERIFY(!opaque.noteText().contains(QStringLiteral("transparent")));
}

void tst_ImageExportDialog::staleResultIsDropped()
{
    auto gate = std::make_shared<GatedEncoder>();
    const QImage source = sampleImage();
    ImageExportOptions first;
    first.format = Format::Jpeg;
    first.jpegQuality = 20;
    ImageExportViewModel vm(source, first);
    vm.setEncoderForTesting([gate](const QImage& image, const ImageExportOptions& options) {
        return (*gate)(image, options);
    });
    QTRY_VERIFY(gate->enteredCount() >= 1);   // quality 20 is being encoded

    // The user moves on while that encode is still running.
    vm.setJpegQuality(95);
    QSignalSpy estimates(&vm, &ImageExportViewModel::estimateChanged);
    gate->releaseOne();

    // The quality-20 result must not become the candidate for quality 95.
    QTRY_VERIFY(gate->enteredCount() >= 2);
    QVERIFY(!vm.canSave());
    QVERIFY(vm.estimating());

    gate->releaseOne();
    QTRY_VERIFY(vm.canSave());
    QCOMPARE(vm.candidate()->options.jpegQuality, 95);
    QCOMPARE(vm.candidate()->data, ImageExport::encode(source, vm.options()).data);
    gate->releaseAll();
}

void tst_ImageExportDialog::rapidChangesEncodeOneAtATime()
{
    auto gate = std::make_shared<GatedEncoder>();
    ImageExportOptions options;
    options.format = Format::Jpeg;
    ImageExportViewModel vm(sampleImage(), options);
    vm.setEncoderForTesting([gate](const QImage& image, const ImageExportOptions& o) {
        return (*gate)(image, o);
    });
    QTRY_VERIFY(gate->enteredCount() >= 1);

    // A slider dragged across its range while the first encode is held up.
    for (int quality = 1; quality <= 100; ++quality) {
        vm.setJpegQuality(quality);
        QCoreApplication::processEvents();
    }
    QTest::qWait(300);   // well past the debounce
    QCOMPARE(vm.encodeRunsStarted(), 1);
    QCOMPARE(gate->maxConcurrent.load(), 1);

    gate->releaseAll();
    QTRY_VERIFY(vm.canSave());
    // One held run, then one for the final value: not one per change.
    QCOMPARE(vm.encodeRunsStarted(), 2);
    QCOMPARE(gate->maxConcurrent.load(), 1);
    QCOMPARE(vm.candidate()->options.jpegQuality, 100);
}

void tst_ImageExportDialog::destroyingWhileEncodingIsSafe()
{
    auto gate = std::make_shared<GatedEncoder>();
    {
        ImageExportViewModel vm(sampleImage(), ImageExportOptions());
        vm.setEncoderForTesting([gate](const QImage& image, const ImageExportOptions& o) {
            return (*gate)(image, o);
        });
        QTRY_VERIFY(gate->enteredCount() >= 1);
    }   // closed mid-encode
    gate->releaseAll();
    QTRY_COMPARE(gate->concurrent.load(), 0);
    QTest::qWait(50);   // a late result has nobody to deliver to
}

void tst_ImageExportDialog::encodeFailureIsShownAndBlocksSave()
{
    ImageExportViewModel vm(sampleImage(), ImageExportOptions());
    vm.setEncoderForTesting([](const QImage&, const ImageExportOptions& options) {
        ImageExport::Encoded failed;
        failed.options = options;
        failed.error = QStringLiteral("disk on fire");
        return failed;
    });
    QTRY_VERIFY(!vm.estimating());
    QVERIFY(!vm.canSave());
    QVERIFY(vm.fileSizeText().contains(QStringLiteral("disk on fire")));
    QSignalSpy save(&vm, &ImageExportViewModel::saveRequested);
    vm.save();
    QCOMPARE(save.count(), 0);

    // Changing an option tries again.
    vm.setEncoderForTesting({});
    QTRY_VERIFY(vm.canSave());
}

// ---- controller ----

void tst_ImageExportDialog::exportWritesExactlyTheShownCandidate()
{
    const QImage source = sampleImage();
    const QImage sourceBefore = source.copy();
    ImageExportDialogController controller;
    controller.setShowDialogForTesting(false);
    QString suggested;
    QString filter;
    const QString target = m_dir.filePath("out.jpg");
    controller.setSaveFileDialogForTesting(
        [&](const QString&, const QString& defaultPath, const QString& nameFilter) {
            suggested = defaultPath;
            filter = nameFilter;
            return target;
        });
    QSignalSpy exported(&controller, &ImageExportDialogController::exported);
    QSignalSpy closed(&controller, &ImageExportDialogController::closed);

    ImageExportDialogController::Request request;
    request.image = source;
    request.nameContext.type = QStringLiteral("Screenshot");
    QVERIFY(controller.open(request));
    ImageExportViewModel* vm = controller.viewModel();
    QVERIFY(vm);
    vm->setFormat(int(Format::Jpeg));
    vm->setJpegQuality(40);
    vm->setResizeMode(int(Resize::LongEdge));
    vm->setLongEdge(400);
    QTRY_VERIFY(vm->canSave());
    const ImageExport::Encoded shown = *vm->candidate();

    vm->save();
    QCOMPARE(exported.count(), 1);
    QCOMPARE(exported.first().at(0).toString(), target);
    QCOMPARE(exported.first().at(1).toSize(), QSize(400, 300));
    QCOMPARE(exported.first().at(2).toLongLong(), qint64(shown.data.size()));
    QCOMPARE(closed.count(), 1);
    QVERIFY(!controller.isOpen());

    // The file is byte for byte what the dialog measured.
    QCOMPARE(readFile(target), shown.data);
    QCOMPARE(QFileInfo(target).size(), qint64(shown.data.size()));
    QImageReader reader(target);
    QCOMPARE(reader.format(), QByteArray("jpeg"));
    QCOMPARE(reader.size(), QSize(400, 300));

    // The suggested name carries the output's pixel size and extension.
    QVERIFY2(suggested.endsWith(QStringLiteral(".jpg")), qPrintable(suggested));
    QVERIFY2(QFileInfo(suggested).fileName().contains(QStringLiteral("400x300")), qPrintable(suggested));
    QVERIFY(filter.contains(QStringLiteral("*.jpg")));
    QVERIFY(!filter.contains(QStringLiteral("*.png")));

    // The caller's image is as it was.
    QCOMPARE(source, sourceBefore);
    QCOMPARE(source.size(), QSize(800, 600));
}

void tst_ImageExportDialog::exportKeepsCandidateChosenBeforeTheFileDialog()
{
    ImageExportDialogController controller;
    controller.setShowDialogForTesting(false);
    const QString target = m_dir.filePath("held.png");
    ImageExportViewModel* vm = nullptr;
    controller.setSaveFileDialogForTesting([&](const QString&, const QString&, const QString&) {
        // The options change while the file dialog is still open.
        vm->setFormat(int(Format::Jpeg));
        vm->setResizeMode(int(Resize::Percent));
        vm->setPercent(10);
        return target;
    });
    ImageExportDialogController::Request request;
    request.image = sampleImage();
    QVERIFY(controller.open(request));
    vm = controller.viewModel();
    QTRY_VERIFY(vm->canSave());
    const QByteArray shown = vm->candidate()->data;

    vm->save();
    // What was confirmed is what is written: a full-size PNG in a .png file.
    QCOMPARE(readFile(target), shown);
    QCOMPARE(QImageReader(target).format(), QByteArray("png"));
    QCOMPARE(QImageReader(target).size(), QSize(800, 600));
}

void tst_ImageExportDialog::exportAddsTheFormatSuffix()
{
    ImageExportOptions jpeg;
    jpeg.format = Format::Jpeg;
    ImageExportOptions png;
    QCOMPARE(ImageExportDialogController::pathWithFormatSuffix("/tmp/a.jpg", jpeg), QStringLiteral("/tmp/a.jpg"));
    QCOMPARE(ImageExportDialogController::pathWithFormatSuffix("/tmp/a.JPEG", jpeg), QStringLiteral("/tmp/a.JPEG"));
    QCOMPARE(ImageExportDialogController::pathWithFormatSuffix("/tmp/a", jpeg), QStringLiteral("/tmp/a.jpg"));
    QCOMPARE(ImageExportDialogController::pathWithFormatSuffix("/tmp/a.png", jpeg), QStringLiteral("/tmp/a.png.jpg"));
    QCOMPARE(ImageExportDialogController::pathWithFormatSuffix("/tmp/a.jpg", png), QStringLiteral("/tmp/a.jpg.png"));
    QCOMPARE(ImageExportDialogController::pathWithFormatSuffix(QString(), png), QString());

    // JPEG data never lands in a file named .png.
    ImageExportDialogController controller;
    controller.setShowDialogForTesting(false);
    controller.setSaveFileDialogForTesting([&](const QString&, const QString&, const QString&) {
        return m_dir.filePath("photo.png");
    });
    QSignalSpy exported(&controller, &ImageExportDialogController::exported);
    ImageExportDialogController::Request request;
    request.image = sampleImage();
    QVERIFY(controller.open(request));
    controller.viewModel()->setFormat(int(Format::Jpeg));
    QTRY_VERIFY(controller.viewModel()->canSave());
    controller.viewModel()->save();
    QCOMPARE(exported.count(), 1);
    QCOMPARE(exported.first().at(0).toString(), m_dir.filePath("photo.png.jpg"));
    QVERIFY(!QFileInfo::exists(m_dir.filePath("photo.png")));
    QCOMPARE(QImageReader(m_dir.filePath("photo.png.jpg")).format(), QByteArray("jpeg"));
}

void tst_ImageExportDialog::exportRefusesToReplaceAnUnconfirmedFile()
{
    // The user typed "shot"; "shot.png" exists but was never confirmed.
    const QString existing = m_dir.filePath("shot.png");
    QFile file(existing);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("precious");
    file.close();

    ImageExportDialogController controller;
    controller.setShowDialogForTesting(false);
    controller.setSaveFileDialogForTesting([&](const QString&, const QString&, const QString&) {
        return m_dir.filePath("shot");
    });
    QSignalSpy exported(&controller, &ImageExportDialogController::exported);
    ImageExportDialogController::Request request;
    request.image = sampleImage();
    QVERIFY(controller.open(request));
    QTRY_VERIFY(controller.viewModel()->canSave());
    controller.viewModel()->save();

    QCOMPARE(exported.count(), 0);
    QCOMPARE(readFile(existing), QByteArray("precious"));
    QVERIFY(controller.isOpen());
    QVERIFY(controller.viewModel()->errorText().contains(QStringLiteral("shot.png")));
}

void tst_ImageExportDialog::cancelledFileDialogWritesNothing()
{
    ImageExportDialogController controller;
    controller.setShowDialogForTesting(false);
    int asked = 0;
    controller.setSaveFileDialogForTesting([&](const QString&, const QString&, const QString&) {
        ++asked;
        return QString();
    });
    QSignalSpy exported(&controller, &ImageExportDialogController::exported);
    ImageExportDialogController::Request request;
    request.image = sampleImage();
    QVERIFY(controller.open(request));
    QTRY_VERIFY(controller.viewModel()->canSave());
    controller.viewModel()->save();

    QCOMPARE(asked, 1);
    QCOMPARE(exported.count(), 0);
    QVERIFY(controller.isOpen());   // still there to try again
    QVERIFY(QDir(m_dir.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
    // Options are only remembered after a successful export.
    QCOMPARE(FileSettingsManager::instance().loadImageExportOptions(), ImageExportOptions());
}

void tst_ImageExportDialog::failedExportStaysOpenAndCanBeRetried()
{
    ImageExportDialogController controller;
    controller.setShowDialogForTesting(false);
    QString target = m_dir.filePath("no-such-folder/out.png");
    controller.setSaveFileDialogForTesting([&](const QString&, const QString&, const QString&) {
        return target;
    });
    QSignalSpy exported(&controller, &ImageExportDialogController::exported);
    ImageExportDialogController::Request request;
    request.image = sampleImage();
    QVERIFY(controller.open(request));
    ImageExportViewModel* vm = controller.viewModel();
    QTRY_VERIFY(vm->canSave());
    const QByteArray shown = vm->candidate()->data;

    vm->save();
    QCOMPARE(exported.count(), 0);
    QVERIFY(controller.isOpen());
    QVERIFY(!vm->errorText().isEmpty());
    QVERIFY(vm->canSave());   // the candidate is still there
    QVERIFY(QDir(m_dir.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());

    // Same dialog, a folder that exists: the same bytes go out.
    target = m_dir.filePath("retry.png");
    vm->save();
    QCOMPARE(exported.count(), 1);
    QCOMPARE(readFile(target), shown);
    QCOMPARE(QDir(m_dir.path()).entryList(QDir::Files | QDir::Hidden), QStringList{QStringLiteral("retry.png")});
}

void tst_ImageExportDialog::cancelClosesWithoutWriting()
{
    ImageExportDialogController controller;
    controller.setShowDialogForTesting(false);
    int asked = 0;
    controller.setSaveFileDialogForTesting([&](const QString&, const QString&, const QString&) {
        ++asked;
        return m_dir.filePath("never.png");
    });
    QSignalSpy closed(&controller, &ImageExportDialogController::closed);
    ImageExportDialogController::Request request;
    request.image = sampleImage();
    QVERIFY(controller.open(request));
    controller.viewModel()->setFormat(int(Format::Jpeg));
    controller.viewModel()->cancel();

    QCOMPARE(closed.count(), 1);
    QVERIFY(!controller.isOpen());
    QCOMPARE(asked, 0);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTest::qWait(200);   // an encode still in flight has nowhere to land
    QVERIFY(QDir(m_dir.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
    QCOMPARE(FileSettingsManager::instance().loadImageExportOptions(), ImageExportOptions());
}

void tst_ImageExportDialog::exportRemembersOptions()
{
    ImageExportDialogController controller;
    controller.setShowDialogForTesting(false);
    controller.setSaveFileDialogForTesting([&](const QString&, const QString&, const QString&) {
        return m_dir.filePath("remember.jpg");
    });
    ImageExportDialogController::Request request;
    request.image = sampleImage();
    QVERIFY(controller.open(request));
    ImageExportViewModel* vm = controller.viewModel();
    vm->setFormat(int(Format::Jpeg));
    vm->setJpegQuality(77);
    vm->setResizeMode(int(Resize::Percent));
    vm->setPercent(40);
    const ImageExportOptions chosen = vm->options();
    QTRY_VERIFY(vm->canSave());
    vm->save();
    QCOMPARE(FileSettingsManager::instance().loadImageExportOptions(), chosen);

    // The next dialog starts from them.
    QVERIFY(controller.open(request));
    QCOMPARE(controller.viewModel()->options(), chosen);
    controller.close();

    // Exporting an image smaller than the remembered long edge does not
    // lower it for the next, larger one.
    ImageExportOptions wide = chosen;
    wide.resize = Resize::LongEdge;
    wide.longEdge = 1920;
    FileSettingsManager::instance().saveImageExportOptions(wide);
    request.image = sampleImage(QSize(400, 300));
    QVERIFY(controller.open(request));
    QCOMPARE(controller.viewModel()->longEdge(), 400);
    QTRY_VERIFY(controller.viewModel()->canSave());
    controller.viewModel()->save();
    QCOMPARE(FileSettingsManager::instance().loadImageExportOptions().longEdge, 1920);
}

void tst_ImageExportDialog::reopeningReplacesTheImage()
{
    ImageExportDialogController controller;
    controller.setShowDialogForTesting(false);
    QSignalSpy closed(&controller, &ImageExportDialogController::closed);
    ImageExportDialogController::Request first;
    first.image = sampleImage(QSize(400, 200));
    ImageExportDialogController::Request second;
    second.image = sampleImage(QSize(100, 300));
    QVERIFY(controller.open(first));
    const QPointer<ImageExportViewModel> firstVm = controller.viewModel();
    QVERIFY(controller.open(second));
    QCOMPARE(closed.count(), 1);
    QVERIFY(controller.viewModel() != firstVm.data());
    QCOMPARE(controller.viewModel()->sourceSizeText(), QStringLiteral("100 \u00D7 300 px"));
    controller.close();
    QCOMPARE(closed.count(), 2);
}

void tst_ImageExportDialog::nullImageIsRefused()
{
    ImageExportDialogController controller;
    controller.setShowDialogForTesting(false);
    QVERIFY(!controller.open(ImageExportDialogController::Request()));
    QVERIFY(!controller.isOpen());
}

// ---- QML ----

void tst_ImageExportDialog::dialogControlsFollowTheViewModel()
{
    ImageExportViewModel vm(sampleImage(QSize(1600, 900)), ImageExportOptions());
    QQuickView view;
    view.setInitialProperties({{"viewModel", QVariant::fromValue(&vm)}});
    view.setSource(QUrl("qrc:/SnapTrayQml/dialogs/ImageExportDialog.qml"));
    QCOMPARE(view.status(), QQuickView::Ready);
    QQuickItem* root = view.rootObject();
    QVERIFY(root);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    auto item = [root](const char* name) { return root->findChild<QQuickItem*>(name); };
    QQuickItem* resizeMode = item("exportResizeMode");
    QQuickItem* percent = item("exportPercent");
    QQuickItem* longEdge = item("exportLongEdge");
    QQuickItem* format = item("exportFormat");
    QQuickItem* quality = item("exportJpegQuality");
    QQuickItem* outputSize = item("exportOutputSize");
    QQuickItem* fileSize = item("exportFileSize");
    QQuickItem* save = item("exportSave");
    QQuickItem* error = item("exportError");
    QVERIFY(resizeMode && percent && longEdge && format && quality && outputSize && fileSize && save && error);

    // Original size, PNG: no size value and no lossy quality control.
    QVERIFY(!percent->isVisible());
    QVERIFY(!longEdge->isVisible());
    QVERIFY2(!quality->isVisible(), "PNG must not offer a lossy quality control");
    QVERIFY(!error->isVisible());
    QVERIFY(outputSize->property("text").toString().contains(QStringLiteral("1600")));

    QVERIFY(QMetaObject::invokeMethod(resizeMode, "activated", Q_ARG(int, int(Resize::Percent))));
    QCOMPARE(vm.resizeMode(), int(Resize::Percent));
    QTRY_VERIFY(percent->isVisible());
    QVERIFY(!longEdge->isVisible());
    QVERIFY(QMetaObject::invokeMethod(percent, "moved", Q_ARG(int, 25)));
    QCOMPARE(vm.percent(), 25);
    QTRY_VERIFY(outputSize->property("text").toString().contains(QStringLiteral("400")));

    QVERIFY(QMetaObject::invokeMethod(resizeMode, "activated", Q_ARG(int, int(Resize::LongEdge))));
    QTRY_VERIFY(longEdge->isVisible());
    QVERIFY(!percent->isVisible());
    QCOMPARE(longEdge->property("to").toInt(), 1600);

    QVERIFY(QMetaObject::invokeMethod(format, "activated", Q_ARG(int, int(Format::Jpeg))));
    QCOMPARE(vm.format(), int(Format::Jpeg));
    QTRY_VERIFY(quality->isVisible());
    QVERIFY(QMetaObject::invokeMethod(quality, "moved", Q_ARG(int, 55)));
    QCOMPARE(vm.jpegQuality(), 55);

    // Export is offered once the real size is known, and shows it.
    QTRY_VERIFY(save->isEnabled());
    QVERIFY(fileSize->property("text").toString().contains(
        QLocale().toString(qint64(vm.candidate()->data.size()))));

    QSignalSpy saveRequested(&vm, &ImageExportViewModel::saveRequested);
    QSignalSpy cancelRequested(&vm, &ImageExportViewModel::cancelRequested);
    QVERIFY(QMetaObject::invokeMethod(save, "clicked"));
    QCOMPARE(saveRequested.count(), 1);
    vm.setErrorText(QStringLiteral("Failed to export image: no space"));
    QTRY_VERIFY(error->isVisible());
    QVERIFY(QMetaObject::invokeMethod(root, "closeRequested"));
    QCOMPARE(cancelRequested.count(), 1);

    // While a new size is being worked out, Export waits.
    vm.setJpegQuality(12);
    QVERIFY(!save->isEnabled());
    QTRY_VERIFY(save->isEnabled());

    if (qEnvironmentVariableIsSet("SNAPTRAY_EXPORT_EVIDENCE")) {
        QVERIFY(view.grabWindow().save(qEnvironmentVariable("SNAPTRAY_EXPORT_EVIDENCE")));
    }
}

QTEST_MAIN(tst_ImageExportDialog)
#include "tst_ImageExportDialog.moc"
