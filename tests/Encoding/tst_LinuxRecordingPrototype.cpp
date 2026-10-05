#include "IVideoEncoder.h"
#include "encoding/EncodingWorker.h"
#include "RecordingManager.h"
#include "settings/RecordingSettingsManager.h"
#include "settings/FileSettingsManager.h"
#include "qml/SettingsBackend.h"
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>
#include <QElapsedTimer>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QWidget>
#include <QWindow>
#include <QPainter>
#include <QTimer>
#include "qml/QmlRecordingControlBar.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

extern "C" {
#include <libavformat/avformat.h>
}

class TestLinuxRecordingPrototype : public QObject
{
    Q_OBJECT
private slots:
    void captureFullScreen();
    void benchmark4K();
    void mainRecordingFlowSavesSilentMp4();
    void mainRecordingFlowRequestsPreview();
};

void TestLinuxRecordingPrototype::captureFullScreen()
{
    // CTest starts this test on its own Xvfb display. Do not capture a user's
    // desktop if the test is launched directly without that explicit marker.
    if (qEnvironmentVariable("SNAPTRAY_TEST_ISOLATED_X11") != "1") {
        QSKIP("Run through CTest's isolated Xvfb wrapper.");
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QWidget scene;
    scene.setStyleSheet("background: #2060d0;");
    scene.showFullScreen();
    QVERIFY(QTest::qWaitForWindowExposed(&scene));
    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);
    const QImage first = screen->grabWindow(0).toImage();
    QCOMPARE(first.size(), QSize(1920, 1080));
    const QString path = dir.filePath("screen.mp4");
    auto* encoder = IVideoEncoder::createNativeEncoder();
    QVERIFY(encoder);
    auto* worker = new EncodingWorker;
    worker->setVideoEncoder(encoder);
    QThread thread;
    auto cleanup = qScopeGuard([&] {
        if (thread.isRunning()) {
            worker->stop();
            thread.quit();
            thread.wait();
        } else {
            delete worker;
        }
    });
    QVERIFY2(encoder->start(path, first.size(), 30), qPrintable(encoder->lastError()));
    QSignalSpy errors(encoder, &IVideoEncoder::error);
    QSignalSpy finished(worker, &EncodingWorker::finished);
    worker->moveOwnedEncodersToThread(&thread);
    worker->moveToThread(&thread);
    connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
    thread.start();
    QVERIFY(worker->start());
    QElapsedTimer elapsed;
    elapsed.start();
    QVERIFY(worker->enqueueFrame({first, 0}));
    int accepted = 1;
    for (int i = 1; i < 30; ++i) {
        QTest::qWait(33);
        scene.setStyleSheet(i % 2 ? "background: #2060d0;" : "background: #d06020;");
        QCoreApplication::processEvents();
        const qint64 timestamp = elapsed.elapsed();
        const QImage frame = screen->grabWindow(0).toImage();
        QVERIFY(!frame.isNull());
        if (worker->enqueueFrame({frame, timestamp})) {
            ++accepted;
        }
    }
    worker->requestFinish();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 20000);
    QCOMPARE(errors.count(), 0);
    QVERIFY(finished.at(0).at(0).toBool());
    QCOMPARE(worker->framesWritten(), accepted);
    AVFormatContext* format = nullptr;
    QCOMPARE(avformat_open_input(&format, QFile::encodeName(path).constData(), nullptr, nullptr), 0);
    auto closeFormat = qScopeGuard([&] { avformat_close_input(&format); });
    QVERIFY(avformat_find_stream_info(format, nullptr) >= 0);
    QCOMPARE(format->nb_streams, 1u);
    QCOMPARE(format->streams[0]->codecpar->width, 1920);
    QCOMPARE(format->streams[0]->codecpar->height, 1080);
    QVERIFY(format->duration >= 900000);
}

