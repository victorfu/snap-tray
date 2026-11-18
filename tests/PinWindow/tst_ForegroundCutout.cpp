#include <QtTest/QtTest>

#include <QImage>
#include <QSemaphore>

#include <atomic>
#include <memory>

#include "pinwindow/ForegroundCutout.h"
#include "pinwindow/ForegroundCutoutRunner.h"

using SnapTray::ForegroundCutout::MaskResult;
using SnapTray::ForegroundCutout::Result;
using SnapTray::ForegroundCutout::Status;
namespace Cutout = SnapTray::ForegroundCutout;

namespace {

QImage opaqueImage(int width, int height)
{
    QImage image(width, height, QImage::Format_ARGB32);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            image.setPixel(x, y, qRgba((x * 7) % 256, (y * 11) % 256, 90, 255));
        }
    }
    return image;
}

QImage maskImage(const QSize& size, uchar fill)
{
    QImage mask(size, QImage::Format_Grayscale8);
    mask.fill(fill);
    return mask;
}

// Left half foreground, right half background.
QImage halfMask(const QSize& size)
{
    QImage mask = maskImage(size, 0);
    for (int y = 0; y < size.height(); ++y) {
        uchar* row = mask.scanLine(y);
        for (int x = 0; x < size.width() / 2; ++x) {
            row[x] = 255;
        }
    }
    return mask;
}

Cutout::MaskProvider fixedMask(const QImage& mask)
{
    return [mask](const QImage&) {
        MaskResult result;
        result.mask = mask;
        result.status = Status::Success;
        return result;
    };
}

} // namespace

class TestForegroundCutout : public QObject
{
    Q_OBJECT

private slots:
    void applyMask_KeepsCanvasAndForeground();
    void applyMask_PartialCoverageScalesPremultipliedPixel();
    void applyMask_MultipliesExistingAlpha();
    void applyMask_NoSubjectAndNothingRemovedAreNotResults();
    void applyMask_RejectsMismatchedMask();
    void applyMask_RoundedBackIsNothingRemoved();
    void applyCoverage_AlwaysGivesImageWithMetadata();
    void validateSize_Limit();
    void run_PassesProviderStatusThrough();
    void runner_DeliversOnGuiThreadOneAtATime();
    void runner_CancelDropsTheResultAndSkipsQueuedWork();
    void runner_DestroyedWhileRunningDoesNotDeliver();
};

void TestForegroundCutout::applyMask_KeepsCanvasAndForeground()
{
    QImage source = opaqueImage(40, 20);
    source.setDevicePixelRatio(2.0);
    const Result result = Cutout::applyMask(source, halfMask(source.size()));
    QCOMPARE(result.status, Status::Success);
    QCOMPARE(result.image.size(), source.size());
    QCOMPARE(result.image.devicePixelRatio(), 2.0);

    const QImage plain = result.image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            if (x < source.width() / 2) {
                QCOMPARE(plain.pixel(x, y), source.pixel(x, y));
            } else {
                QCOMPARE(qAlpha(plain.pixel(x, y)), 0);
            }
        }
    }
}

void TestForegroundCutout::applyMask_PartialCoverageScalesPremultipliedPixel()
{
    QImage source(1, 1, QImage::Format_ARGB32);
    source.setPixel(0, 0, qRgba(200, 100, 50, 255));
    QImage mask = maskImage(source.size(), 128);
    const Result result = Cutout::applyMask(source, mask);
    QCOMPARE(result.status, Status::Success);
    QCOMPARE(result.image.format(), QImage::Format_ARGB32_Premultiplied);
    const QRgb premultiplied = result.image.pixel(0, 0);
    QCOMPARE(qAlpha(premultiplied), 128);
    // Unpremultiplied, a soft edge keeps the subject's colour.
    const QRgb plain = result.image.convertToFormat(QImage::Format_ARGB32).pixel(0, 0);
    QVERIFY(qAbs(qRed(plain) - 200) <= 2);
    QVERIFY(qAbs(qGreen(plain) - 100) <= 2);
    QVERIFY(qAbs(qBlue(plain) - 50) <= 2);
}

