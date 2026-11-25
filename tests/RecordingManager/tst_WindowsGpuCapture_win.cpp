#include <QtTest/QtTest>

#include "capture/D3D11Frame.h"
#include "capture/D3D11FrameComparer.h"
#include "capture/CaptureFrameTiming.h"
#include "capture/DXGICaptureEngine.h"
#include "capture/WGCCaptureEngine.h"
#include "recording/SessionClock.h"
#include "utils/CoordinateHelper.h"

#include <QGuiApplication>
#include <QMutex>
#include <QScreen>
#include <QScopeGuard>
#include <QPainter>
#include <QTimer>
#include <QWidget>

#include <atomic>
#include <thread>

#include <windows.h>
#include <tlhelp32.h>

#include <d3d11.h>
#include <windows.h>

namespace {

constexpr int kFrameRate = 10;
constexpr int kFrameTimeoutMs = 5000;
constexpr int kPatternSquarePixels = 64;
constexpr int kPatternStepPixels = 13;

class CapturePattern : public QWidget
{
public:
    int step = 0;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::black);
        const int x = (step * kPatternStepPixels) % qMax(1, width() - kPatternSquarePixels);
        painter.fillRect(QRect(x, height() / 2, kPatternSquarePixels, kPatternSquarePixels), Qt::white);
    }
};

// CTest starts the test hidden, and Windows applies that to the process's
// first ShowWindow: the pattern would never reach the screen, and a capture
// test would only pass while every refresh counted as a new picture (#417).
void putOnScreen(QWidget &widget)
{
    ShowWindow(reinterpret_cast<HWND>(widget.winId()), SW_SHOW);
}

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

int processThreadCount()
{
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return -1;
    int count = 0;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID == GetCurrentProcessId()) ++count;
    }
    CloseHandle(snapshot);
    return count;
}

} // namespace

class TestWindowsGpuCapture : public QObject
{
    Q_OBJECT

private slots:
    void testGraphicsCaptureDeliversFramesOfTheRegion();
    void testGraphicsCaptureKeepsAStaticScreenAlive();
    void testGraphicsCaptureChangingAndStaticFramesHaveNoDuplicates();
    void testGraphicsCaptureSendsOnlyChangedPictures();
    void testFrameComparerCountsChangedPixels();
    void testGraphicsCaptureNativeFramesStayOnItsDevice();
    void testGraphicsCaptureDeliversNothingAfterStop();
    void testGraphicsCaptureStoppedFromGuiThreadReleasesSession();
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

void TestWindowsGpuCapture::testGraphicsCaptureKeepsAStaticScreenAlive()
{
    // #337 replaces #152's nominal-rate repeats with a settling frame and
    // sparse keep-alives. A static screen must remain live without converting
    // every nominal interval. Cover the desktop with a non-animated fixture.
    if (!WGCCaptureEngine::isAvailable()) QSKIP("Windows Graphics Capture is not supported here");
    auto* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);
    CapturePattern pattern;
    pattern.setWindowFlag(Qt::WindowStaysOnTopHint);
    pattern.setGeometry(screen->geometry());
    pattern.showFullScreen();
    putOnScreen(pattern);
    QVERIFY(QTest::qWaitForWindowExposed(&pattern));
    WGCCaptureEngine engine;
    QVERIFY(configure(engine));
    FrameSink sink;
    sink.connectTo(engine);
    QVERIFY2(engine.start(), qPrintable(engine.lastStartError()));
    const auto stopCapture = qScopeGuard([&] { engine.stop(); });
    QTRY_VERIFY_WITH_TIMEOUT(sink.count() >= 1, kFrameTimeoutMs);
    const auto initial = sink.take();
    const qint64 observedAt = SnapTray::captureClockNowNs();
    constexpr qint64 kTimerSlackMs = 500;
    constexpr qint64 kNsPerMs = 1000000;
    QTest::qWait(int(SnapTray::kCaptureKeepAliveNs / kNsPerMs + kTimerSlackMs));
    engine.stop();
    const auto frames = sink.take();
    QVERIFY(!frames.isEmpty());
    QVERIFY(frames.back().sourceTimeNs >= observedAt + SnapTray::kCaptureKeepAliveNs - kTimerSlackMs * kNsPerMs);
    int newPictures = 0, repeats = 0;
    auto previousSequence = initial.back().sequence;
    qint64 previousTime = initial.back().sourceTimeNs;
    for (const auto& frame : frames) {
        QVERIFY(frame.sourceTimeNs > previousTime);
        if (frame.sequence == previousSequence) ++repeats; else ++newPictures;
        previousSequence = frame.sequence;
        previousTime = frame.sourceTimeNs;
    }
    // Desktop/system changes can still arrive. Allow one settling repeat for
    // each new picture, plus the bounded idle keep-alives, not a full-rate loop.
    const qint64 keepAliveAllowance = (SnapTray::captureClockNowNs() - observedAt)
        / SnapTray::kCaptureKeepAliveNs + 2;
    QVERIFY2(repeats <= newPictures + keepAliveAllowance,
             qPrintable(QStringLiteral("%1 repeats for %2 new pictures").arg(repeats).arg(newPictures)));
}

