#include "capture/DXGICaptureEngine.h"

#include <QScreen>
#include <QGuiApplication>
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

using Microsoft::WRL::ComPtr;

class DXGICaptureEngine::Private : public QObject
{
    Q_OBJECT
public:
    Private() : QObject(nullptr) {}
    ~Private() { cleanupThread(); cleanup(); }

    bool initializeDXGI();
    void cleanup();
    void cleanupThread();
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

    QRect captureRegion;
    QScreen *targetScreen = nullptr;
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

bool DXGICaptureEngine::Private::initializeDXGI()
{
    if (!targetScreen) {
        qWarning() << "DXGICaptureEngine: targetScreen is null";
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

    // Try to find matching output by position
    // Note: QScreen::geometry() returns logical coordinates, but DXGI uses physical coordinates
    // We need to convert using devicePixelRatio for HiDPI displays
    QRect screenGeom = targetScreen->geometry();
    qreal dpr = targetScreen->devicePixelRatio();
    QRect physicalGeom(
        static_cast<int>(screenGeom.x() * dpr),
        static_cast<int>(screenGeom.y() * dpr),
        static_cast<int>(screenGeom.width() * dpr),
        static_cast<int>(screenGeom.height() * dpr)
    );
    bool foundMatch = false;

    while (adapter->EnumOutputs(outputIndex, &output) != DXGI_ERROR_NOT_FOUND) {
        DXGI_OUTPUT_DESC desc;
        output->GetDesc(&desc);

        QRect outputRect(
            desc.DesktopCoordinates.left,
            desc.DesktopCoordinates.top,
            desc.DesktopCoordinates.right - desc.DesktopCoordinates.left,
            desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top
        );

        // Compare using physical coordinates for HiDPI compatibility
        if (outputRect == physicalGeom) {
            outputDesc = desc;
            foundMatch = true;
            break;
        }

        output.Reset();
        outputIndex++;
    }

    // If no exact match, use first output
    if (!foundMatch) {
        hr = adapter->EnumOutputs(0, &output);
        if (FAILED(hr)) {
            qWarning() << "DXGICaptureEngine: No outputs found:" << hr;
            return false;
        }
        output->GetDesc(&outputDesc);
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

void DXGICaptureEngine::Private::cleanupThread()
{
    running = false;

    if (captureTimer) {
        // Stop timer from worker thread context
        QMetaObject::invokeMethod(captureTimer, "stop", Qt::QueuedConnection);
        captureTimer->deleteLater();
        captureTimer = nullptr;
    }

    if (workerThread) {
        workerThread->quit();
        if (!workerThread->wait(3000)) {  // Wait up to 3 seconds
            qWarning() << "DXGICaptureEngine: Worker thread did not exit gracefully";
            workerThread->terminate();
            workerThread->wait();
        }
        workerThread->deleteLater();
        workerThread = nullptr;
    }

    // Move back to main thread for proper cleanup
    moveToThread(QGuiApplication::instance()->thread());
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
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    // Convert logical coordinates to physical coordinates for HiDPI
    qreal dpr = targetScreen ? targetScreen->devicePixelRatio() : 1.0;
    int physX = static_cast<int>(captureRegion.x() * dpr);
    int physY = static_cast<int>(captureRegion.y() * dpr);
    int physWidth = static_cast<int>(captureRegion.width() * dpr);
    int physHeight = static_cast<int>(captureRegion.height() * dpr);

    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, physWidth, physHeight);
    HGDIOBJ hOld = SelectObject(hdcMem, hBitmap);

    // Capture screen region using physical coordinates
    BitBlt(hdcMem, 0, 0, physWidth, physHeight,
           hdcScreen, physX, physY,
           SRCCOPY | CAPTUREBLT);

    SelectObject(hdcMem, hOld);

    // Convert to QImage using physical dimensions
    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = physWidth;
    bmi.bmiHeader.biHeight = -physHeight;  // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    QImage image(physWidth, physHeight, QImage::Format_ARGB32);
    GetDIBits(hdcMem, hBitmap, 0, physHeight, image.bits(), &bmi, DIB_RGB_COLORS);

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

    // Calculate region relative to output in physical coordinates
    // captureRegion is in logical coordinates, DXGI texture is in physical coordinates
    qreal dpr = targetScreen ? targetScreen->devicePixelRatio() : 1.0;
    int physX = static_cast<int>(captureRegion.x() * dpr);
    int physY = static_cast<int>(captureRegion.y() * dpr);
    int physWidth = static_cast<int>(captureRegion.width() * dpr);
    int physHeight = static_cast<int>(captureRegion.height() * dpr);

    int relX = physX - outputDesc.DesktopCoordinates.left;
    int relY = physY - outputDesc.DesktopCoordinates.top;

    // Crop to capture region and deep copy
    lastFrame = fullImage.copy(relX, relY, physWidth, physHeight);
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

    d->captureRegion = region;
    d->targetScreen = screen;
    m_captureRegion = region;
    m_targetScreen = screen;
    return true;
}

bool DXGICaptureEngine::start()
{
    if (!d->targetScreen || d->captureRegion.isEmpty()) {
        emit error("Region or screen not configured");
        return false;
    }

    // Try to initialize DXGI
    d->useDXGI = d->initializeDXGI();

    if (!d->useDXGI) {
        qDebug() << "DXGICaptureEngine: DXGI unavailable, using BitBlt fallback";
    }

    // Create worker thread for async capture
    d->workerThread = new QThread();
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
    if (d->running) {
        d->cleanupThread();
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
