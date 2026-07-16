#include "capture/DXGICaptureEngine.h"
#include "utils/CoordinateHelper.h"

#include <QCoreApplication>
#include <QScreen>
#include <QDebug>
#include <QThread>
#include <QTimer>

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <chrono>
#include <mutex>
#include <atomic>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace {

struct MonitorLookup
{
    QString deviceName;
    QRect physicalGeometry;
    bool found = false;
};

BOOL CALLBACK findMonitorByDeviceName(HMONITOR monitor, HDC, LPRECT, LPARAM context)
{
    auto *lookup = reinterpret_cast<MonitorLookup *>(context);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, reinterpret_cast<LPMONITORINFO>(&info))) {
        return TRUE;
    }

    const QString candidateName = QString::fromWCharArray(info.szDevice);
    if (candidateName.compare(lookup->deviceName, Qt::CaseInsensitive) != 0) {
        return TRUE;
    }

    lookup->physicalGeometry = QRect(
        info.rcMonitor.left,
        info.rcMonitor.top,
        info.rcMonitor.right - info.rcMonitor.left,
        info.rcMonitor.bottom - info.rcMonitor.top);
    lookup->found = true;
    return FALSE;
}

} // namespace

class DXGICaptureEngine::Private : public QObject
{
    Q_OBJECT
public:
    Private() : QObject(nullptr) {}
    ~Private() override
    {
        if (workerThread || captureTimer) {
            qWarning() << "DXGICaptureEngine: Private destroyed with active thread resources."
                       << "stop() should be called before destruction.";
        }
        cleanup();
    }

    bool initializeDXGI();
    bool resolvePhysicalOutputGeometry();
    void cleanup();
    void cleanupThread(QThread *ownerThread);
    QImage captureWithBitBlt();
    QImage captureWithDXGI();

    // Thread-safe frame access (matching macOS SCKCaptureEngine pattern)
    void setLatestFrame(const QImage &frame) {
        std::lock_guard<std::mutex> lock(frameMutex);
        latestFrame = frame;
    }

    QImage getLatestFrame() {
        std::lock_guard<std::mutex> lock(frameMutex);
        return latestFrame;
    }

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIOutputDuplication> duplication;
    ComPtr<ID3D11Texture2D> stagingTexture;

    DXGI_OUTPUT_DESC outputDesc;
    D3D11_TEXTURE2D_DESC textureDesc;
    QRect physicalOutputGeometry;

    QRect captureRegion;
    CaptureScreenInfo screenInfo;
    int frameRate = 30;
    std::atomic<bool> running{false};
    bool useDXGI = false;
    QImage lastFrame;  // Cache for when no new frame available (used during capture)
    std::chrono::steady_clock::time_point lastFrameTime;

    // Worker thread members
    QThread *workerThread = nullptr;
    QTimer *captureTimer = nullptr;
    QImage latestFrame;  // Thread-safe cached frame for main thread access
    std::mutex frameMutex;

    // Retry limit for DXGI reinitialization
    int dxgiRetryCount = 0;
    static constexpr int kMaxDxgiRetries = 3;

    // Mutex to protect DXGI resources during cleanup
    // Using recursive_mutex because captureWithDXGI() may call cleanup() internally
    std::recursive_mutex resourceMutex;

public slots:
    void doCapture();
};

bool DXGICaptureEngine::Private::resolvePhysicalOutputGeometry()
{
    const QString deviceName = screenInfo.nativeName.trimmed().isEmpty()
        ? screenInfo.name.trimmed()
        : screenInfo.nativeName.trimmed();
    if (deviceName.isEmpty()) {
        qWarning() << "DXGICaptureEngine: Target screen has no Windows display device name";
        return false;
    }

    MonitorLookup lookup;
    lookup.deviceName = deviceName;
    EnumDisplayMonitors(
        nullptr,
        nullptr,
        findMonitorByDeviceName,
        reinterpret_cast<LPARAM>(&lookup));

    if (!lookup.found || lookup.physicalGeometry.isEmpty()) {
        qWarning() << "DXGICaptureEngine: Could not resolve physical monitor for"
                   << deviceName;
        return false;
    }

    if (!screenInfo.physicalGeometry.isEmpty() &&
        screenInfo.physicalGeometry != lookup.physicalGeometry) {
        qWarning() << "DXGICaptureEngine: Target monitor geometry changed during initialization"
                   << screenInfo.physicalGeometry << lookup.physicalGeometry;
        return false;
    }

    physicalOutputGeometry = lookup.physicalGeometry;
    return true;
}

