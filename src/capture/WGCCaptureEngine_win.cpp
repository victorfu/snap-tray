#include "capture/WGCCaptureEngine.h"
#include "capture/CaptureFrameTiming.h"
#include "capture/WGCFrameTiming.h"
#include "capture/D3D11Frame.h"
#include "capture/D3D11FrameComparer.h"
#include "encoding/D3D11Nv12Converter_win.h"
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
#include <exception>
#include <mutex>
#include <thread>

#pragma comment(lib, "windowsapp")

using Microsoft::WRL::ComPtr;
namespace wgc = winrt::Windows::Graphics::Capture;
namespace wdx = winrt::Windows::Graphics::DirectX;

namespace {

// Two buffers: one being captured into while SnapTray copies the other.
constexpr int kFramePoolBuffers = 2;
// Failed GPU comparisons in a row before comparing is given up for the session.
constexpr int kMaxComparerFailures = 3;
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

bool currentThreadIsMta()
{
    APTTYPE type{};
    APTTYPEQUALIFIER qualifier{};
    return SUCCEEDED(CoGetApartmentType(&type, &qualifier)) && type == APTTYPE_MTA;
}

// The capture's WinRT objects belong to the MTA (#416). Called from an STA
// thread (the GUI thread), they fail with RPC_E_WRONG_THREAD; such a call
// then runs on a short-lived thread, which joins the MTA kept alive by
// MtaUsage, and is waited for.
template <typename Fn>
void runInMta(Fn &&fn)
{
    if (currentThreadIsMta()) {
        fn();
        return;
    }
    std::exception_ptr failure;
    std::thread worker([&fn, &failure] {
        try {
            fn();
        } catch (...) {
            failure = std::current_exception();
        }
    });
    worker.join();
    if (failure) std::rethrow_exception(failure);
}

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
    bool sameContent(const SnapTray::CapturedFrame &a, const SnapTray::CapturedFrame &b);  // mutex held
    void emitFrame(const SnapTray::CapturedFrame &frame, quint64 generationAtStart);
    void emitFrameLocked(const SnapTray::CapturedFrame &frame, quint64 generationAtStart);  // emitMutex held
    void emitError(const QString &message, quint64 generationAtStart);

    WGCCaptureEngine *q;
    QRect captureRegion;
    CaptureScreenInfo screenInfo;
    std::atomic<int> frameRate{30};
    std::atomic<bool> nativeOutput{false};
    std::atomic<bool> running{false};
    std::atomic<bool> captureFailed{false};
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
    std::unique_ptr<SnapTray::D3D11FrameComparer> comparer;
    bool comparerUnavailable = false;
    int comparerFailures = 0;  // in a row
    ComPtr<ID3D11Texture2D> staging;
    QSize stagingSize;
    SnapTray::CapturedFrame latest;
    quint64 sequence = 0;
    bool reportedFailure = false;

    // Serializes signal emission with stop(): once stop() has passed it, no
    // frame or error of the stopped session reaches the receivers.
    std::mutex emitMutex;
    qint64 lastEmittedNs = -1;  // guarded by emitMutex

    std::atomic<bool> refreshRequested{false};
    std::atomic<quint64> pictureSerial{0};
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

