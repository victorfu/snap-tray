#include "IVideoEncoder.h"
#include <mutex>
#include "capture/X11CaptureEngine.h"
#include "encoding/EncodingWorker.h"
#include "RecordingManager.h"
#include "recording/CursorTimeline.h"
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
#include "qml/QmlRecordingBoundary.h"
#include "recording/RecordingSession.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

extern "C" {
#include <libavformat/avformat.h>
}

class TestLinuxRecordingPrototype : public QObject
{
    Q_OBJECT
private slots:
    void captureFullScreen();
    void captureContinuesWhileGuiBlocked();
    void benchmark4K();
    void mainRecordingFlowSavesSilentMp4();
    void mainRecordingFlowRequestsPreview();
    void chosenAreaQuickModeRecordsTheArea();
    void chosenAreaEditModeRecordsTheScreenAndCrops();
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
    // The real X11 pointer sidecar: complete, with shapes and clicks, in video pixels.
    const auto cursor = SnapTray::CursorTimelineSidecar::read(path);
    QVERIFY(cursor);
    QVERIFY(cursor->finished);
    QVERIFY(cursor->info.shapesAvailable);
    QVERIFY(cursor->info.clicksAvailable);
    QVERIFY(!cursor->info.cursorInVideo);
    QCOMPARE(cursor->info.frameSize, QGuiApplication::primaryScreen()->size());
    QVERIFY(!cursor->samples.empty());
    QVERIFY(cursor->samples.front().shapeId != 0);
    QVERIFY(cursor->shape(cursor->samples.front().shapeId));
    manager.cancelRecording();
    QFile::remove(path);
    SnapTray::CursorTimelineSidecar::remove(path);
}

// #194, quick mode: the chosen area is the recorded region, so the saved
// file is exactly that area.
void TestLinuxRecordingPrototype::chosenAreaQuickModeRecordsTheArea()
{
    if (qEnvironmentVariable("SNAPTRAY_TEST_ISOLATED_X11") != "1") QSKIP("Requires isolated display");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto& settings = RecordingSettingsManager::instance();
    settings.setShowPreview(false); settings.setAudioEnabled(false); settings.setOutputFormat(0);
    settings.setCountdownEnabled(false); settings.setFrameRate(30);
    auto& files = FileSettingsManager::instance();
    files.saveRecordingPath(dir.path());
    files.saveAutoSaveRecordings(true);
    RecordingManager manager;
    QSignalSpy errors(&manager, &RecordingManager::recordingError), started(&manager, &RecordingManager::recordingStarted),
        stopped(&manager, &RecordingManager::recordingStopped);
    // Odd sizes: the recorded region grows to even pixels, and the frame
    // goes around that region, never into it (PR #205 review).
    const QRect area(100, 60, 641, 361);
    manager.startScreenRecording(QGuiApplication::primaryScreen(), area);
    QTRY_VERIFY_WITH_TIMEOUT(!started.isEmpty() || !errors.isEmpty(), 10000);
    QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first().first().toString()));
    const QRect region = manager.m_recordingRegion;
    QVERIFY(region.contains(area));
    QCOMPARE(region.width() % 2, 0);
    QCOMPARE(region.height() % 2, 0);
    QVERIFY(manager.m_areaBoundary); // its frame, just outside the region
    QCOMPARE(manager.m_areaBoundary->region(), region);
    manager.pauseRecording();
    QCOMPARE(manager.m_areaBoundary->borderMode(), SnapTray::QmlRecordingBoundary::BorderMode::Paused);
    manager.resumeRecording();
    QTest::qWait(500);
    manager.stopRecording();
    QTRY_VERIFY_WITH_TIMEOUT(!stopped.isEmpty() || !errors.isEmpty(), 20000);
    QVERIFY(errors.isEmpty());
    QVERIFY(!manager.m_areaBoundary);
    QVERIFY(manager.previewInitialCrop().isEmpty());
    const QString path = stopped.first().first().toString();
    AVFormatContext* format = nullptr;
    QCOMPARE(avformat_open_input(&format, QFile::encodeName(path).constData(), nullptr, nullptr), 0);
    auto closeFormat = qScopeGuard([&] { avformat_close_input(&format); });
    QVERIFY(avformat_find_stream_info(format, nullptr) >= 0);
    QCOMPARE(format->streams[0]->codecpar->width, region.width());
    QCOMPARE(format->streams[0]->codecpar->height, region.height());
}