void TestLinuxRecordingPrototype::mainRecordingFlowSavesSilentMp4()
{
    if (qEnvironmentVariable("SNAPTRAY_TEST_ISOLATED_X11") != "1") {
        QSKIP("Run through CTest's isolated Xvfb wrapper.");
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto& settings = RecordingSettingsManager::instance();
    settings.setShowPreview(false);
    settings.setAudioEnabled(false);
    settings.setOutputFormat(0);
    settings.setCountdownEnabled(false);
    settings.setFrameRate(30);
    auto& files = FileSettingsManager::instance();
    files.saveRecordingPath(dir.path());
    files.saveAutoSaveRecordings(true);
    SnapTray::SettingsBackend backend;
    QVERIFY(backend.recordingSupported());
    QVERIFY(!backend.recordingDirectMp4Only());
    QCOMPARE(backend.recordingOutputFormat(), 0);
    QVERIFY(!backend.recordingAudioEnabled());
    QVERIFY(!backend.recordingShowPreview());

    RecordingManager manager;
    QSignalSpy errors(&manager, &RecordingManager::recordingError);
    QSignalSpy started(&manager, &RecordingManager::recordingStarted);
    QSignalSpy stopped(&manager, &RecordingManager::recordingStopped);
    QSignalSpy preview(&manager, &RecordingManager::previewRequested);
    manager.startScreenRecording(QGuiApplication::primaryScreen());
    QTRY_VERIFY_WITH_TIMEOUT(started.count() == 1 || !errors.isEmpty(), 10000);
    QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first().first().toString()));
    QCOMPARE(started.count(), 1);
    QTest::qWait(1100);
    QVERIFY(manager.m_controlBar);
    bool visibleControls = false;
    for (auto* window : QGuiApplication::allWindows())
        if (window->winId() == manager.m_controlBar->winId()) visibleControls = window->isVisible();
    QVERIFY(!visibleControls);
    const auto beforeBusyUi = manager.m_frameCount.load();
    QThread::msleep(200); // Deliberately block GUI event delivery.
    QVERIFY(manager.m_frameCount.load() > beforeBusyUi);
    manager.pauseRecording();
    QCOMPARE(manager.state(), RecordingManager::State::Paused);
    const auto pausedFrames = manager.m_frameCount.load();
    QTest::qWait(100);
    QCOMPARE(manager.m_frameCount.load(), pausedFrames);
    manager.resumeRecording();
    QCOMPARE(manager.state(), RecordingManager::State::Recording);
    QTest::qWait(300);
    manager.stopRecording();
    QTRY_VERIFY_WITH_TIMEOUT(stopped.count() == 1 || !errors.isEmpty(), 20000);
    QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first().first().toString()));
    QCOMPARE(stopped.count(), 1);
    QCOMPARE(preview.count(), 0);
    QCOMPARE(manager.state(), RecordingManager::State::Idle);
    const QString path = stopped.first().first().toString();
    QVERIFY(path.startsWith(dir.path()));
    QVERIFY(path.endsWith(".mp4"));
    AVFormatContext* format = nullptr;
    QCOMPARE(avformat_open_input(&format, QFile::encodeName(path).constData(), nullptr, nullptr), 0);
    auto closeFormat = qScopeGuard([&] { avformat_close_input(&format); });
    QVERIFY(avformat_find_stream_info(format, nullptr) >= 0);
    QCOMPARE(format->nb_streams, 1u);
    QCOMPARE(format->streams[0]->codecpar->codec_id, AV_CODEC_ID_H264);
    QCOMPARE(format->streams[0]->codecpar->width, 1920);
    QCOMPARE(format->streams[0]->codecpar->height, 1080);
}

void TestLinuxRecordingPrototype::mainRecordingFlowRequestsPreview()
{
    if (qEnvironmentVariable("SNAPTRAY_TEST_ISOLATED_X11") != "1") QSKIP("Requires isolated display");
    auto& settings = RecordingSettingsManager::instance();
    settings.setShowPreview(true); settings.setAudioEnabled(false); settings.setOutputFormat(0);
    settings.setCountdownEnabled(false); settings.setFrameRate(30);
    SnapTray::SettingsBackend backend;
    QVERIFY(backend.recordingShowPreview());
    RecordingManager manager;
    QSignalSpy errors(&manager, &RecordingManager::recordingError), started(&manager, &RecordingManager::recordingStarted), preview(&manager, &RecordingManager::previewRequested);
    manager.startScreenRecording(QGuiApplication::primaryScreen());
    QTRY_VERIFY_WITH_TIMEOUT(!started.isEmpty() || !errors.isEmpty(), 10000);
    QVERIFY(errors.isEmpty()); QTest::qWait(500); manager.stopRecording();
    QTRY_VERIFY_WITH_TIMEOUT(!preview.isEmpty() || !errors.isEmpty(), 20000);
    QVERIFY(errors.isEmpty()); QCOMPARE(preview.count(), 1);
    QCOMPARE(manager.state(), RecordingManager::State::Previewing);
    const QString path = preview.first().first().toString();
    QVERIFY(QFileInfo::exists(path));
    manager.cancelRecording();
    QFile::remove(path);
}