bool DXGICaptureEngine::Private::initializeDXGI()
{
    if (!screenInfo.isValid()) {
        qWarning() << "DXGICaptureEngine: screen metadata is invalid";
        return false;
    }

    HRESULT hr;

    // Create D3D11 device
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL featureLevel;

    hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &device,
        &featureLevel,
        &context
    );

    if (FAILED(hr)) {
        qWarning() << "DXGICaptureEngine: Failed to create D3D11 device:" << hr;
        return false;
    }

    // Get DXGI device
    ComPtr<IDXGIDevice> dxgiDevice;
    hr = device.As(&dxgiDevice);
    if (FAILED(hr)) {
        qWarning() << "DXGICaptureEngine: Failed to get DXGI device:" << hr;
        return false;
    }

    // Get adapter
    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr)) {
        qWarning() << "DXGICaptureEngine: Failed to get adapter:" << hr;
        return false;
    }

    // Find the output (monitor) that matches our target screen
    ComPtr<IDXGIOutput> output;
    UINT outputIndex = 0;
    bool foundMatch = false;
    const QString targetDeviceName = screenInfo.nativeName.trimmed().isEmpty()
        ? screenInfo.name.trimmed()
        : screenInfo.nativeName.trimmed();

    while (true) {
        hr = adapter->EnumOutputs(outputIndex, &output);
        if (hr == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (FAILED(hr)) {
            qWarning() << "DXGICaptureEngine: Failed to enumerate DXGI output:"
                       << hr;
            break;
        }

        DXGI_OUTPUT_DESC desc{};
        hr = output->GetDesc(&desc);
        if (FAILED(hr)) {
            output.Reset();
            outputIndex++;
            continue;
        }

        const QString outputName = QString::fromWCharArray(desc.DeviceName);
        if (outputName.compare(targetDeviceName, Qt::CaseInsensitive) == 0) {
            const QRect outputRect(
                desc.DesktopCoordinates.left,
                desc.DesktopCoordinates.top,
                desc.DesktopCoordinates.right - desc.DesktopCoordinates.left,
                desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top);
            if (outputRect != physicalOutputGeometry) {
                qWarning() << "DXGICaptureEngine: DXGI output geometry does not match screen snapshot"
                           << outputRect << physicalOutputGeometry;
                return false;
            }
            outputDesc = desc;
            foundMatch = true;
            break;
        }

        output.Reset();
        outputIndex++;
    }

    if (!foundMatch) {
        // Selecting output 0 here can silently capture a different monitor.
        // Let the caller use the correctly mapped BitBlt fallback instead.
        qWarning() << "DXGICaptureEngine: Target output is not on the selected DXGI adapter:"
                   << targetDeviceName;
        return false;
    }

    // Get IDXGIOutput1 for duplication
    ComPtr<IDXGIOutput1> output1;
    hr = output.As(&output1);
    if (FAILED(hr)) {
        qWarning() << "DXGICaptureEngine: Failed to get IDXGIOutput1:" << hr;
        return false;
    }

    // Create desktop duplication
    hr = output1->DuplicateOutput(device.Get(), &duplication);
    if (FAILED(hr)) {
        qWarning() << "DXGICaptureEngine: Failed to create desktop duplication:" << hr;
        if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) {
            qWarning() << "  - Desktop duplication not available (possibly remote session)";
        }
        return false;
    }

    // Create staging texture for CPU access
    ZeroMemory(&textureDesc, sizeof(textureDesc));
    textureDesc.Width = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
    textureDesc.Height = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_STAGING;
    textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    hr = device->CreateTexture2D(&textureDesc, nullptr, &stagingTexture);
    if (FAILED(hr)) {
        qWarning() << "DXGICaptureEngine: Failed to create staging texture:" << hr;
        return false;
    }

    return true;
}

void DXGICaptureEngine::Private::cleanup()
{
    std::lock_guard<std::recursive_mutex> lock(resourceMutex);

    if (duplication) {
        duplication->ReleaseFrame();
    }
    stagingTexture.Reset();
    duplication.Reset();
    context.Reset();
    device.Reset();
    useDXGI = false;
}