// #194, edit mode: the whole screen is recorded; the area is the preview's
// initial crop, in the manifest as well (recovery opens with it).
void TestLinuxRecordingPrototype::chosenAreaEditModeRecordsTheScreenAndCrops()
{
    if (qEnvironmentVariable("SNAPTRAY_TEST_ISOLATED_X11") != "1") QSKIP("Requires isolated display");
    auto& settings = RecordingSettingsManager::instance();
    settings.setShowPreview(true); settings.setAudioEnabled(false); settings.setOutputFormat(0);
    settings.setCountdownEnabled(false); settings.setFrameRate(30);
    RecordingManager manager;
    QSignalSpy errors(&manager, &RecordingManager::recordingError), started(&manager, &RecordingManager::recordingStarted),
        preview(&manager, &RecordingManager::previewRequested);
    const QRect area(100, 60, 640, 360);
    manager.startScreenRecording(QGuiApplication::primaryScreen(), area);
    QTRY_VERIFY_WITH_TIMEOUT(!started.isEmpty() || !errors.isEmpty(), 10000);
    QVERIFY(errors.isEmpty());
    QCOMPARE(manager.m_recordingRegion, QGuiApplication::primaryScreen()->geometry());
    // X11 cannot leave a frame out of the full-screen capture: none is shown,
    // so a later crop change never reveals one (PR #205 review).
    QVERIFY(!manager.m_areaBoundary);
    QCOMPARE(manager.m_session.initialCrop, area); // 1x: video pixels = logical
    const auto manifest = SnapTray::RecordingSession::read(SnapTray::RecordingSession::manifestPathFor(manager.m_session.videoPath));
    QVERIFY(manifest);
    QCOMPARE(manifest->initialCrop, area);
    QTest::qWait(300);
    manager.stopRecording();
    QTRY_VERIFY_WITH_TIMEOUT(!preview.isEmpty() || !errors.isEmpty(), 20000);
    QVERIFY(errors.isEmpty());
    QVERIFY(!manager.m_areaBoundary);
    QCOMPARE(manager.previewInitialCrop(), area);
    const QString path = preview.first().first().toString();
    AVFormatContext* format = nullptr;
    QCOMPARE(avformat_open_input(&format, QFile::encodeName(path).constData(), nullptr, nullptr), 0);
    auto closeFormat = qScopeGuard([&] { avformat_close_input(&format); });
    QVERIFY(avformat_find_stream_info(format, nullptr) >= 0);
    QCOMPARE(format->streams[0]->codecpar->width, QGuiApplication::primaryScreen()->size().width());
    manager.cancelRecording();
    manager.onPreviewClosed(false);
    SnapTray::RecordingSession::discard(path);
}

void TestLinuxRecordingPrototype::captureContinuesWhileGuiBlocked()
{
    if (qEnvironmentVariable("SNAPTRAY_TEST_ISOLATED_X11") != "1") {
        QSKIP("Run through CTest's isolated Xvfb wrapper.");
    }
    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);
    X11CaptureEngine engine;
    QVERIFY(engine.setRegion(screen->geometry(), screen));
    engine.setFrameRate(30);
    std::mutex mutex;
    QList<qint64> times;
    QList<quint64> sequences;
    // Frames arrive on the capture thread; nothing here needs the GUI thread.
    QObject::connect(&engine, &ICaptureEngine::frameCaptured, &engine,
                     [&](const SnapTray::CapturedFrame& frame) {
        std::lock_guard<std::mutex> lock(mutex);
        times.append(frame.sourceTimeNs);
        sequences.append(frame.sequence);
    }, Qt::DirectConnection);
    QVERIFY(engine.start());
    const qint64 blockStart = SnapTray::captureClockNowNs();
    QThread::msleep(600);  // the GUI thread is busy; no events are processed
    const qint64 blockEnd = SnapTray::captureClockNowNs();
    engine.stop();

    std::lock_guard<std::mutex> lock(mutex);
    QList<qint64> duringBlock;
    for (const qint64 t : times) {
        if (t >= blockStart && t <= blockEnd) duringBlock.append(t);
    }
    // Capture kept its cadence without the GUI: about 18 frames in 600 ms.
    QVERIFY2(duringBlock.size() >= 10, qPrintable(QString::number(duringBlock.size())));
    for (int i = 1; i < times.size(); ++i) {
        QVERIFY(times[i] > times[i - 1]);
        QCOMPARE(sequences[i], sequences[i - 1] + 1);
    }
    qint64 longestGap = 0;
    for (int i = 1; i < duringBlock.size(); ++i) {
        longestGap = qMax(longestGap, duringBlock[i] - duringBlock[i - 1]);
    }
    // No stall (four intervals or more) while the GUI was blocked.
    QVERIFY2(longestGap < 4 * 1000000000LL / 30, qPrintable(QString::number(longestGap / 1000000)));
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