void TestForegroundCutout::applyMask_MultipliesExistingAlpha()
{
    // A pin that already has transparent areas (an inserted band, an earlier
    // cut-out) never regains opacity.
    QImage source = opaqueImage(4, 1);
    source.setPixel(0, 0, qRgba(0, 0, 0, 0));
    source.setPixel(1, 0, qRgba(255, 255, 255, 100));
    QImage mask = maskImage(source.size(), 255);
    mask.scanLine(0)[3] = 0;
    const Result result = Cutout::applyMask(source, mask);
    QCOMPARE(result.status, Status::Success);
    const QImage plain = result.image.convertToFormat(QImage::Format_ARGB32);
    QCOMPARE(qAlpha(plain.pixel(0, 0)), 0);
    QCOMPARE(qAlpha(plain.pixel(1, 0)), 100);
    QCOMPARE(qAlpha(plain.pixel(2, 0)), 255);
    QCOMPARE(qAlpha(plain.pixel(3, 0)), 0);
}

void TestForegroundCutout::applyMask_NoSubjectAndNothingRemovedAreNotResults()
{
    const QImage source = opaqueImage(10, 10);
    const Result none = Cutout::applyMask(source, maskImage(source.size(), 0));
    QCOMPARE(none.status, Status::NoSubject);
    QVERIFY(none.image.isNull());

    const Result all = Cutout::applyMask(source, maskImage(source.size(), 255));
    QCOMPARE(all.status, Status::NothingRemoved);
    QVERIFY(all.image.isNull());

    // Background removed only where the image was already transparent.
    QImage transparentRight = source;
    for (int y = 0; y < 10; ++y) {
        for (int x = 5; x < 10; ++x) {
            transparentRight.setPixel(x, y, qRgba(0, 0, 0, 0));
        }
    }
    QCOMPARE(Cutout::applyMask(transparentRight, halfMask(source.size())).status,
             Status::NothingRemoved);
}

void TestForegroundCutout::applyMask_RejectsMismatchedMask()
{
    const QImage source = opaqueImage(10, 10);
    QCOMPARE(Cutout::applyMask(source, maskImage(QSize(9, 10), 255)).status, Status::Failed);
    QCOMPARE(Cutout::applyMask(source, QImage()).status, Status::Failed);
    QCOMPARE(Cutout::applyMask(QImage(), maskImage(source.size(), 255)).status, Status::Failed);
    const QImage rgbMask = maskImage(source.size(), 255).convertToFormat(QImage::Format_RGB32);
    QCOMPARE(Cutout::applyMask(source, rgbMask).status, Status::Failed);
}

void TestForegroundCutout::applyMask_RoundedBackIsNothingRemoved()
{
    // Coverage 254 rounds every channel of this pixel back to its input.
    QImage source(1, 1, QImage::Format_ARGB32_Premultiplied);
    source.setPixel(0, 0, qRgba(64, 32, 16, 100));
    const Result result = Cutout::applyMask(source, maskImage(source.size(), 254));
    QCOMPARE(result.status, Status::NothingRemoved);
    QVERIFY(result.image.isNull());
}

void TestForegroundCutout::applyCoverage_AlwaysGivesImageWithMetadata()
{
    QImage source = opaqueImage(10, 10);
    source.setDevicePixelRatio(2.0);
    source.setText(QStringLiteral("SnapTraySourceLogicalSize"), QStringLiteral("5x5"));
    const QImage none = Cutout::applyCoverage(source, maskImage(source.size(), 0));
    QVERIFY(!none.isNull());
    QCOMPARE(none.size(), source.size());
    QCOMPARE(none.devicePixelRatio(), 2.0);
    QCOMPARE(none.text(QStringLiteral("SnapTraySourceLogicalSize")), QStringLiteral("5x5"));
    QCOMPARE(qAlpha(none.pixel(3, 3)), 0);
    QVERIFY(!Cutout::applyCoverage(source, maskImage(source.size(), 255)).isNull());
    QVERIFY(Cutout::applyCoverage(source, maskImage(QSize(9, 10), 255)).isNull());
    // A successful applyMask hands its mask back for the regions.
    const Result result = Cutout::applyMask(source, halfMask(source.size()));
    QCOMPARE(result.mask.size(), source.size());
}

void TestForegroundCutout::validateSize_Limit()
{
    QCOMPARE(Cutout::validateSize(QSize(7680, 4320)), Status::Success);
    QCOMPARE(Cutout::validateSize(QSize(8000, 5000)), Status::Success);
    QCOMPARE(Cutout::validateSize(QSize(8000, 5001)), Status::TooLarge);
    QCOMPARE(Cutout::validateSize(QSize(1, 40'000'001)), Status::TooLarge);
    QCOMPARE(Cutout::validateSize(QSize()), Status::Failed);
}