void DXGICaptureEngine::Private::cleanupThread(QThread *ownerThread)
{
    QThread *resolvedOwnerThread = ownerThread ? ownerThread : QThread::currentThread();
    auto workerAffinityCleanup = [this, resolvedOwnerThread](bool allowMoveToOwnerThread) {
        running = false;

        if (captureTimer) {
            captureTimer->stop();
            delete captureTimer;
            captureTimer = nullptr;
        }

        if (allowMoveToOwnerThread
            && resolvedOwnerThread
            && thread() != resolvedOwnerThread) {
            moveToThread(resolvedOwnerThread);
        }
    };

    QThread *objectThread = thread();
    if (objectThread && objectThread != QThread::currentThread()) {
        if (objectThread->isRunning()) {
            // Keep moveToThread() and timer destruction in the object's current affinity thread.
            const bool invoked = QMetaObject::invokeMethod(
                this,
                [workerAffinityCleanup]() { workerAffinityCleanup(true); },
                Qt::BlockingQueuedConnection
            );
            if (!invoked) {
                qWarning() << "DXGICaptureEngine: Failed to invoke worker-affinity cleanup."
                           << "Skipping unsafe cross-thread timer destruction.";
                running = false;
            }
        } else {
            qWarning() << "DXGICaptureEngine: Affinity thread is not running during cleanup."
                       << "Performing fallback cleanup from current thread.";
            workerAffinityCleanup(false);
        }
    } else {
        workerAffinityCleanup(true);
    }

    if (workerThread) {
        QThread *threadToCleanup = workerThread;
        workerThread = nullptr;

        threadToCleanup->quit();
        if (!threadToCleanup->wait(3000)) {  // Wait up to 3 seconds
            qWarning() << "DXGICaptureEngine: Worker thread did not exit gracefully";
            threadToCleanup->terminate();
            threadToCleanup->wait();
        }
        if (threadToCleanup->thread() == QThread::currentThread()) {
            delete threadToCleanup;
        } else {
            // Avoid blocking cross-thread deletion, which can deadlock when the affinity
            // thread is waiting for this caller thread to finish (e.g. init cancellation).
            threadToCleanup->deleteLater();
        }
    } else {
        running = false;
    }
}

void DXGICaptureEngine::Private::doCapture()
{
    if (!running) return;

    std::lock_guard<std::recursive_mutex> lock(resourceMutex);
    if (!running) return;  // Double-check after acquiring lock

    QImage frame;
    if (useDXGI) {
        frame = captureWithDXGI();
    } else {
        frame = captureWithBitBlt();
    }

    if (!frame.isNull()) {
        setLatestFrame(frame);
    }
}

QImage DXGICaptureEngine::Private::captureWithBitBlt()
{
    // Fallback: Use GDI BitBlt for capture
    HDC hdcScreen = GetDC(NULL);
    if (!hdcScreen) {
        return QImage();
    }

    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    if (!hdcMem) {
        ReleaseDC(NULL, hdcScreen);
        return QImage();
    }

    const QRect physRegion = CoordinateHelper::toPhysicalScreenRect(
        captureRegion,
        screenInfo.geometry,
        physicalOutputGeometry,
        screenInfo.devicePixelRatio);
    if (physRegion.isEmpty() || !physicalOutputGeometry.contains(physRegion)) {
        qWarning() << "DXGICaptureEngine: Capture region maps outside target monitor"
                   << physRegion << physicalOutputGeometry;
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        return QImage();
    }

    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, physRegion.width(), physRegion.height());
    if (!hBitmap) {
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        return QImage();
    }

    HGDIOBJ hOld = SelectObject(hdcMem, hBitmap);

    // Capture screen region using physical coordinates
    BitBlt(hdcMem, 0, 0, physRegion.width(), physRegion.height(),
           hdcScreen, physRegion.x(), physRegion.y(),
           SRCCOPY | CAPTUREBLT);

    SelectObject(hdcMem, hOld);

    // Convert to QImage using physical dimensions
    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = physRegion.width();
    bmi.bmiHeader.biHeight = -physRegion.height();  // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    QImage image(physRegion.width(), physRegion.height(), QImage::Format_ARGB32);
    GetDIBits(hdcMem, hBitmap, 0, physRegion.height(), image.bits(), &bmi, DIB_RGB_COLORS);

    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    return image;
}

