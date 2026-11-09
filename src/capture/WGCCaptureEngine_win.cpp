#include "capture/WGCCaptureEngine.h"
#include "capture/CaptureFrameTiming.h"
#include "capture/D3D11Frame.h"
#include "utils/CoordinateHelper.h"

#include <QDebug>
#include <QScreen>

#include <unknwn.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#pragma comment(lib, "windowsapp")

using Microsoft::WRL::ComPtr;
namespace wgc = winrt::Windows::Graphics::Capture;
namespace wdx = winrt::Windows::Graphics::DirectX;

namespace {

// Two buffers: one being captured into while SnapTray copies the other.
constexpr int kFramePoolBuffers = 2;
constexpr wchar_t kSessionClass[] = L"Windows.Graphics.Capture.GraphicsCaptureSession";

struct MonitorLookup
{
    QString deviceName;
    HMONITOR monitor = nullptr;
    QRect physicalGeometry;
};

BOOL CALLBACK findMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM context)
{
    auto *lookup = reinterpret_cast<MonitorLookup *>(context);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, reinterpret_cast<LPMONITORINFO>(&info))
        || QString::fromWCharArray(info.szDevice).compare(lookup->deviceName, Qt::CaseInsensitive) != 0) {
        return TRUE;
    }
    lookup->monitor = monitor;
    lookup->physicalGeometry = QRect(info.rcMonitor.left, info.rcMonitor.top,
                                     info.rcMonitor.right - info.rcMonitor.left,
                                     info.rcMonitor.bottom - info.rcMonitor.top);
    return FALSE;
}

// Frame times are QPC ticks (100 ns units); convert them to the steady capture
// clock by their distance from now, whatever clock steady_clock is built on.
qint64 qpcTicksToCaptureClockNs(qint64 frameTicks100ns)
{
    LARGE_INTEGER counter{};
    LARGE_INTEGER frequency{};
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    const qint64 nowNs = SnapTray::captureClockNowNs();
    const qint64 qpcNowNs = qint64(double(counter.QuadPart) * 1e9 / double(frequency.QuadPart));
    const qint64 ageNs = qMax<qint64>(0, qpcNowNs - frameTicks100ns * 100);
    return nowNs - ageNs;
}

// Keeps the process MTA alive while WinRT objects exist, so any thread
// (the recording init worker, thread-pool callbacks) can use them.
class MtaUsage
{
public:
    MtaUsage() { m_ok = SUCCEEDED(CoIncrementMTAUsage(&m_cookie)); }
    ~MtaUsage() { if (m_ok) CoDecrementMTAUsage(m_cookie); }
    MtaUsage(const MtaUsage&) = delete;
    MtaUsage& operator=(const MtaUsage&) = delete;

private:
    CO_MTA_USAGE_COOKIE m_cookie = nullptr;
    bool m_ok = false;
};

QString winrtErrorText(const winrt::hresult_error &error)
{
    return QStringLiteral("%1 (0x%2)")
        .arg(QString::fromWCharArray(error.message().c_str()))
        .arg(quint32(error.code().value), 8, 16, QChar('0'));
}

} // namespace

class WGCCaptureEngine::Private : public std::enable_shared_from_this<Private>
{
public:
    explicit Private(WGCCaptureEngine *engine) : q(engine) {}

    bool start(QString *error);
    void stop();
    void onFrameArrived(quint64 generationAtStart);
    void onItemClosed(quint64 generationAtStart);
    void repeatLoop(quint64 generationAtStart);
    SnapTray::CapturedFrame copyFrame(ID3D11Texture2D *surface, const QSize &contentSize, qint64 sourceTimeNs);
    void emitFrame(const SnapTray::CapturedFrame &frame, quint64 generationAtStart);
    void emitFrameLocked(const SnapTray::CapturedFrame &frame, quint64 generationAtStart);  // emitMutex held
    void emitError(const QString &message, quint64 generationAtStart);

    WGCCaptureEngine *q;
    QRect captureRegion;
    CaptureScreenInfo screenInfo;
    std::atomic<int> frameRate{30};
    std::atomic<bool> nativeOutput{false};
    std::atomic<bool> running{false};
    std::atomic<quint64> generation{0};
    bool cursorInFrames = true;
    bool borderRequired = true;
    QString lastStartError;