void TestForegroundCutout::run_PassesProviderStatusThrough()
{
    const QImage source = opaqueImage(10, 10);
    for (const Status status : {Status::Unsupported, Status::NoSubject, Status::Failed}) {
        const Result result = Cutout::run(source, [status](const QImage&) {
            MaskResult mask;
            mask.status = status;
            return mask;
        });
        QCOMPARE(result.status, status);
        QVERIFY(result.image.isNull());
    }
    QCOMPARE(Cutout::run(source, Cutout::MaskProvider()).status, Status::Unsupported);
    QCOMPARE(Cutout::run(source, fixedMask(halfMask(source.size()))).status, Status::Success);
}

void TestForegroundCutout::runner_DeliversOnGuiThreadOneAtATime()
{
    const QImage source = opaqueImage(20, 10);
    auto gate = std::make_shared<QSemaphore>(0);
    auto calls = std::make_shared<std::atomic_int>(0);
    SnapTray::ForegroundCutoutRunner runner([gate, calls](const QImage& image) {
        ++*calls;
        gate->acquire();
        return fixedMask(halfMask(image.size()))(image);
    });

    bool delivered = false;
    Result received;
    QThread* deliveredOn = nullptr;
    QVERIFY(runner.start(source, [&](const Result& result) {
        delivered = true;
        received = result;
        deliveredOn = QThread::currentThread();
    }));
    QVERIFY(runner.isBusy());
    // A second request while one is in flight starts nothing.
    QVERIFY(!runner.start(source, [](const Result&) { QFAIL("second job ran"); }));

    gate->release();
    QTRY_VERIFY(delivered);
    QCOMPARE(received.status, Status::Success);
    QCOMPARE(deliveredOn, QThread::currentThread());
    QVERIFY(!runner.isBusy());
    QCOMPARE(calls->load(), 1);
}

void TestForegroundCutout::runner_CancelDropsTheResultAndSkipsQueuedWork()
{
    const QImage source = opaqueImage(20, 10);
    auto gate = std::make_shared<QSemaphore>(0);
    auto calls = std::make_shared<std::atomic_int>(0);
    auto blocking = [gate, calls](const QImage& image) {
        ++*calls;
        gate->acquire();
        return fixedMask(halfMask(image.size()))(image);
    };

    // Two runners share one worker: the second job waits behind the first.
    SnapTray::ForegroundCutoutRunner first(blocking);
    SnapTray::ForegroundCutoutRunner second(blocking);
    bool firstDelivered = false;
    bool secondDelivered = false;
    QVERIFY(first.start(source, [&](const Result&) { firstDelivered = true; }));
    QTRY_COMPARE(calls->load(), 1);
    QVERIFY(second.start(source, [&](const Result&) { secondDelivered = true; }));

    first.cancel();
    second.cancel();
    QVERIFY(!first.isBusy());
    QVERIFY(!second.isBusy());

    gate->release();
    // The started job finishes on its own; the queued one never runs.
    QTest::qWait(200);
    QCoreApplication::processEvents();
    QVERIFY(!firstDelivered);
    QVERIFY(!secondDelivered);
    QCOMPARE(calls->load(), 1);

    // A cancelled runner can start again.
    bool again = false;
    gate->release();
    QVERIFY(first.start(source, [&](const Result& result) {
        again = result.status == Status::Success;
    }));
    QTRY_VERIFY(again);
}

void TestForegroundCutout::runner_DestroyedWhileRunningDoesNotDeliver()
{
    const QImage source = opaqueImage(20, 10);
    auto gate = std::make_shared<QSemaphore>(0);
    auto entered = std::make_shared<std::atomic_bool>(false);
    auto finished = std::make_shared<std::atomic_bool>(false);
    bool delivered = false;
    {
        SnapTray::ForegroundCutoutRunner runner([gate, entered, finished](const QImage& image) {
            *entered = true;
            gate->acquire();
            *finished = true;
            return fixedMask(halfMask(image.size()))(image);
        });
        QVERIFY(runner.start(source, [&](const Result&) { delivered = true; }));
        QTRY_VERIFY(entered->load());
        // Destroyed here, while the worker is inside the provider, without
        // waiting for it.
    }
    gate->release();
    QTRY_VERIFY(finished->load());
    QTest::qWait(50);
    QCoreApplication::processEvents();
    QVERIFY(!delivered);
}

QTEST_MAIN(TestForegroundCutout)
#include "tst_ForegroundCutout.moc"
