#include <QtTest>
#include <QSemaphore>
#include <QtEndian>
#include "video/RemixedAudioPreview.h"
#include <mutex>
#include <atomic>

namespace {
struct OutputState {
    std::mutex mutex;
    QList<QByteArray> writes;
    std::atomic<int> resets{0};
    std::atomic<int> queued{0};
} output;
class FakeOutput final : public SnapTray::PreviewPcmOutput {
public:
    bool open() override { return true; }
    void reset() override { ++output.resets; output.queued = 0; }
    int queuedFrames() const override { return output.queued.load(); }
    bool write(const QByteArray& pcm) override {
        std::lock_guard<std::mutex> lock(output.mutex);
        output.writes.append(pcm); output.queued += kChunkFrames;
        return true;
    }
};
int writeCount() { std::lock_guard<std::mutex> lock(output.mutex); return output.writes.size(); }
QByteArray lastWrite() { std::lock_guard<std::mutex> lock(output.mutex); return output.writes.last(); }
}
namespace SnapTray {
std::unique_ptr<PreviewPcmOutput> createPreviewPcmOutput() { return std::make_unique<FakeOutput>(); }
}
class TestRemixedAudioPreview : public QObject {
    Q_OBJECT
private slots:
    void init() { std::lock_guard<std::mutex> lock(output.mutex); output.writes.clear(); output.resets = 0; output.queued = 0; }
    void seekDiscardsAnInflightRead();
    void pauseSpeedAndVolume();
};

void TestRemixedAudioPreview::seekDiscardsAnInflightRead()
{
    QSemaphore entered, release;
    std::atomic<bool> block{true};
    SnapTray::RemixedAudioPreview preview;
    preview.setSource([&](qint64 start, int frames) {
        if (block.exchange(false)) { entered.release(); release.acquire(); }
        QByteArray pcm(frames * 4, '\0');
        pcm[0] = start >= 48000 ? '\2' : '\1';
        return pcm;
    });
    preview.synchronize(0, true, 1.0f, true);
    const bool reading = entered.tryAcquire(1, 2000);
    if (!reading) release.release(); // prevent teardown hanging on a failed assertion
    QVERIFY(reading);
    preview.synchronize(1000, true, 1.0f, true);
    release.release();
    QTRY_VERIFY(writeCount() >= 1);
    preview.synchronize(1000, false, 1.0f);
    QTRY_COMPARE(output.queued.load(), 0);
    std::lock_guard<std::mutex> lock(output.mutex);
    for (const auto& pcm : output.writes) QCOMPARE(pcm[0], '\2');
}

void TestRemixedAudioPreview::pauseSpeedAndVolume()
{
    SnapTray::RemixedAudioPreview preview;
    preview.setSource([](qint64, int frames) {
        QByteArray pcm(frames * 4, '\0');
        for (int i = 0; i < pcm.size(); i += 2) qToLittleEndian<qint16>(10000, pcm.data() + i);
        return pcm;
    });
    preview.setVolume(0.5f, false);
    preview.synchronize(0, true, 1.0f, true);
    QTRY_VERIFY(writeCount() >= 1);
    QCOMPARE(qFromLittleEndian<qint16>(lastWrite().constData()), qint16(5000));
    preview.synchronize(100, false, 1.0f);
    QTRY_COMPARE(output.queued.load(), 0);
    const int pausedCount = writeCount();
    QTest::qWait(30);
    QCOMPARE(writeCount(), pausedCount);
    preview.synchronize(100, true, 2.0f); // edited speed policy: silence
    QTest::qWait(30);
    QCOMPARE(writeCount(), pausedCount);
    preview.setVolume(1.0f, true);
    preview.synchronize(100, true, 1.0f);
    QTRY_VERIFY(writeCount() > pausedCount);
    QCOMPARE(lastWrite(), QByteArray(SnapTray::PreviewPcmOutput::kChunkFrames * 4, '\0'));
    preview.setSource({});
    QTRY_COMPARE(output.queued.load(), 0);
    QVERIFY(!preview.hasSource());
}
QTEST_GUILESS_MAIN(TestRemixedAudioPreview)
#include "tst_RemixedAudioPreview.moc"