    // Guarded by mutex: WinRT objects, frame copies and the latest frame.
    std::mutex mutex;
    std::unique_ptr<MtaUsage> mta;
    QRect monitorGeometry;
    std::shared_ptr<SnapTray::D3D11FrameDevice> frameDevice;
    wdx::Direct3D11::IDirect3DDevice d3dDevice{nullptr};
    wgc::GraphicsCaptureItem item{nullptr};
    wgc::Direct3D11CaptureFramePool framePool{nullptr};
    wgc::GraphicsCaptureSession session{nullptr};
    winrt::event_token frameArrivedToken{};
    winrt::event_token closedToken{};
    winrt::Windows::Graphics::SizeInt32 poolSize{};
    std::shared_ptr<SnapTray::D3D11TexturePool> texturePool;
    ComPtr<ID3D11Texture2D> staging;
    QSize stagingSize;
    SnapTray::CapturedFrame latest;
    quint64 sequence = 0;
    bool reportedFailure = false;

    // Serializes signal emission with stop(): once stop() has passed it, no
    // frame or error of the stopped session reaches the receivers.
    std::mutex emitMutex;
    qint64 lastEmittedNs = -1;  // guarded by emitMutex

    std::thread repeater;
    std::mutex repeaterMutex;
    std::condition_variable repeaterWake;
};

bool WGCCaptureEngine::Private::start(QString *error)
{
    const QString deviceName = screenInfo.nativeName.trimmed().isEmpty()
        ? screenInfo.name.trimmed() : screenInfo.nativeName.trimmed();
    MonitorLookup lookup;
    lookup.deviceName = deviceName;
    EnumDisplayMonitors(nullptr, nullptr, findMonitor, reinterpret_cast<LPARAM>(&lookup));
    if (!lookup.monitor || lookup.physicalGeometry.isEmpty()) {
        *error = QStringLiteral("display %1 not found").arg(deviceName);
        return false;
    }
    if (!screenInfo.physicalGeometry.isEmpty() && screenInfo.physicalGeometry != lookup.physicalGeometry) {
        *error = QStringLiteral("display geometry changed during initialization");
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex);
    mta = std::make_unique<MtaUsage>();
    monitorGeometry = lookup.physicalGeometry;
    frameDevice = SnapTray::D3D11FrameDevice::create();
    if (!frameDevice) {
        *error = QStringLiteral("cannot create a D3D11 device");
        return false;
    }

    try {
        ComPtr<IDXGIDevice> dxgiDevice;
        winrt::check_hresult(frameDevice->device.As(&dxgiDevice));
        winrt::com_ptr<::IInspectable> inspectable;
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), inspectable.put()));
        d3dDevice = inspectable.as<wdx::Direct3D11::IDirect3DDevice>();

        auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        winrt::check_hresult(interop->CreateForMonitor(
            lookup.monitor, winrt::guid_of<wgc::GraphicsCaptureItem>(), winrt::put_abi(item)));

        poolSize = item.Size();
        framePool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
            d3dDevice, wdx::DirectXPixelFormat::B8G8R8A8UIntNormalized, kFramePoolBuffers, poolSize);
        session = framePool.CreateCaptureSession(item);

        using winrt::Windows::Foundation::Metadata::ApiInformation;
        cursorInFrames = true;
        if (ApiInformation::IsPropertyPresent(kSessionClass, L"IsCursorCaptureEnabled")) {
            session.IsCursorCaptureEnabled(false);
            cursorInFrames = session.IsCursorCaptureEnabled();
        }
        borderRequired = true;
        if (ApiInformation::IsPropertyPresent(kSessionClass, L"IsBorderRequired")) {
            try {
                session.IsBorderRequired(false);
                borderRequired = session.IsBorderRequired();
            } catch (const winrt::hresult_error &borderError) {
                // Not allowed for this process: the border stays, recording does not depend on it.
                qDebug() << "WGCCaptureEngine: capture border stays:" << winrtErrorText(borderError);
            }
        }
        if (ApiInformation::IsPropertyPresent(kSessionClass, L"MinUpdateInterval")) {
            const auto interval = SnapTray::captureFrameInterval(frameRate.load());
            session.MinUpdateInterval(std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(interval));
        }

        const quint64 startGeneration = generation.load();
        std::weak_ptr<Private> weak = shared_from_this();
        frameArrivedToken = framePool.FrameArrived([weak, startGeneration](auto &&, auto &&) {
            if (auto self = weak.lock()) self->onFrameArrived(startGeneration);
        });
        closedToken = item.Closed([weak, startGeneration](auto &&, auto &&) {
            if (auto self = weak.lock()) self->onItemClosed(startGeneration);
        });

        running = true;
        session.StartCapture();
    } catch (const winrt::hresult_error &startError) {
        running = false;
        *error = winrtErrorText(startError);
        return false;
    }

    const quint64 startGeneration = generation.load();
    repeater = std::thread([self = shared_from_this(), startGeneration] { self->repeatLoop(startGeneration); });
    return true;
}