void TestWindowsGpuCapture::testGraphicsCaptureChangingAndStaticFramesHaveNoDuplicates()
{
    if (!WGCCaptureEngine::isAvailable()) QSKIP("Windows Graphics Capture is not supported here");
    auto* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);
    CapturePattern pattern;
    pattern.setWindowFlag(Qt::WindowStaysOnTopHint);
    pattern.setGeometry(screen->geometry());
    pattern.showFullScreen();
    putOnScreen(pattern);
    QVERIFY(QTest::qWaitForWindowExposed(&pattern));

    WGCCaptureEngine engine;
    constexpr int kPatternFrameRate = 60;
    constexpr int kDurationMs = 10000;
    constexpr int kPhaseMs = 2000;
    engine.setFrameRate(kPatternFrameRate);
    engine.setNativeFrameOutput(true);
    QVERIFY(engine.setRegion(screen->geometry(), CaptureScreenInfo::fromScreen(screen)));
    SnapTray::SessionClock clock;
    clock.start(SnapTray::captureClockNowNs());
    // Admit on the capture thread, just as RecordingManager does. Do not retain
    // ten seconds of full-screen GPU textures in the test process.
    QObject::connect(&engine, &ICaptureEngine::frameCaptured, &engine,
                     [&clock](const SnapTray::CapturedFrame& frame) {
        clock.admit(frame.sourceTimeNs, nullptr);
    }, Qt::DirectConnection);
    QVERIFY2(engine.start(), qPrintable(engine.lastStartError()));

    QElapsedTimer elapsed;
    elapsed.start();
    QTimer animation;
    QObject::connect(&animation, &QTimer::timeout, &pattern, [&] {
        if ((elapsed.elapsed() / kPhaseMs) % 2 == 0) {
            ++pattern.step;
            pattern.update();
        }
    });
    animation.start(1000 / kPatternFrameRate);
    QTest::qWait(kDurationMs);
    engine.stop();
    const auto stats = clock.stats();
    qInfo() << "10s WGC changing/static capture: accepted" << stats.accepted
            << "duplicates" << stats.duplicates << "backwards" << stats.backwards;
    QVERIFY(stats.accepted >= kPatternFrameRate);
    QCOMPARE(stats.duplicates, qint64(0));
    QCOMPARE(stats.backwards, qint64(0));
}

void TestWindowsGpuCapture::testGraphicsCaptureSendsOnlyChangedPictures()
{
    // Windows 10 delivers a WGC frame on every refresh even when nothing on
    // the screen changed. Those must not reach the encoder as new pictures:
    // a still screen is kept alive by the sparse repeats alone (#417).
    if (!WGCCaptureEngine::isAvailable()) QSKIP("Windows Graphics Capture is not supported here");
    auto* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);
    // On top of everything, so that nothing else on the desktop (a terminal
    // printing test output) changes the captured picture.
    CapturePattern pattern;
    pattern.setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    pattern.setGeometry(screen->geometry());
    pattern.show();
    putOnScreen(pattern);
    QVERIFY(QTest::qWaitForWindowExposed(&pattern));
    QVERIFY(QTest::qWaitForWindowExposed(&pattern));

    WGCCaptureEngine engine;
    constexpr int kRate = 60;
    engine.setFrameRate(kRate);
    engine.setNativeFrameOutput(true);
    QVERIFY(engine.setRegion(screen->geometry(), CaptureScreenInfo::fromScreen(screen)));
    FrameSink sink;
    sink.connectTo(engine);
    QVERIFY2(engine.start(), qPrintable(engine.lastStartError()));
    const auto stopCapture = qScopeGuard([&] { engine.stop(); });
    QTRY_VERIFY_WITH_TIMEOUT(sink.count() >= 1, kFrameTimeoutMs);
    QTest::qWait(500);
    auto lastSequence = sink.take().back().sequence;
    // A repeat carries the sequence of the picture it repeats.
    const auto countNewPictures = [&lastSequence](const QList<SnapTray::CapturedFrame>& frames) {
        int pictures = 0;
        for (const auto& frame : frames) {
            if (frame.sequence != lastSequence) ++pictures;
            lastSequence = frame.sequence;
        }
        return pictures;
    };

    constexpr int kStillMs = 3000;
    QTest::qWait(kStillMs);
    const int stillPictures = countNewPictures(sink.take());
    // At 60 fps three still seconds were 180 pictures; allow a few for
    // whatever else the desktop draws (the 80% target of #337 is 36).
    QVERIFY2(stillPictures <= 6, qPrintable(QStringLiteral("%1 new pictures of a still screen").arg(stillPictures)));

    // The first change after the still span is sent at once.
    ++pattern.step;
    pattern.update();
    // QTRY re-evaluates its expression: keep the taking out of it.
    int changedPictures = 0;
    QTRY_VERIFY_WITH_TIMEOUT((changedPictures += countNewPictures(sink.take())) > 0, 1000);
}

