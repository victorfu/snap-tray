#include "D3D11Nv12Converter_win.h"

#include <QDebug>

#include <d3d11_1.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>

#include <atomic>
#include <mutex>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace SnapTray {

// NV12 surfaces handed to Media Foundation, reused once it releases them.
class Nv12TexturePool
{
public:
    static constexpr size_t kMaxIdleTextures = 8;

    ComPtr<ID3D11Texture2D> take()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_idle.empty()) return nullptr;
        ComPtr<ID3D11Texture2D> texture = std::move(m_idle.back());
        m_idle.pop_back();
        return texture;
    }

    void giveBack(ComPtr<ID3D11Texture2D> texture)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_idle.size() < kMaxIdleTextures) m_idle.push_back(std::move(texture));
    }

private:
    std::mutex m_mutex;
    std::vector<ComPtr<ID3D11Texture2D>> m_idle;
};

namespace {

// IMFTrackedSample calls this when the last reference to the sample is
// released, i.e. the encoder is done reading the surface.
class Nv12Recycler final : public IMFAsyncCallback
{
public:
    Nv12Recycler(std::weak_ptr<Nv12TexturePool> pool, ComPtr<ID3D11Texture2D> texture)
        : m_pool(std::move(pool)), m_texture(std::move(texture)) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** object) override
    {
        if (!object) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFAsyncCallback)) {
            *object = static_cast<IMFAsyncCallback*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++m_refCount; }
    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG count = --m_refCount;
        if (count == 0) delete this;
        return count;
    }
    STDMETHODIMP GetParameters(DWORD*, DWORD*) override { return E_NOTIMPL; }
    STDMETHODIMP Invoke(IMFAsyncResult*) override
    {
        if (auto pool = m_pool.lock()) pool->giveBack(std::move(m_texture));
        m_texture.Reset();
        return S_OK;
    }

private:
    ~Nv12Recycler() = default;
    std::atomic<ULONG> m_refCount{1};
    std::weak_ptr<Nv12TexturePool> m_pool;
    ComPtr<ID3D11Texture2D> m_texture;
};

D3D11_TEXTURE2D_DESC textureDesc(const QSize& size, DXGI_FORMAT format, D3D11_USAGE usage, UINT bindFlags,
                                 UINT cpuAccess = 0)
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(size.width());
    desc.Height = static_cast<UINT>(size.height());
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = usage;
    desc.BindFlags = bindFlags;
    desc.CPUAccessFlags = cpuAccess;
    return desc;
}

} // namespace

D3D11Nv12Converter::D3D11Nv12Converter()
    : m_pool(std::make_shared<Nv12TexturePool>())
{
}

D3D11Nv12Converter::~D3D11Nv12Converter() = default;

bool D3D11Nv12Converter::initialize(std::shared_ptr<D3D11FrameDevice> device, const QSize& outputSize,
                                    int frameRate, QString* error)
{
    auto fail = [error](const QString& message, HRESULT hr) {
        if (error) *error = QStringLiteral("%1 (0x%2)").arg(message).arg(quint32(hr), 8, 16, QChar('0'));
        return false;
    };
    if (!device || !device->device || !device->videoSupport) {
        if (error) *error = QStringLiteral("capture device has no video support");
        return false;
    }
    m_device = std::move(device);
    m_outputSize = outputSize;
    m_frameRate = frameRate > 0 ? frameRate : 30;

    HRESULT hr = m_device->device.As(&m_videoDevice);
    if (FAILED(hr)) return fail(QStringLiteral("no ID3D11VideoDevice"), hr);
    hr = m_device->context.As(&m_videoContext);
    if (FAILED(hr)) return fail(QStringLiteral("no ID3D11VideoContext"), hr);
    hr = ensureProcessor(outputSize);
    if (FAILED(hr)) return fail(QStringLiteral("video processor unavailable"), hr);
    return true;
}