void TestLinuxRecordingPrototype::benchmark4K()
{
    if (qEnvironmentVariable("SNAPTRAY_TEST_BENCHMARK_4K") != "1"
        || qEnvironmentVariable("SNAPTRAY_TEST_ISOLATED_X11") != "1") QSKIP("Opt-in isolated 4K benchmark");
    class Scene : public QWidget {
    public:
        QImage page;
        int offset = 0;
        Scene() : page(3840, 2160, QImage::Format_RGB32) {
            page.fill(QColor("#20232a"));
            QPainter painter(&page); painter.setPen(QColor("#cdd6f4"));
            painter.setFont(QFont("Sans", 16));
            for (int y = 25; y < 2160; y += 32)
                for (int x = 10; x < 3840; x += 480)
                    painter.drawText(x, y, QString("Recording benchmark %1: 0123456789").arg(y));
        }
        void paintEvent(QPaintEvent*) override {
            QPainter painter(this); painter.drawImage(rect(), page);
            painter.fillRect((offset++*17)%width(), 100, 160, 240, QColor("#4080e0"));
        }
    } scene;
    scene.showFullScreen(); QVERIFY(QTest::qWaitForWindowExposed(&scene));
    auto* screen = QGuiApplication::primaryScreen();
    QCOMPARE(screen->grabWindow(0).size(), QSize(3840,2160));
    QElapsedTimer baseline; baseline.start();
    for (int i=0;i<30;++i) QVERIFY(!screen->grabWindow(0).toImage().isNull());
    qInfo() << "4K synchronous Qt capture mean ms:" << baseline.elapsed()/30.0;
    QTimer animation; connect(&animation, &QTimer::timeout, &scene, QOverload<>::of(&QWidget::update)); animation.start(50);
    QTemporaryDir dir;
    auto& settings = RecordingSettingsManager::instance();
    settings.setShowPreview(false); settings.setAudioEnabled(false); settings.setOutputFormat(0);
    settings.setCountdownEnabled(false); settings.setFrameRate(30);
    auto& files = FileSettingsManager::instance(); files.saveRecordingPath(dir.path()); files.saveAutoSaveRecordings(true);
    RecordingManager manager;
    QSignalSpy errors(&manager, &RecordingManager::recordingError), started(&manager, &RecordingManager::recordingStarted), stopped(&manager, &RecordingManager::recordingStopped);
    manager.startScreenRecording(screen);
    QTRY_VERIFY_WITH_TIMEOUT(!started.isEmpty() || !errors.isEmpty(), 10000);
    QVERIFY(errors.isEmpty());
    // Exclude countdown/control hiding and codec startup from steady-state throughput.
    QTest::qWait(500);
    const auto initialFrames = manager.m_frameCount.load();
    QElapsedTimer elapsed; elapsed.start(); QTest::qWait(4000);
    const auto captureMs = elapsed.elapsed();
    const auto frames = manager.m_frameCount.load()-initialFrames;
    manager.stopRecording();
    QTRY_VERIFY_WITH_TIMEOUT(!stopped.isEmpty() || !errors.isEmpty(), 30000);
    QVERIFY(errors.isEmpty()); QVERIFY(!stopped.isEmpty()); QVERIFY(frames > 0);
    qInfo() << "4K moving-text scene (steady state):" << frames << "accepted frames in" << captureMs
            << "ms; observed fps" << frames*1000.0/captureMs;
}

QTEST_MAIN(TestLinuxRecordingPrototype)
#include "tst_LinuxRecordingPrototype.moc"
