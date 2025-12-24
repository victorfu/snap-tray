#include "capture/DXGICaptureEngine.h"

#include <QScreen>
#include <QGuiApplication>
#include <QDebug>

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

class DXGICaptureEngine::Private
{
public:
    Private() = default;
    ~Private() { cleanup(); }

    bool initializeDXGI();
    void cleanup();
    QImage captureWithBitBlt();
    QImage captureWithDXGI();

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIOutputDuplication> duplication;
    ComPtr<ID3D11Texture2D> stagingTexture;

    DXGI_OUTPUT_DESC outputDesc;
    D3D11_TEXTURE2D_DESC textureDesc;

    QRect captureRegion;
    QScreen *targetScreen = nullptr;
    int frameRate = 30;
    bool running = false;
    bool useDXGI = false;
    QImage lastFrame;  // Cache for when no new frame available
};

bool DXGICaptureEngine::Private::initializeDXGI()
{
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
    if (duplication) {
        duplication->ReleaseFrame();
    }
    stagingTexture.Reset();
    duplication.Reset();
    context.Reset();
    device.Reset();
    running = false;
    useDXGI = false;
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
        // No new frame, return cached frame
        if (!lastFrame.isNull()) {
            return lastFrame;
        }
        return captureWithBitBlt();
    }

    if (hr == DXGI_ERROR_ACCESS_LOST) {
        // Duplication was invalidated, try to reinitialize
        qWarning() << "DXGICaptureEngine: Access lost, reinitializing";
        cleanup();
        useDXGI = initializeDXGI();
        if (useDXGI) {
            // Successfully reinitialized, retry DXGI capture
            return captureWithDXGI();
        }
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

    context->Unmap(stagingTexture.Get(), 0);
    duplication->ReleaseFrame();

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

    d->running = true;
    qDebug() << "DXGICaptureEngine: Started"
             << (d->useDXGI ? "(DXGI Desktop Duplication)" : "(BitBlt fallback)")
             << "region:" << d->captureRegion;

    return true;
}

void DXGICaptureEngine::stop()
{
    if (d->running) {
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

    if (d->useDXGI) {
        return d->captureWithDXGI();
    }

    return d->captureWithBitBlt();
}

QString DXGICaptureEngine::engineName() const
{
    if (d->useDXGI) {
        return QStringLiteral("DXGI Desktop Duplication");
    }
    return QStringLiteral("GDI BitBlt (fallback)");
}