HRESULT D3D11Nv12Converter::ensureProcessor(const QSize& sourceSize)
{
    if (m_processor && sourceSize == m_processorInputSize) return S_OK;
    m_processor.Reset();
    m_enumerator.Reset();

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
    content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    content.InputFrameRate = {static_cast<UINT>(m_frameRate), 1};
    content.InputWidth = static_cast<UINT>(sourceSize.width());
    content.InputHeight = static_cast<UINT>(sourceSize.height());
    content.OutputFrameRate = content.InputFrameRate;
    content.OutputWidth = static_cast<UINT>(m_outputSize.width());
    content.OutputHeight = static_cast<UINT>(m_outputSize.height());
    content.Usage = D3D11_VIDEO_USAGE_OPTIMAL_SPEED;
    HRESULT hr = m_videoDevice->CreateVideoProcessorEnumerator(&content, &m_enumerator);
    if (FAILED(hr)) return hr;

    UINT inputSupport = 0;
    UINT outputSupport = 0;
    if (FAILED(m_enumerator->CheckVideoProcessorFormat(DXGI_FORMAT_B8G8R8A8_UNORM, &inputSupport))
        || !(inputSupport & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT)
        || FAILED(m_enumerator->CheckVideoProcessorFormat(DXGI_FORMAT_NV12, &outputSupport))
        || !(outputSupport & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT)) {
        m_enumerator.Reset();
        return MF_E_UNSUPPORTED_D3D_TYPE;
    }

    hr = m_videoDevice->CreateVideoProcessor(m_enumerator.Get(), 0, &m_processor);
    if (FAILED(hr)) return hr;

    ComPtr<ID3D11VideoContext1> videoContext1;
    if (SUCCEEDED(m_videoContext.As(&videoContext1))) {
        videoContext1->VideoProcessorSetStreamColorSpace1(m_processor.Get(), 0, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
        videoContext1->VideoProcessorSetOutputColorSpace1(m_processor.Get(), DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709);
    } else {
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE input{};
        input.RGB_Range = 0;  // 0-255
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE output{};
        output.YCbCr_Matrix = 1;  // BT.709
        output.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
        m_videoContext->VideoProcessorSetStreamColorSpace(m_processor.Get(), 0, &input);
        m_videoContext->VideoProcessorSetOutputColorSpace(m_processor.Get(), &output);
    }
    m_videoContext->VideoProcessorSetStreamAutoProcessingMode(m_processor.Get(), 0, FALSE);
    m_videoContext->VideoProcessorSetStreamFrameFormat(m_processor.Get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
    const RECT sourceRect{0, 0, sourceSize.width(), sourceSize.height()};
    const RECT outputRect{0, 0, m_outputSize.width(), m_outputSize.height()};
    m_videoContext->VideoProcessorSetStreamSourceRect(m_processor.Get(), 0, TRUE, &sourceRect);
    m_videoContext->VideoProcessorSetStreamDestRect(m_processor.Get(), 0, TRUE, &outputRect);
    m_videoContext->VideoProcessorSetOutputTargetRect(m_processor.Get(), TRUE, &outputRect);
    m_processorInputSize = sourceSize;
    return S_OK;
}

HRESULT D3D11Nv12Converter::convert(ID3D11Texture2D* source, const QSize& sourceSize, IMFSample** sample)
{
    if (!source || !sample || sourceSize.isEmpty()) return E_INVALIDARG;
    *sample = nullptr;
    HRESULT hr = ensureProcessor(sourceSize);
    if (FAILED(hr)) return hr;

    ComPtr<ID3D11Texture2D> nv12 = m_pool->take();
    if (!nv12) {
        const auto desc = textureDesc(m_outputSize, DXGI_FORMAT_NV12, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET);
        hr = m_device->device->CreateTexture2D(&desc, nullptr, &nv12);
        if (FAILED(hr)) return hr;
    }

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputDesc{};
    inputDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11VideoProcessorInputView> inputView;
    hr = m_videoDevice->CreateVideoProcessorInputView(source, m_enumerator.Get(), &inputDesc, &inputView);
    if (FAILED(hr)) return hr;

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputDesc{};
    outputDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11VideoProcessorOutputView> outputView;
    hr = m_videoDevice->CreateVideoProcessorOutputView(nv12.Get(), m_enumerator.Get(), &outputDesc, &outputView);
    if (FAILED(hr)) return hr;

    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = inputView.Get();
    hr = m_videoContext->VideoProcessorBlt(m_processor.Get(), outputView.Get(), 0, 1, &stream);
    if (FAILED(hr)) return hr;

    ComPtr<IMFMediaBuffer> buffer;
    hr = MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), nv12.Get(), 0, FALSE, &buffer);
    if (FAILED(hr)) return hr;
    ComPtr<IMF2DBuffer> buffer2d;
    DWORD length = 0;
    if (SUCCEEDED(buffer.As(&buffer2d)) && SUCCEEDED(buffer2d->GetContiguousLength(&length))) {
        buffer->SetCurrentLength(length);
    }

    ComPtr<IMFTrackedSample> tracked;
    hr = MFCreateTrackedSample(&tracked);
    if (FAILED(hr)) return hr;
    ComPtr<IMFSample> result;
    hr = tracked.As(&result);
    if (FAILED(hr)) return hr;
    hr = result->AddBuffer(buffer.Get());
    if (FAILED(hr)) return hr;
    ComPtr<IMFAsyncCallback> recycler;
    recycler.Attach(new Nv12Recycler(m_pool, nv12));
    hr = tracked->SetAllocator(recycler.Get(), nullptr);
    if (FAILED(hr)) return hr;

    *sample = result.Detach();
    return S_OK;
}