void WGCCaptureEngine::Private::stop()
{
    ++generation;
    running = false;
    {
        std::lock_guard<std::mutex> lock(repeaterMutex);
        repeaterWake.notify_all();
    }
    if (repeater.joinable() && repeater.get_id() != std::this_thread::get_id()) {
        repeater.join();
    }

    // Detach the WinRT objects under the lock, then close them without it: a
    // FrameArrived handler waiting for the lock must not be waited on by Close().
    wgc::Direct3D11CaptureFramePool closingPool{nullptr};
    wgc::GraphicsCaptureSession closingSession{nullptr};
    wgc::GraphicsCaptureItem closingItem{nullptr};
    std::unique_ptr<MtaUsage> closingMta;
    {
        std::lock_guard<std::mutex> lock(mutex);
        closingPool = std::exchange(framePool, nullptr);
        closingSession = std::exchange(session, nullptr);
        closingItem = std::exchange(item, nullptr);
        closingMta = std::move(mta);
        d3dDevice = nullptr;
        texturePool.reset();
        staging.Reset();
        stagingSize = QSize();
        latest = {};
        frameDevice.reset();
    }
    try {
        if (closingPool) closingPool.FrameArrived(frameArrivedToken);
        if (closingItem) closingItem.Closed(closedToken);
        if (closingSession) closingSession.Close();
        if (closingPool) closingPool.Close();
    } catch (const winrt::hresult_error &stopError) {
        qWarning() << "WGCCaptureEngine: cleanup failed:" << winrtErrorText(stopError);
    }
    closingSession = nullptr;
    closingPool = nullptr;
    closingItem = nullptr;
    closingMta.reset();
    // Wait out a frame or error being delivered right now.
    std::lock_guard<std::mutex> emitLock(emitMutex);
    lastEmittedNs = -1;
}

SnapTray::CapturedFrame WGCCaptureEngine::Private::copyFrame(ID3D11Texture2D *surface, const QSize &contentSize,
                                                             qint64 sourceTimeNs)
{
    // Logical capture region -> pixels of this monitor's frame.
    const QRect physRegion = CoordinateHelper::toPhysicalScreenRect(
        captureRegion, screenInfo.geometry, monitorGeometry, screenInfo.devicePixelRatio);
    const QRect region = physRegion.translated(-monitorGeometry.topLeft()).intersected(QRect(QPoint(), contentSize));
    if (region.isEmpty()) {
        return {};
    }
    const D3D11_BOX box{UINT(region.left()), UINT(region.top()), 0,
                        UINT(region.right() + 1), UINT(region.bottom() + 1), 1};

    SnapTray::CapturedFrame frame;
    frame.sourceTimeNs = sourceTimeNs;
    frame.pixelSize = region.size();
    frame.generation = generation.load();
    frame.sequence = ++sequence;
    ID3D11DeviceContext *context = frameDevice->context.Get();

    if (nativeOutput) {
        if (!texturePool || texturePool->size() != region.size()) {
            texturePool = SnapTray::D3D11TexturePool::create(frameDevice, region.size());
        }
        if (auto texture = texturePool ? texturePool->acquire() : nullptr) {
            context->CopySubresourceRegion(texture->texture.Get(), 0, 0, 0, 0, surface, 0, &box);
            frame.nativeBuffer = texture;
            return frame;
        }
        // Out of GPU memory for another frame: this one takes the CPU copy.
    }

    if (!staging || stagingSize != region.size()) {
        staging.Reset();
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = UINT(region.width());
        desc.Height = UINT(region.height());
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(frameDevice->device->CreateTexture2D(&desc, nullptr, &staging))) {
            return {};
        }
        stagingSize = region.size();
    }
    context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, surface, 0, &box);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return {};
    }
    frame.image = QImage(static_cast<const uchar *>(mapped.pData), region.width(), region.height(),
                         qsizetype(mapped.RowPitch), QImage::Format_ARGB32).copy();
    context->Unmap(staging.Get(), 0);
    return frame;
}