QImage DXGICaptureEngine::Private::captureWithDXGI()
{
    if (!duplication || !stagingTexture) {
        return captureWithBitBlt();
    }

    HRESULT hr;
    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    ComPtr<IDXGIResource> resource;

    // Try to acquire next frame
    hr = duplication->AcquireNextFrame(16, &frameInfo, &resource);  // 16ms timeout

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        // No new frame, return cached frame if still fresh
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastFrameTime).count();

        if (!lastFrame.isNull() && elapsed < 500) {
            return lastFrame;
        }
        return captureWithBitBlt();
    }

    if (hr == DXGI_ERROR_ACCESS_LOST) {
        // Duplication was invalidated, try to reinitialize with retry limit
        if (dxgiRetryCount >= kMaxDxgiRetries) {
            qWarning() << "DXGICaptureEngine: Max retries reached, falling back to BitBlt";
            dxgiRetryCount = 0;
            useDXGI = false;
            return captureWithBitBlt();
        }

        qWarning() << "DXGICaptureEngine: Access lost, reinitializing (attempt"
                   << dxgiRetryCount + 1 << "/" << kMaxDxgiRetries << ")";
        cleanup();
        useDXGI = initializeDXGI();
        if (useDXGI) {
            dxgiRetryCount++;
            return captureWithDXGI();
        }
        dxgiRetryCount = 0;
        return captureWithBitBlt();
    }

    if (FAILED(hr)) {
        qWarning() << "DXGICaptureEngine: AcquireNextFrame failed with hr:" << Qt::hex << hr;
        return captureWithBitBlt();
    }

    // Get texture from resource
    ComPtr<ID3D11Texture2D> frameTexture;
    hr = resource.As(&frameTexture);
    if (FAILED(hr)) {
        duplication->ReleaseFrame();
        return captureWithBitBlt();
    }

    // Copy to staging texture
    context->CopyResource(stagingTexture.Get(), frameTexture.Get());

    // Map staging texture for CPU read
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = context->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        duplication->ReleaseFrame();
        return captureWithBitBlt();
    }

    // Create full-screen image
    int fullWidth = textureDesc.Width;
    int fullHeight = textureDesc.Height;

    QImage fullImage(
        static_cast<uchar *>(mapped.pData),
        fullWidth,
        fullHeight,
        mapped.RowPitch,
        QImage::Format_ARGB32
    );

    // Map from Qt's per-screen logical coordinate space into the selected
    // output's native desktop coordinate space. The output origin is translated,
    // never multiplied by the target screen DPR.
    const QRect physRegion = CoordinateHelper::toPhysicalScreenRect(
        captureRegion,
        screenInfo.geometry,
        physicalOutputGeometry,
        screenInfo.devicePixelRatio);

    if (physRegion.isEmpty() || !physicalOutputGeometry.contains(physRegion)) {
        qWarning() << "DXGICaptureEngine: Capture region maps outside selected DXGI output"
                   << physRegion << physicalOutputGeometry;
        context->Unmap(stagingTexture.Get(), 0);
        duplication->ReleaseFrame();
        return QImage();
    }

    const int relX = physRegion.x() - physicalOutputGeometry.x();
    const int relY = physRegion.y() - physicalOutputGeometry.y();

    // Crop to capture region and deep copy
    lastFrame = fullImage.copy(relX, relY, physRegion.width(), physRegion.height());
    lastFrameTime = std::chrono::steady_clock::now();

    context->Unmap(stagingTexture.Get(), 0);
    duplication->ReleaseFrame();

    // Reset retry counter on successful capture
    dxgiRetryCount = 0;

    return lastFrame;
}

// DXGICaptureEngine implementation

DXGICaptureEngine::DXGICaptureEngine(QObject *parent)
    : ICaptureEngine(parent)
    , d(new Private)
{
}

DXGICaptureEngine::~DXGICaptureEngine()
{
    stop();
    delete d;
}

