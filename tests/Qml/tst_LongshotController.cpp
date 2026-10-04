#include "qml/LongshotController.h"
#include "longshot/PositionSolver.h"
#include "SyntheticScroll.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <QClipboard>
#include <QApplication>
#include <QQmlEngine>
#include <QQuickImageProvider>
using namespace SnapTray::Longshot;
namespace {
class Source final : public LongshotFrameSource {
public:
    Source(QImage frame, bool slow) : m_frame(std::move(frame)), m_slow(slow) {}
    bool open(const QString&, qint64, qint64, const QRect&) override { m_index = 0; return true; }
    std::optional<QImage> next(qint64* time) override {
        if (m_index >= (m_slow ? 40 : 3)) return {};
        if (m_slow) QThread::msleep(15);
        *time = m_index++ * 50; return m_frame;
    }
    QSize frameSize() const override { return m_frame.size(); }
    QSize videoSize() const override { return m_frame.size(); }
    double frameRate() const override { return 20; }
    int expectedFrameCount() const override { return m_slow ? 40 : 3; }
    QString lastError() const override { return {}; }
private:
    QImage m_frame; bool m_slow; int m_index = 0;
};
LongshotSession::SourceFactory factory(bool slow = false) {
    auto page = SyntheticScroll::renderPage({320, 240, 42});
    return [page, slow] { return std::make_unique<Source>(page, slow); };
}
}
class tst_LongshotController : public QObject {
    Q_OBJECT
private slots:
    void resultAndRetry() {
        QTemporaryDir dir;
        LongshotController controller(nullptr, factory());
        QSignalSpy ready(&controller, &LongshotController::resultReady);
        QSignalSpy saved(&controller, &LongshotController::imageSaved);
        QSignalSpy pinned(&controller, &LongshotController::pinRequested);
        controller.start(dir.filePath("source.mp4"), 0, 150, {});
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
        QVERIFY(!controller.busy()); QVERIFY(controller.hasResult());
        QCOMPARE(controller.partCount(), 1);
        QVERIFY(QFile::exists(controller.preview().toLocalFile()));
        controller.pin(); QCOMPARE(pinned.count(), 1);
        QVERIFY(controller.hasResult());
        // Invalid destination cannot report success; retry leaves the result alive.
        QFile blocker(dir.filePath("blocker")); QVERIFY(blocker.open(QIODevice::WriteOnly)); blocker.close();
        QVERIFY(!controller.saveToDirectory(blocker.fileName(), "capture"));
        QCOMPARE(saved.count(), 0);
        QVERIFY(controller.saveToDirectory(dir.path(), "capture"));
        QCOMPARE(saved.count(), 1);
        QVERIFY(controller.saveToDirectory(dir.path(), "capture"));
        QCOMPARE(saved.count(), 1);
        QCOMPARE(QDir(dir.path()).entryList({"capture*.png"}, QDir::Files).size(), 1);
        QVERIFY(controller.hasResult());
        controller.invalidate(); QVERIFY(!controller.hasResult());
    }
    void cancellationKeepsEventLoopResponsive() {
        LongshotController controller(nullptr, factory(true));
        QSignalSpy ready(&controller, &LongshotController::resultReady);
        QSignalSpy idle(&controller, &LongshotController::idle);
        int ticks = 0; QTimer timer; timer.setInterval(1);
        connect(&timer, &QTimer::timeout, this, [&] { ++ticks; }); timer.start();
        controller.start("unused", 0, -1, {});
        QTimer::singleShot(30, &controller, &LongshotController::cancel);
        QTRY_COMPARE_WITH_TIMEOUT(idle.count(), 1, 10000);
        QVERIFY(ticks > 1); QCOMPARE(ready.count(), 0); QVERIFY(!controller.hasResult());
        controller.start("unused", 0, -1, {});
        controller.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(idle.count(), 2, 10000);
        QCOMPARE(ready.count(), 0);
    }
    void unavailableSourceFails() {
        LongshotController controller(nullptr, [] { return std::unique_ptr<LongshotFrameSource>(); });
        QSignalSpy idle(&controller, &LongshotController::idle);
        controller.start("missing", 0, -1, {});
        QTRY_COMPARE_WITH_TIMEOUT(idle.count(), 1, 10000);
        QVERIFY(!controller.hasResult()); QVERIFY(!controller.message().isEmpty());
    }
    void boundedTileProviderIsRemoved() {
        QQmlEngine engine;
        QString provider;
        {
            LongshotController controller(nullptr, factory());
            controller.installImageProvider(&engine);
            QSignalSpy ready(&controller, &LongshotController::resultReady);
            controller.start("unused", 0, -1, {});
            QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
            provider = controller.preview().host();
            auto* images = static_cast<QQuickImageProvider*>(engine.imageProvider(provider));
            QVERIFY(images);
            QSize original;
            const auto tile = images->requestImage("1/0", &original, QSize(10000, 10000));
            QVERIFY(!tile.isNull()); QVERIFY(tile.width() <= 2048); QVERIFY(tile.height() <= 2048);
            QCOMPARE(original, controller.imageSize());
            QVERIFY(images->requestImage("1/100", nullptr, {}).isNull());
        }
        QVERIFY(!engine.imageProvider(provider));
    }
    void solverCancellation() {
        std::vector<qint64> times; std::vector<PairShift> edges;
        for (int i = 0; i < 100; ++i) { times.push_back(i * 50); if (i) edges.push_back({i - 1, i, 20, 1.0}); }
        int calls = 0;
        auto result = PositionSolver::solve(times, edges, 3, 240, [&] { return ++calls < 3; });
        QVERIFY(result.cancelled);
    }
};
QTEST_MAIN(tst_LongshotController)
#include "tst_LongshotController.moc"