void WGCCaptureEngine::Private::onFrameArrived(quint64 generationAtStart)
{
    SnapTray::CapturedFrame captured;
    QString failure;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!running || generation != generationAtStart || !framePool) {
            return;
        }
        try {
            auto frame = framePool.TryGetNextFrame();
            if (!frame) {
                return;
            }
            const auto contentSize = frame.ContentSize();
            if (contentSize.Width != poolSize.Width || contentSize.Height != poolSize.Height) {
                // The monitor changed size: frames of the old pool size are
                // dropped and the pool rebuilt for the new one.
                frame.Close();
                poolSize = contentSize;
                framePool.Recreate(d3dDevice, wdx::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                                   kFramePoolBuffers, poolSize);
                return;
            }
            ComPtr<ID3D11Texture2D> surface;
            auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
            winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(&surface)));
            const qint64 sourceTimeNs = qpcTicksToCaptureClockNs(frame.SystemRelativeTime().count());
            captured = copyFrame(surface.Get(), QSize(contentSize.Width, contentSize.Height), sourceTimeNs);
            frame.Close();
        } catch (const winrt::hresult_error &frameError) {
            failure = winrtErrorText(frameError);
        }
        if (failure.isEmpty() && frameDevice->isLost()) {
            failure = QStringLiteral("the capture device was lost (0x%1)")
                          .arg(quint32(frameDevice->device->GetDeviceRemovedReason()), 8, 16, QChar('0'));
        }
        if (failure.isEmpty() && captured.isValid()) {
            latest = captured;
        }
    }
    if (!failure.isEmpty()) {
        emitError(QStringLiteral("Windows Graphics Capture failed: %1").arg(failure), generationAtStart);
        return;
    }
    if (captured.isValid()) {
        emitFrame(captured, generationAtStart);
    }
}

void WGCCaptureEngine::Private::onItemClosed(quint64 generationAtStart)
{
    emitError(QStringLiteral("The captured display is no longer available"), generationAtStart);
}

void WGCCaptureEngine::Private::repeatLoop(quint64 generationAtStart)
{
    // Windows Graphics Capture delivers nothing while the screen is static.
    // Repeat the last frame once a frame interval passes without one, so a
    // still screen keeps the recording's cadence instead of reading as a stall.
    std::unique_lock<std::mutex> wakeLock(repeaterMutex);
    while (running && generation == generationAtStart) {
        const auto interval = SnapTray::captureFrameInterval(frameRate.load());
        repeaterWake.wait_for(wakeLock, interval);
        if (!running || generation != generationAtStart) {
            break;
        }
        // Check and emit under one emitMutex hold, re-reading the latest frame
        // there, so a real frame emitted meanwhile is never followed by an
        // older one with a newer time.
        std::lock_guard<std::mutex> emitLock(emitMutex);
        const qint64 nowNs = SnapTray::captureClockNowNs();
        if (lastEmittedNs >= 0 && nowNs - lastEmittedNs < interval.count()) {
            continue;
        }
        SnapTray::CapturedFrame repeat;
        {
            std::lock_guard<std::mutex> lock(mutex);
            repeat = latest;
        }
        if (!repeat.isValid()) {
            continue;
        }
        repeat.sourceTimeNs = nowNs;
        emitFrameLocked(repeat, generationAtStart);
    }
}

void WGCCaptureEngine::Private::emitFrame(const SnapTray::CapturedFrame &frame, quint64 generationAtStart)
{
    std::lock_guard<std::mutex> lock(emitMutex);
    emitFrameLocked(frame, generationAtStart);
}

void WGCCaptureEngine::Private::emitFrameLocked(const SnapTray::CapturedFrame &frame, quint64 generationAtStart)
{
    if (!running || generation != generationAtStart) {
        return;
    }
    // A real frame carries the time it was composed and can arrive after a
    // repeat stamped "now" already went out. Its picture is newer than the
    // repeat's, so it is kept, but never with an earlier time: frame times
    // only move forward.
    if (lastEmittedNs >= 0 && frame.sourceTimeNs < lastEmittedNs) {
        SnapTray::CapturedFrame stamped = frame;
        stamped.sourceTimeNs = lastEmittedNs;
        emit q->frameCaptured(stamped);
        return;
    }
    lastEmittedNs = frame.sourceTimeNs;
    emit q->frameCaptured(frame);
}

void WGCCaptureEngine::Private::emitError(const QString &message, quint64 generationAtStart)
{
    std::lock_guard<std::mutex> lock(emitMutex);
    if (!running || generation != generationAtStart || reportedFailure) {
        return;
    }
    reportedFailure = true;
    qWarning() << "WGCCaptureEngine:" << message;
    emit q->error(message);
}

