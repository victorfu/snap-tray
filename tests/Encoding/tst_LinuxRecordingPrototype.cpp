#include "IVideoEncoder.h"
#include "encoding/FFmpegEncoder.h"
#include "encoding/EncodingWorker.h"
#include "qml/SettingsBackend.h"
#include <QtQml/qqmlextensionplugin.h>
#include <memory>
#include <QtTest>
#include <QElapsedTimer>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QWidget>

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

extern "C" {
#include <libavformat/avformat.h>
}

class TestLinuxRecordingPrototype : public QObject
{
    Q_OBJECT
private slots:
    void captureFullScreen();
    void productionRecordingStaysDisabled();
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
    auto* encoder = new FFmpegEncoder;
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

void TestLinuxRecordingPrototype::productionRecordingStaysDisabled()
{
    if (qEnvironmentVariable("SNAPTRAY_TEST_ISOLATED_X11") != "1") {
        QSKIP("Run through CTest's isolated Xvfb wrapper.");
    }
    SnapTray::SettingsBackend backend;
    QVERIFY(!backend.recordingSupported());
    QVERIFY(!backend.recordingDirectMp4Only());
    std::unique_ptr<IVideoEncoder> native(IVideoEncoder::createNativeEncoder());
    QVERIFY(!native);
}

QTEST_MAIN(TestLinuxRecordingPrototype)
#include "tst_LinuxRecordingPrototype.moc"
