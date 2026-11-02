#include "capture/D3D11Frame.h"

#include <QDebug>

#include <d3d10.h>
#include <dxgi.h>

using Microsoft::WRL::ComPtr;

namespace SnapTray {

std::shared_ptr<D3D11FrameDevice> D3D11FrameDevice::create(IDXGIAdapter *adapter)
{
    const D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    // An explicit adapter requires D3D_DRIVER_TYPE_UNKNOWN.
    const D3D_DRIVER_TYPE driverType = adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;

    auto result = std::make_shared<D3D11FrameDevice>();
    D3D_FEATURE_LEVEL featureLevel{};
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    HRESULT hr = D3D11CreateDevice(adapter, driverType, nullptr, flags, featureLevels,
                                   ARRAYSIZE(featureLevels), D3D11_SDK_VERSION,
                                   &result->device, &featureLevel, &result->context);
    result->videoSupport = SUCCEEDED(hr);
    if (FAILED(hr)) {
        qWarning() << "D3D11FrameDevice: device without video support, hr =" << Qt::hex << hr;
        flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        hr = D3D11CreateDevice(adapter, driverType, nullptr, flags, featureLevels,
                               ARRAYSIZE(featureLevels), D3D11_SDK_VERSION,
                               &result->device, &featureLevel, &result->context);
    }
    if (FAILED(hr)) {
        qWarning() << "D3D11FrameDevice: Failed to create D3D11 device:" << Qt::hex << hr;
        return nullptr;
    }

    ComPtr<ID3D10Multithread> multithread;
    if (SUCCEEDED(result->device.As(&multithread))) {
        multithread->SetMultithreadProtected(TRUE);
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> deviceAdapter;
    DXGI_ADAPTER_DESC adapterDesc{};
    if (SUCCEEDED(result->device.As(&dxgiDevice))
        && SUCCEEDED(dxgiDevice->GetAdapter(&deviceAdapter))
        && SUCCEEDED(deviceAdapter->GetDesc(&adapterDesc))) {
        result->adapterLuid = adapterDesc.AdapterLuid;
        result->adapterName = QString::fromWCharArray(adapterDesc.Description);
    }
    return result;
}

bool D3D11FrameDevice::isLost() const
{
    return !device || FAILED(device->GetDeviceRemovedReason());
}

std::shared_ptr<D3D11TexturePool> D3D11TexturePool::create(std::shared_ptr<D3D11FrameDevice> device,
                                                           const QSize &size)
{
    if (!device || !device->device || size.isEmpty()) {
        return nullptr;
    }
    return std::shared_ptr<D3D11TexturePool>(new D3D11TexturePool(std::move(device), size));
}

D3D11TexturePool::D3D11TexturePool(std::shared_ptr<D3D11FrameDevice> device, const QSize &size)
    : m_device(std::move(device))
    , m_size(size)
{
}

std::shared_ptr<D3D11Frame> D3D11TexturePool::acquire()
{
    ComPtr<ID3D11Texture2D> texture;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_idle.empty()) {
            texture = std::move(m_idle.back());
            m_idle.pop_back();
        }
    }
    if (!texture) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(m_size.width());
        desc.Height = static_cast<UINT>(m_size.height());
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        // Render target + shader resource: accepted as video processor input.
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        const HRESULT hr = m_device->device->CreateTexture2D(&desc, nullptr, &texture);
        if (FAILED(hr)) {
            qWarning() << "D3D11TexturePool: Failed to create frame texture" << m_size << Qt::hex << hr;
            return nullptr;
        }
    }

    // The deleter holds the pool only weakly: frames outliving the capture
    // session release their texture instead of keeping the pool alive.
    std::weak_ptr<D3D11TexturePool> weakPool = shared_from_this();
    auto *frame = new D3D11Frame{texture, m_size, m_device};
    return std::shared_ptr<D3D11Frame>(frame, [weakPool](D3D11Frame *released) {
        if (auto pool = weakPool.lock()) {
            pool->giveBack(std::move(released->texture));
        }
        delete released;
    });
}

void D3D11TexturePool::giveBack(ComPtr<ID3D11Texture2D> texture)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_idle.size() < kMaxIdleTextures && !m_device->isLost()) {
        m_idle.push_back(std::move(texture));
    }
}

} // namespace SnapTray