WGCCaptureEngine::WGCCaptureEngine(QObject *parent)
    : ICaptureEngine(parent)
    , d(std::make_shared<Private>(this))
{
}

WGCCaptureEngine::~WGCCaptureEngine()
{
    stop();
}

bool WGCCaptureEngine::isAvailable()
{
    static const bool available = [] {
        MtaUsage mta;
        try {
            return wgc::GraphicsCaptureSession::IsSupported();
        } catch (const winrt::hresult_error &error) {
            qDebug() << "WGCCaptureEngine: not supported:" << winrtErrorText(error);
            return false;
        }
    }();
    return available;
}

bool WGCCaptureEngine::setRegion(const QRect &region, QScreen *screen)
{
    if (!screen) {
        emit error("Invalid screen");
        return false;
    }
    return setRegion(region, CaptureScreenInfo::fromScreen(screen));
}

bool WGCCaptureEngine::setRegion(const QRect &region, const CaptureScreenInfo &screenInfo)
{
    if (!screenInfo.isValid()) {
        emit error("Invalid screen metadata");
        return false;
    }
    d->captureRegion = region;
    d->screenInfo = screenInfo;
    m_captureRegion = region;
    m_targetScreen = nullptr;
    return true;
}

void WGCCaptureEngine::setFrameRate(int fps)
{
    const auto interval = SnapTray::captureFrameInterval(fps);
    if (interval <= std::chrono::nanoseconds::zero()) {
        emit error("Invalid capture frame rate");
        return;
    }
    m_frameRate = fps;
    d->frameRate = fps;
    std::lock_guard<std::mutex> lock(d->mutex);
    if (d->session) {
        try {
            using winrt::Windows::Foundation::Metadata::ApiInformation;
            if (ApiInformation::IsPropertyPresent(kSessionClass, L"MinUpdateInterval")) {
                d->session.MinUpdateInterval(std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(interval));
            }
        } catch (const winrt::hresult_error &rateError) {
            qWarning() << "WGCCaptureEngine: cannot change the update interval:" << winrtErrorText(rateError);
        }
    }
}

bool WGCCaptureEngine::start()
{
    if (d->running) {
        return true;
    }
    if (SnapTray::captureFrameInterval(m_frameRate) <= std::chrono::nanoseconds::zero()) {
        d->lastStartError = QStringLiteral("invalid capture frame rate");
        emit error("Invalid capture frame rate");
        return false;
    }
    if (!d->screenInfo.isValid() || d->captureRegion.isEmpty()) {
        d->lastStartError = QStringLiteral("region or screen not configured");
        emit error("Region or screen not configured");
        return false;
    }
    if (!isAvailable()) {
        d->lastStartError = QStringLiteral("Windows Graphics Capture is not supported on this system");
        return false;
    }

    d->reportedFailure = false;
    d->sequence = 0;
    QString startError;
    if (!d->start(&startError)) {
        d->lastStartError = startError;
        d->stop();
        qWarning() << "WGCCaptureEngine: start failed:" << startError;
        return false;
    }
    d->lastStartError.clear();
    qDebug() << "WGCCaptureEngine: Started on" << d->frameDevice->adapterName
             << "region:" << d->captureRegion << "fps:" << m_frameRate
             << "cursor in frames:" << d->cursorInFrames << "border:" << d->borderRequired;
    return true;
}

void WGCCaptureEngine::stop()
{
    const bool wasRunning = d->running;
    d->stop();
    if (wasRunning) {
        qDebug() << "WGCCaptureEngine: Stopped";
    }
}

bool WGCCaptureEngine::isRunning() const
{
    return d->running;
}

QImage WGCCaptureEngine::captureFrame()
{
    std::lock_guard<std::mutex> lock(d->mutex);
    return d->latest.image;
}

bool WGCCaptureEngine::capturesCursor() const
{
    return d->cursorInFrames;
}

std::shared_ptr<void> WGCCaptureEngine::nativeFrameDevice() const
{
    std::lock_guard<std::mutex> lock(d->mutex);
    return d->frameDevice;
}

void WGCCaptureEngine::setNativeFrameOutput(bool enabled)
{
    d->nativeOutput = enabled;
}

QString WGCCaptureEngine::engineName() const
{
    return QStringLiteral("Windows Graphics Capture");
}

bool WGCCaptureEngine::borderRequired() const
{
    return d->borderRequired;
}

QString WGCCaptureEngine::lastStartError() const
{
    return d->lastStartError;
}