bool DXGICaptureEngine::isAvailable()
{
    // Cache the result to avoid expensive device creation on every call
    static int cachedResult = -1;  // -1 = not checked, 0 = false, 1 = true

    if (cachedResult >= 0) {
        return cachedResult == 1;
    }

    // Check if we're in a remote session (DXGI duplication doesn't work)
    if (GetSystemMetrics(SM_REMOTESESSION)) {
        cachedResult = 0;
        return false;
    }

    // Check Windows version (need Windows 8+)
    OSVERSIONINFOEXW osvi;
    ZeroMemory(&osvi, sizeof(osvi));
    osvi.dwOSVersionInfoSize = sizeof(osvi);

    // Windows 8 is version 6.2
    osvi.dwMajorVersion = 6;
    osvi.dwMinorVersion = 2;

    DWORDLONG conditionMask = 0;
    VER_SET_CONDITION(conditionMask, VER_MAJORVERSION, VER_GREATER_EQUAL);
    VER_SET_CONDITION(conditionMask, VER_MINORVERSION, VER_GREATER_EQUAL);

    if (!VerifyVersionInfoW(&osvi, VER_MAJORVERSION | VER_MINORVERSION, conditionMask)) {
        cachedResult = 0;
        return false;
    }

    // Try to create a D3D11 device to verify DirectX support
    D3D_FEATURE_LEVEL featureLevel;
    ComPtr<ID3D11Device> testDevice;
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &testDevice,
        &featureLevel,
        nullptr
    );

    cachedResult = (SUCCEEDED(hr) && featureLevel >= D3D_FEATURE_LEVEL_11_0) ? 1 : 0;
    return cachedResult == 1;
}

bool DXGICaptureEngine::setRegion(const QRect &region, QScreen *screen)
{
    if (!screen) {
        emit error("Invalid screen");
        return false;
    }

    return setRegion(region, CaptureScreenInfo::fromScreen(screen));
}

bool DXGICaptureEngine::setRegion(const QRect &region, const CaptureScreenInfo &screenInfo)
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

bool DXGICaptureEngine::start()
{
    if (!d->screenInfo.isValid() || d->captureRegion.isEmpty()) {
        emit error("Region or screen not configured");
        return false;
    }

    // Resolve the native monitor bounds by stable Windows device name before
    // choosing DXGI or GDI. Without this mapping, mixed-DPI secondary origins
    // cannot be converted safely from Qt logical coordinates.
    if (!d->resolvePhysicalOutputGeometry()) {
        emit error("Failed to resolve target display");
        return false;
    }

    // Try to initialize DXGI
    d->useDXGI = d->initializeDXGI();

    if (!d->useDXGI) {
        qDebug() << "DXGICaptureEngine: DXGI unavailable, using BitBlt fallback";
    }

    // Create worker thread object on the application thread so stop() can tear it down safely.
    QObject *threadOwnerObject = QCoreApplication::instance();
    QThread *threadOwner = threadOwnerObject ? threadOwnerObject->thread() : QThread::currentThread();

    d->workerThread = new QThread();
    if (!d->workerThread) {
        emit error("Failed to create DXGI capture worker thread");
        d->cleanup();
        return false;
    }

    if (threadOwnerObject
        && threadOwner
        && d->workerThread->thread() != threadOwner) {
        d->workerThread->moveToThread(threadOwner);
    }

    d->moveToThread(d->workerThread);

    // Create timer in worker thread context
    d->captureTimer = new QTimer();
    d->captureTimer->moveToThread(d->workerThread);
    d->captureTimer->setInterval(1000 / d->frameRate);

    // Connect timer to capture slot
    connect(d->captureTimer, &QTimer::timeout, d, &Private::doCapture);
    connect(d->workerThread, &QThread::started, d->captureTimer,
            QOverload<>::of(&QTimer::start));

    d->running = true;
    d->workerThread->start();

    qDebug() << "DXGICaptureEngine: Started with worker thread"
             << (d->useDXGI ? "(DXGI Desktop Duplication)" : "(BitBlt fallback)")
             << "region:" << d->captureRegion
             << "fps:" << d->frameRate;

    return true;
}

void DXGICaptureEngine::stop()
{
    const bool hasThreadResources = d->running || d->captureTimer || d->workerThread;
    if (hasThreadResources || d->useDXGI) {
        if (hasThreadResources) {
            d->cleanupThread(thread());
        }
        d->cleanup();
        qDebug() << "DXGICaptureEngine: Stopped";
    }
}

bool DXGICaptureEngine::isRunning() const
{
    return d->running;
}

QImage DXGICaptureEngine::captureFrame()
{
    if (!d->running) {
        return QImage();
    }

    // Return cached frame from worker thread (non-blocking)
    return d->getLatestFrame();
}

QString DXGICaptureEngine::engineName() const
{
    if (d->useDXGI) {
        return QStringLiteral("DXGI Desktop Duplication");
    }
    return QStringLiteral("GDI BitBlt (fallback)");
}

// Required for Q_OBJECT in Private class defined in .cpp file
#include "DXGICaptureEngine_win.moc"