void TestWindowsGpuCapture::testFrameComparerCountsChangedPixels()
{
    const auto device = SnapTray::D3D11FrameDevice::create();
    if (!device) QSKIP("No D3D11 device");
    auto comparer = SnapTray::D3D11FrameComparer::create(device->device.Get(), device->context.Get());
    QVERIFY(comparer);

    constexpr UINT kWidth = 257;  // odd sizes: no assumption about blocks
    constexpr UINT kHeight = 131;
    QImage picture(int(kWidth), int(kHeight), QImage::Format_ARGB32);
    picture.fill(QColor(30, 60, 90));
    auto makeTexture = [&](const QImage& image) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = UINT(image.width());
        desc.Height = UINT(image.height());
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{image.constBits(), UINT(image.bytesPerLine()), 0};
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        device->device->CreateTexture2D(&desc, &data, &texture);
        return texture;
    };
    const auto first = makeTexture(picture);
    QVERIFY(first);
    QCOMPARE(comparer->differingPixels(first.Get(), makeTexture(picture).Get()), std::optional<quint64>(0));

    // One channel of one pixel, one step: a blinking text caret is no larger.
    QImage onePixel = picture;
    onePixel.setPixel(kWidth - 1, kHeight - 1, qRgb(30, 60, 91));
    QCOMPARE(comparer->differingPixels(first.Get(), makeTexture(onePixel).Get()), std::optional<quint64>(1));

    QImage tenPixels = picture;
    for (int i = 0; i < 10; ++i) tenPixels.setPixel(i * 20, i * 10, qRgb(255, 255, 255));
    QCOMPARE(comparer->differingPixels(first.Get(), makeTexture(tenPixels).Get()), std::optional<quint64>(10));

    QImage allPixels = picture;
    allPixels.fill(QColor(90, 60, 30));
    QCOMPARE(comparer->differingPixels(first.Get(), makeTexture(allPixels).Get()),
             std::optional<quint64>(quint64(kWidth) * kHeight));

    const auto smaller = makeTexture(picture.copy(0, 0, int(kWidth) - 1, int(kHeight)));
    QVERIFY(!comparer->differingPixels(first.Get(), smaller.Get()).has_value());

    // Comparing pictures that differ, again and again, keeps the device:
    // the earlier pass hung Intel UHD 620 on the second one (#420).
    const auto changed = makeTexture(onePixel);
    for (int i = 0; i < 20; ++i) {
        QCOMPARE(comparer->differingPixels(first.Get(), changed.Get()), std::optional<quint64>(1));
    }
    QVERIFY(!device->isLost());
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

void TestWindowsGpuCapture::testGraphicsCaptureStoppedFromGuiThreadReleasesSession()
{
    if (!WGCCaptureEngine::isAvailable()) QSKIP("Windows Graphics Capture is not supported here");
    // A recording starts the engine on its init worker (MTA) and stops it on
    // the GUI thread (STA). Stopping there failed with RPC_E_WRONG_THREAD and
    // left every session's frame pool and D3D device behind (#416).
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (com == RPC_E_CHANGED_MODE) QSKIP("The test thread is already in the MTA");
    const auto uninitialize = qScopeGuard([com] { if (SUCCEEDED(com)) CoUninitialize(); });
    QTest::failOnWarning(QRegularExpression(QStringLiteral("cleanup failed")));

    WGCCaptureEngine engine;
    QVERIFY(configure(engine));
    FrameSink sink;
    sink.connectTo(engine);
    constexpr int kRounds = 5;
    int threadsAfterFirstRound = 0;
    for (int round = 0; round < kRounds; ++round) {
        bool started = false;
        QString startError;
        std::thread init([&] {
            started = engine.start();
            startError = engine.lastStartError();
        });
        init.join();
        QVERIFY2(started, qPrintable(startError));
        QTRY_VERIFY_WITH_TIMEOUT(sink.count() >= 1, kFrameTimeoutMs);
        engine.stop();
        QVERIFY(!engine.isRunning());
        sink.take();
        QTest::qWait(300);
        if (round == 0) threadsAfterFirstRound = processThreadCount();
    }
    // A leaked session keeps its frame pool's present thread (and the
    // driver's threads for its device): the count would grow every round.
    const int threadsAtEnd = processThreadCount();
    QVERIFY2(threadsAtEnd <= threadsAfterFirstRound + 2,
             qPrintable(QStringLiteral("threads after round 1: %1, after round %2: %3")
                            .arg(threadsAfterFirstRound).arg(kRounds).arg(threadsAtEnd)));
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
