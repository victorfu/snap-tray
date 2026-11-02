#include <QtTest/QtTest>

#include "capture/D3D11Frame.h"
#include "capture/DXGICaptureEngine.h"
#include "capture/WGCCaptureEngine.h"
#include "utils/CoordinateHelper.h"

#include <QGuiApplication>
#include <QMutex>
#include <QScreen>

#include <atomic>

namespace {

constexpr int kFrameRate = 10;
constexpr int kFrameTimeoutMs = 5000;

// Collects frames delivered on the capture thread.
struct FrameSink
{
    QMutex mutex;
    QList<SnapTray::CapturedFrame> frames;

    void connectTo(ICaptureEngine &engine)
    {
        QObject::connect(&engine, &ICaptureEngine::frameCaptured, &engine,
                         [this](const SnapTray::CapturedFrame &frame) {
                             QMutexLocker lock(&mutex);
                             frames.append(frame);
                         }, Qt::DirectConnection);
    }
    int count()
    {
        QMutexLocker lock(&mutex);
        return frames.size();
    }
    QList<SnapTray::CapturedFrame> take()
    {
        QMutexLocker lock(&mutex);
        return std::exchange(frames, {});
    }
};

} // namespace

class TestWindowsGpuCapture : public QObject
{
    Q_OBJECT

private slots:
    void testGraphicsCaptureDeliversFramesOfTheRegion();
    void testGraphicsCaptureRepeatsAStaticScreen();
    void testGraphicsCaptureNativeFramesStayOnItsDevice();
    void testGraphicsCaptureDeliversNothingAfterStop();
    void testGraphicsCaptureUnknownDisplayFailsToStart();
    void testDesktopDuplicationNativeFramesStayOnItsDevice();

private:
    // A small region at the primary screen's top-left; false if no screen.
    bool configure(ICaptureEngine &engine, QSize *pixelSize = nullptr) const;
};

bool TestWindowsGpuCapture::configure(ICaptureEngine &engine, QSize *pixelSize) const
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen || screen->geometry().isEmpty()) {
        return false;
    }
    const QRect region(screen->geometry().topLeft(), QSize(160, 120));
    engine.setFrameRate(kFrameRate);
    const CaptureScreenInfo info = CaptureScreenInfo::fromScreen(screen);
    if (pixelSize) {
        *pixelSize = CoordinateHelper::toPhysicalScreenRect(region, info.geometry, info.physicalGeometry,
                                                            info.devicePixelRatio).size();
    }
    return engine.setRegion(region, info);
}

void TestWindowsGpuCapture::testGraphicsCaptureDeliversFramesOfTheRegion()
{
    if (!WGCCaptureEngine::isAvailable()) QSKIP("Windows Graphics Capture is not supported here");
    WGCCaptureEngine engine;
    QSize pixelSize;
    QVERIFY(configure(engine, &pixelSize));
    FrameSink sink;
    sink.connectTo(engine);
    QVERIFY2(engine.start(), qPrintable(engine.lastStartError()));
    QVERIFY(engine.deliversFrames());
    QTRY_VERIFY_WITH_TIMEOUT(sink.count() >= 3, kFrameTimeoutMs);
    engine.stop();

    const auto frames = sink.take();
    qint64 previousSequence = 0;
    for (const auto &frame : frames) {
        QVERIFY(!frame.image.isNull());
        QVERIFY(!frame.nativeBuffer);
        QCOMPARE(frame.pixelSize, pixelSize);
        QCOMPARE(frame.image.size(), pixelSize);
        QVERIFY(frame.sourceTimeNs > 0);
        QVERIFY(frame.sourceTimeNs <= SnapTray::captureClockNowNs());
        QVERIFY(qint64(frame.sequence) >= previousSequence);
        previousSequence = qint64(frame.sequence);
    }
}

void TestWindowsGpuCapture::testGraphicsCaptureRepeatsAStaticScreen()
{
    // WGC only delivers on change. A still screen must keep the cadence
    // instead of reading as a capture stall (#152).
    if (!WGCCaptureEngine::isAvailable()) QSKIP("Windows Graphics Capture is not supported here");
    WGCCaptureEngine engine;
    QVERIFY(configure(engine));
    FrameSink sink;
    sink.connectTo(engine);
    QVERIFY2(engine.start(), qPrintable(engine.lastStartError()));
    QTRY_VERIFY_WITH_TIMEOUT(sink.count() >= 1, kFrameTimeoutMs);
    sink.take();
    QTest::qWait(1000);
    engine.stop();
    const auto frames = sink.take();
    // 10 fps for a second; allow for timer slack.
    QVERIFY2(frames.size() >= kFrameRate / 2, qPrintable(QString::number(frames.size())));
    for (int i = 1; i < frames.size(); ++i) {
        QVERIFY(frames[i].sourceTimeNs >= frames[i - 1].sourceTimeNs);
    }
}