HRESULT D3D11Nv12Converter::convertImage(const QImage& image, IMFSample** sample)
{
    if (image.isNull()) return E_INVALIDARG;
    // Little-endian ARGB32/RGB32 is B,G,R,A in memory: the BGRA texture layout.
    const QImage bgra = image.format() == QImage::Format_RGB32 || image.format() == QImage::Format_ARGB32
        ? image : image.convertToFormat(QImage::Format_RGB32);
    if (!m_uploadTexture || m_uploadSize != bgra.size()) {
        m_uploadTexture.Reset();
        const auto desc = textureDesc(bgra.size(), DXGI_FORMAT_B8G8R8A8_UNORM, D3D11_USAGE_DEFAULT,
                                      D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        const HRESULT hr = m_device->device->CreateTexture2D(&desc, nullptr, &m_uploadTexture);
        if (FAILED(hr)) return hr;
        m_uploadSize = bgra.size();
    }
    m_device->context->UpdateSubresource(m_uploadTexture.Get(), 0, nullptr, bgra.constBits(),
                                         static_cast<UINT>(bgra.bytesPerLine()), 0);
    return convert(m_uploadTexture.Get(), bgra.size(), sample);
}

QImage readBackD3D11Frame(const D3D11Frame& frame)
{
    if (!frame.texture || !frame.device || !frame.device->context || frame.size.isEmpty()) return {};
    const auto desc = textureDesc(frame.size, DXGI_FORMAT_B8G8R8A8_UNORM, D3D11_USAGE_STAGING, 0,
                                  D3D11_CPU_ACCESS_READ);
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(frame.device->device->CreateTexture2D(&desc, nullptr, &staging))) return {};
    ID3D11DeviceContext* context = frame.device->context.Get();
    context->CopyResource(staging.Get(), frame.texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return {};
    const QImage image = QImage(static_cast<const uchar*>(mapped.pData), frame.size.width(), frame.size.height(),
                                static_cast<qsizetype>(mapped.RowPitch), QImage::Format_ARGB32).copy();
    context->Unmap(staging.Get(), 0);
    return image;
}

} // namespace SnapTray
