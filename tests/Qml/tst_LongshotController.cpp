#include "qml/LongshotController.h"
#include "longshot/LongshotSession.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QClipboard>
#include <QApplication>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QThread>
#include <stdexcept>
using namespace SnapTray::Longshot;
namespace {
struct Stats { std::atomic_int opens{0}; std::atomic_bool fail{false}, throws{false}; };
class Source final : public LongshotFrameSource {
public:
    Source(std::shared_ptr<Stats> stats, bool stationary, bool slow) : stats(stats), stationary(stationary), slow(slow), page(160,400,QImage::Format_RGB32) {
        QRandomGenerator random(42);
        for(int y=0;y<page.height();++y) for(int x=0;x<page.width();++x) page.setPixel(x,y,QColor::fromRgb(random.generate()).rgb());
    }
    bool open(const QString&,qint64,qint64,const QRect&) override { index=0; ++stats->opens; return !stats->fail; }
    std::optional<QImage> next(qint64* time) override {
        if (stats->throws) throw std::runtime_error("Decode failed");
        if(index==4) return {};
        if(slow) QThread::msleep(30);
        *time=index*50; return page.copy(0,stationary ? (index++,0) : index++*60,160,200);
    }
    QSize frameSize() const override { return {160,200}; }
    QSize videoSize() const override { return frameSize(); }
    double frameRate() const override { return 20; }
    int expectedFrameCount() const override { return 4; }
    QString lastError() const override { return {}; }
    std::shared_ptr<Stats> stats; bool stationary,slow; int index=0; QImage page;
};
LongshotSession::SourceFactory factory(std::shared_ptr<Stats> stats, bool stationary=false, bool slow=false) {
    return [=]{return std::make_unique<Source>(stats,stationary,slow);};
}
}
class tst_LongshotController : public QObject {
    Q_OBJECT
private slots:
    void multipartSaveRetriesOnlyFailedImages() {
        LongshotController controller;
        QImage first(64, 100, QImage::Format_RGB32); first.fill(Qt::red);
        QImage second(64, 80, QImage::Format_RGB32); second.fill(Qt::blue);
        // A missing second image simulates an encoder failure after part one succeeds.
        controller.m_result.parts = {first, QImage()};
        controller.m_result.fullHeightPx = 180;
        controller.m_phase = "result";
        QTemporaryDir directory;
        QSignalSpy saved(&controller, &LongshotController::imageSaved);
        QVERIFY(!controller.saveToDirectory(directory.path(), "long"));
        QCOMPARE(saved.count(), 1);
        QCOMPARE(QImage(directory.filePath("long-001.png")), first);
        controller.m_result.parts[1] = second;
        controller.save(); // Retries the remembered destination without another dialog.
        QCOMPARE(saved.count(), 2);
        QCOMPARE(QImage(directory.filePath("long-002.png")), second);
        QCOMPARE(QDir(directory.path()).entryList({"*.png"}, QDir::Files).size(), 2);
        controller.setSelectedPart(1);
        QSignalSpy pin(&controller, &LongshotController::pinRequested);
        controller.pin();
        QCOMPARE(qvariant_cast<QImage>(pin[0][0]), second);
    }
    void clipboardUsesOriginalPixels() {
        LongshotController controller(nullptr,factory(std::make_shared<Stats>()));
        controller.start("fixture",0,200,{});
        QTRY_COMPARE_WITH_TIMEOUT(controller.phase(),QString("recommendation"),10000);
        controller.generate(); QTRY_VERIFY_WITH_TIMEOUT(controller.hasResult(),10000);
        QSignalSpy pinned(&controller,&LongshotController::pinRequested);
        controller.pin(); controller.copy();
        QTRY_VERIFY(!controller.message().isEmpty());
        const QImage copied=QApplication::clipboard()->image();
        if(copied.isNull()) QSKIP("System clipboard unavailable in this desktop session");
        QCOMPARE(copied,qvariant_cast<QImage>(pinned[0][0]));
    }
    void recommendationThenSelectedOutput() {
        const auto stats=std::make_shared<Stats>();
        QQmlEngine engine;
        LongshotController controller(nullptr,factory(stats)); controller.installImageProvider(&engine);
        controller.start("fixture",0,200,{});
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),10000);
        QCOMPARE(controller.phase(),QString("recommendation")); QVERIFY(!controller.hasResult());
        QVERIFY(!controller.candidates().isEmpty());
        const auto choice=controller.candidate();
        QVERIFY(choice.value("recommended").toBool());
        auto url=choice.value("startPreview").toUrl();
        auto* provider=static_cast<QQuickImageProvider*>(engine.imageProvider(url.host()));
        QVERIFY(provider); QSize size;
        QVERIFY(!provider->requestImage(url.path().mid(1),&size,{}).isNull());
        const int opens=stats->opens;
        controller.generate(); QTRY_VERIFY_WITH_TIMEOUT(controller.hasResult(),10000);
        QCOMPARE(stats->opens,opens+1); // Render only: no new analysis/solve pass.
        QCOMPARE(controller.phase(),QString("result"));
        QCOMPARE(controller.imageSize(),QSize(choice.value("width").toInt(),choice.value("height").toInt()));
        QSignalSpy pin(&controller,&LongshotController::pinRequested), annotate(&controller,&LongshotController::annotateRequested);
        controller.pin(); controller.annotate();
        QCOMPARE(pin.count(),1); QCOMPARE(annotate.count(),1);
        QCOMPARE(qvariant_cast<QImage>(pin[0][0]),qvariant_cast<QImage>(annotate[0][0]));
        QTemporaryDir dir; QFile blocker(dir.filePath("blocker")); QVERIFY(blocker.open(QIODevice::WriteOnly)); blocker.close();
        QVERIFY(!controller.saveToDirectory(blocker.fileName(),"image"));
        QSignalSpy saved(&controller,&LongshotController::imageSaved);
        QVERIFY(controller.saveToDirectory(dir.path(),"image")); QVERIFY(controller.saveToDirectory(dir.path(),"image"));
        QCOMPARE(saved.count(),1); QVERIFY(QFile::exists(dir.filePath("image.png")));
        controller.showRecommendation(); QVERIFY(!controller.hasResult()); QCOMPARE(controller.candidate(),choice);
    }
    void stationaryIsActionable() {
        LongshotController controller(nullptr,factory(std::make_shared<Stats>(),true));
        controller.start("fixture",0,200,{}); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),10000);
        QCOMPARE(controller.phase(),QString("error")); QCOMPARE(controller.failureReason(),QString("noScrolling"));
        QVERIFY(controller.candidates().isEmpty()); QVERIFY(!controller.hasResult());
    }
    void cancellationAndRetryPreserveRecommendation() {
        const auto stats=std::make_shared<Stats>();
        LongshotController controller(nullptr,factory(stats,false,true));
        controller.start("fixture",0,200,{}); controller.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),10000); QCOMPARE(controller.phase(),QString("idle"));
        controller.start("fixture",0,200,{}); QTRY_COMPARE_WITH_TIMEOUT(controller.phase(),QString("recommendation"),10000);
        const auto choice=controller.candidate();
        controller.generate(); controller.cancel(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),10000);
        QCOMPARE(controller.phase(),QString("recommendation")); QCOMPARE(controller.candidate(),choice);
        stats->fail=true; controller.generate(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),10000);
        QCOMPARE(controller.phase(),QString("recommendation")); QCOMPARE(controller.candidate(),choice); QVERIFY(!controller.message().isEmpty());
        stats->fail=false; stats->throws=true; controller.generate(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),10000);
        QCOMPARE(controller.phase(),QString("recommendation")); QCOMPARE(controller.candidate(),choice);
        stats->throws=false; controller.generate(); QTRY_VERIFY_WITH_TIMEOUT(controller.hasResult(),10000);
        controller.invalidate(); QCOMPARE(controller.phase(),QString("idle")); QVERIFY(controller.candidates().isEmpty());
    }
    void invalidationRejectsWorkerResult() {
        LongshotController controller(nullptr,factory(std::make_shared<Stats>(),false,true));
        controller.start("fixture",0,200,{}); controller.invalidate();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(),10000);
        QCOMPARE(controller.phase(),QString("idle")); QVERIFY(controller.candidates().isEmpty()); QVERIFY(!controller.hasResult());
    }
    void sessionRejectsStaleIdsAndInputs() {
        const auto stats=std::make_shared<Stats>(); LongshotSession session(factory(stats));
        session.setRecording("fixture"); session.setTrim(0,200);
        auto report=session.analyze({}); QCOMPARE(report.error,LongshotError::None); QVERIFY(!report.candidates.empty());
        const auto id=report.candidates.front().id;
        QVERIFY(session.renderCandidate("obsolete",{}).error!=LongshotError::None);
        session.setCrop(QRect(0,0,100,100)); QVERIFY(session.renderCandidate(id,{}).error!=LongshotError::None);
        session.setCrop({}); report=session.analyze({}); QVERIFY(!report.candidates.empty());
        QVERIFY(report.candidates.front().id!=id); QVERIFY(session.renderCandidate(id,{}).error!=LongshotError::None);
        session.setTrim(0,100); QVERIFY(session.renderCandidate(report.candidates.front().id,{}).error!=LongshotError::None);
    }
};
QTEST_MAIN(tst_LongshotController)
#include "tst_LongshotController.moc"