void TestWindowsGpuCapture::testGraphicsCaptureNativeFramesStayOnItsDevice()
{
    if (!WGCCaptureEngine::isAvailable()) QSKIP("Windows Graphics Capture is not supported here");
    WGCCaptureEngine engine;
    QSize pixelSize;
    QVERIFY(configure(engine, &pixelSize));
    engine.setNativeFrameOutput(true);
    FrameSink sink;
    sink.connectTo(engine);
    QVERIFY2(engine.start(), qPrintable(engine.lastStartError()));
    const auto device = std::static_pointer_cast<SnapTray::D3D11FrameDevice>(engine.nativeFrameDevice());
    QVERIFY(device);
    QTRY_VERIFY_WITH_TIMEOUT(sink.count() >= 2, kFrameTimeoutMs);
    engine.stop();
    for (const auto &frame : sink.take()) {
        QVERIFY(frame.image.isNull());
        const auto texture = SnapTray::d3d11FrameFrom(frame.nativeBuffer);
        QVERIFY(texture && texture->texture);
        QCOMPARE(texture->size, pixelSize);
        QCOMPARE(texture->device, device);
    }
}

void TestWindowsGpuCapture::testGraphicsCaptureDeliversNothingAfterStop()
{
    if (!WGCCaptureEngine::isAvailable()) QSKIP("Windows Graphics Capture is not supported here");
    WGCCaptureEngine engine;
    QVERIFY(configure(engine));
    FrameSink sink;
    sink.connectTo(engine);
    for (int round = 0; round < 3; ++round) {
        QVERIFY2(engine.start(), qPrintable(engine.lastStartError()));
        QVERIFY(engine.isRunning());
        QTRY_VERIFY_WITH_TIMEOUT(sink.count() >= 1, kFrameTimeoutMs);
        engine.stop();
        QVERIFY(!engine.isRunning());
        const int afterStop = sink.count();
        QTest::qWait(300);
        QCOMPARE(sink.count(), afterStop);
        sink.take();
    }
}

void TestWindowsGpuCapture::testGraphicsCaptureUnknownDisplayFailsToStart()
{
    if (!WGCCaptureEngine::isAvailable()) QSKIP("Windows Graphics Capture is not supported here");
    WGCCaptureEngine engine;
    CaptureScreenInfo info;
    info.name = info.nativeName = QStringLiteral("\\\\.\\DISPLAY99");
    info.geometry = QRect(0, 0, 640, 480);
    QVERIFY(engine.setRegion(QRect(0, 0, 64, 64), info));
    QVERIFY(!engine.start());
    QVERIFY(!engine.isRunning());
    QVERIFY(engine.lastStartError().contains(QStringLiteral("not found")));
    QVERIFY(!engine.nativeFrameDevice());
}

void TestWindowsGpuCapture::testDesktopDuplicationNativeFramesStayOnItsDevice()
{
    DXGICaptureEngine engine;
    QSize pixelSize;
    QVERIFY(configure(engine, &pixelSize));
    engine.setNativeFrameOutput(true);
    QVERIFY(engine.start());
    const auto device = std::static_pointer_cast<SnapTray::D3D11FrameDevice>(engine.nativeFrameDevice());
    if (!device) {
        engine.stop();
        QSKIP("Desktop Duplication unavailable (BitBlt fallback produces CPU frames)");
    }
    // The first frames can still be CPU copies from before the duplication
    // delivered; wait for a GPU one.
    SnapTray::CapturedFrame frame;
    QTRY_VERIFY_WITH_TIMEOUT((frame = engine.captureNativeFrame()).nativeBuffer != nullptr, kFrameTimeoutMs);
    engine.stop();
    QVERIFY(frame.image.isNull());
    QCOMPARE(frame.pixelSize, pixelSize);
    const auto texture = SnapTray::d3d11FrameFrom(frame.nativeBuffer);
    QVERIFY(texture && texture->texture);
    QCOMPARE(texture->size, pixelSize);
    QCOMPARE(texture->device, device);
}

QTEST_MAIN(TestWindowsGpuCapture)
#include "tst_WindowsGpuCapture_win.moc"