    bool started = false;
    runInMta([&] {
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
            started = true;
        } catch (const winrt::hresult_error &startError) {
            running = false;
            *error = winrtErrorText(startError);
        }
    });
    if (!started) {
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

    // A WGC callback can enter with the D3D lock held and then wait for
    // mutex. Detach under mutex, but close WinRT objects and release D3D
    // resources outside it: even releasing a staging texture takes that lock.
    std::unique_lock<std::mutex> lock(mutex);
    auto closingMta = std::move(mta);
    auto closingPool = std::exchange(framePool, nullptr);
    auto closingSession = std::exchange(session, nullptr);
    auto closingItem = std::exchange(item, nullptr);
    auto closingDevice = std::exchange(d3dDevice, nullptr);
    auto closingFrameDevice = std::move(frameDevice);
    auto closingTexturePool = std::move(texturePool);
    auto closingComparer = std::move(comparer);
    auto closingStaging = std::move(staging);
    auto closingFrame = std::exchange(latest, {});
    comparerUnavailable = false;
    comparerFailures = 0;
    stagingSize = QSize();
    lock.unlock();

    // Each step on its own: one that fails must not leave the session and
    // frame pool open, which keeps their present thread and D3D device (#416).
    const auto attempt = [](const char *step, auto &&call) {
        try {
            call();
        } catch (const winrt::hresult_error &stopError) {
            qWarning() << "WGCCaptureEngine: cleanup failed:" << step << winrtErrorText(stopError);
        }
    };
    runInMta([&] {
        if (closingPool) attempt("FrameArrived", [&] { closingPool.FrameArrived(frameArrivedToken); });
        if (closingItem) attempt("Closed", [&] { closingItem.Closed(closedToken); });
        if (closingSession) attempt("session Close", [&] { closingSession.Close(); });
        if (closingPool) attempt("frame pool Close", [&] { closingPool.Close(); });
        closingSession = nullptr;
        closingPool = nullptr;
        closingItem = nullptr;
        closingDevice = nullptr;
    });
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

bool WGCCaptureEngine::Private::sameContent(const SnapTray::CapturedFrame &a, const SnapTray::CapturedFrame &b)
{
    if (a.pixelSize != b.pixelSize) {
        return false;
    }
    if (a.nativeBuffer && b.nativeBuffer) {
        const auto first = SnapTray::d3d11FrameFrom(a.nativeBuffer);
        const auto second = SnapTray::d3d11FrameFrom(b.nativeBuffer);
        if (!first || !second || !frameDevice) {
            return false;
        }
        if (!comparer && !comparerUnavailable) {
            comparer = SnapTray::D3D11FrameComparer::create(frameDevice->device.Get(), frameDevice->context.Get());
            if (!comparer) {
                comparerUnavailable = true;
                qWarning() << "WGCCaptureEngine: cannot compare frames on the GPU; every frame is kept";
            }
        }
        if (!comparer) {
            return false;
        }
        const auto differing = comparer->differingPixels(first->texture.Get(), second->texture.Get());
        if (differing) {
            comparerFailures = 0;
            return *differing == 0;
        }
        // A failed comparison keeps the frame. One that took the device with
        // it, or keeps failing, is not tried again in this session: the
        // device is shared with the encoder (#420).
        const bool lost = frameDevice->isLost();
        if (lost || ++comparerFailures >= kMaxComparerFailures) {
            qWarning().noquote() << QStringLiteral("WGCCaptureEngine: GPU frame comparison failed%1; every frame is kept")
                                        .arg(lost ? QStringLiteral(", device lost (0x%1)")
                                                        .arg(quint32(frameDevice->device->GetDeviceRemovedReason()), 8, 16, QChar('0'))
                                                  : QStringLiteral(" %1 times in a row").arg(comparerFailures));
            comparer.reset();
            comparerUnavailable = true;
        }
        return false;
    }
    if (!a.image.isNull() && !b.image.isNull()) {
        return a.image == b.image;
    }
    return false;
}

void WGCCaptureEngine::Private::onFrameArrived(quint64 generationAtStart)
{
    SnapTray::CapturedFrame captured;
    QString failure;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!running || captureFailed || generation != generationAtStart || !framePool) {
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
        // The device can be lost by either the copy or the comparison.
        const bool duplicate = failure.isEmpty() && captured.isValid() && latest.isValid()
            && sameContent(captured, latest);
        if (failure.isEmpty() && frameDevice->isLost()) {
            failure = QStringLiteral("the capture device was lost (0x%1)")
                          .arg(quint32(frameDevice->device->GetDeviceRemovedReason()), 8, 16, QChar('0'));
        }
        if (!failure.isEmpty()) {
            captureFailed = true;
            latest = {};
        } else if (duplicate) {
            return;
        } else if (captured.isValid()) {
            captured.captureCopies = 1;
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
    // Sparse keep-alives bound encoder buffering; the encoder extends the
    // previous sample rather than converting every nominal frame interval.
    std::unique_lock<std::mutex> wakeLock(repeaterMutex);
    quint64 seenPicture = pictureSerial.load(), settledPicture = 0;
    while (running && generation == generationAtStart) {
        const auto interval = seenPicture != settledPicture
            ? SnapTray::captureSettlingInterval(frameRate.load())
            : std::chrono::nanoseconds(SnapTray::kCaptureKeepAliveNs);
        repeaterWake.wait_for(wakeLock, interval, [&] {
            return refreshRequested.load() || pictureSerial.load() != seenPicture
                || !running || generation != generationAtStart;
        });
        const bool refresh = refreshRequested.exchange(false);
        if (!refresh && pictureSerial.load() != seenPicture) {
            seenPicture = pictureSerial.load();
            continue; // wait for one quiet interval after the newest picture
        }
        if (!running || generation != generationAtStart) {
            break;
        }
        // Check and emit under one emitMutex hold, re-reading the latest frame
        // there, so a real frame emitted meanwhile is never followed by an
        // older one with a newer time.
        std::lock_guard<std::mutex> emitLock(emitMutex);
        const qint64 nowNs = SnapTray::captureClockNowNs();
        if (!refresh && lastEmittedNs >= 0 && nowNs - lastEmittedNs < interval.count()) {
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
        // The output policy may have changed at the recording gate, while
        // a static screen still holds a GPU-only warm-up frame.
        if (!nativeOutput && repeat.image.isNull() && repeat.nativeBuffer) {
            const auto texture = SnapTray::d3d11FrameFrom(repeat.nativeBuffer);
            if (texture) repeat.image = SnapTray::readBackD3D11Frame(*texture);
            if (repeat.image.isNull()) continue;
        }
        repeat.sourceTimeNs = nowNs;
        repeat.captureCopies = 0;  // a keep-alive, not a new picture
        emitFrameLocked(repeat, generationAtStart);
        settledPicture = seenPicture;
    }
}

void WGCCaptureEngine::Private::emitFrame(const SnapTray::CapturedFrame &frame, quint64 generationAtStart)
{
    {
        std::lock_guard<std::mutex> lock(emitMutex);
        emitFrameLocked(frame, generationAtStart);
        ++pictureSerial;
    }
    // Match the waiter's mutex after releasing emitMutex: this closes the
    // predicate-to-wait gap without reversing repeatLoop's lock order.
    std::lock_guard<std::mutex> wakeLock(repeaterMutex);
    repeaterWake.notify_all();
}

void WGCCaptureEngine::Private::emitFrameLocked(const SnapTray::CapturedFrame &frame, quint64 generationAtStart)
{
    if (!running || captureFailed || generation != generationAtStart) {
        return;
    }
    // A real frame carries the time it was composed and can arrive after a
    // repeat stamped "now" already went out. Its picture is newer than the
    // repeat's. Advance even equal timestamps by at least one nanosecond so
    // SessionClock accepts the picture instead of counting it as a duplicate.
    const auto stamped = SnapTray::wgcFrameForEmission(frame, lastEmittedNs);
    lastEmittedNs = stamped.sourceTimeNs;
    emit q->frameCaptured(stamped);
}

void WGCCaptureEngine::Private::emitError(const QString &message, quint64 generationAtStart)
{
    std::lock_guard<std::mutex> lock(emitMutex);
    if (!running || generation != generationAtStart || reportedFailure) {
        return;
    }
    reportedFailure = true;
    captureFailed = true;
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
        runInMta([&] {
            try {
                using winrt::Windows::Foundation::Metadata::ApiInformation;
                if (ApiInformation::IsPropertyPresent(kSessionClass, L"MinUpdateInterval")) {
                    d->session.MinUpdateInterval(
                        std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(interval));
                }
            } catch (const winrt::hresult_error &rateError) {
                qWarning() << "WGCCaptureEngine: cannot change the update interval:" << winrtErrorText(rateError);
            }
        });
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
    d->captureFailed = false;
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

void WGCCaptureEngine::requestFrame()
{
    std::lock_guard<std::mutex> lock(d->repeaterMutex);
    d->refreshRequested = true;
    d->repeaterWake.notify_all();
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
