#include "capture/D3D11FrameComparer.h"

#include <QDebug>

#include <d3d11.h>
#include <d3dcompiler.h>
// After d3dcompiler.h, which brings in d3d10_1.h.
#include <d3d10.h>
#include <wrl/client.h>

#include <chrono>
#include <thread>

#pragma comment(lib, "d3dcompiler")

using Microsoft::WRL::ComPtr;

namespace SnapTray {

namespace {

// A triangle that covers the viewport, from the vertex index alone.
constexpr char kVertexShader[] = R"(
float4 main(uint id : SV_VertexID) : SV_Position
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
)";

// Equal pixels are discarded; the occlusion query counts the others.
constexpr char kPixelShader[] = R"(
Texture2D<float4> first : register(t0);
Texture2D<float4> second : register(t1);
float4 main(float4 position : SV_Position) : SV_Target
{
    int3 texel = int3(position.xy, 0);
    if (all(first.Load(texel) == second.Load(texel))) discard;
    return float4(0, 0, 0, 0);
}
)";

// A 4K comparison finishes in well under a millisecond; past this the
// caller treats the frame as changed rather than wait on the GPU.
constexpr auto kResultTimeout = std::chrono::milliseconds(50);

ComPtr<ID3DBlob> compile(const char *source, size_t size, const char *profile)
{
    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompile(source, size, nullptr, nullptr, nullptr, "main", profile,
                                  D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(hr)) {
        qWarning() << "D3D11FrameComparer: shader does not compile:"
                   << (errors ? static_cast<const char *>(errors->GetBufferPointer()) : "") << Qt::hex << quint32(hr);
        return {};
    }
    return code;
}

} // namespace

struct D3D11FrameComparer::Private
{
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vertexShader;
    ComPtr<ID3D11PixelShader> pixelShader;
    ComPtr<ID3D11RasterizerState> rasterizer;
    ComPtr<ID3D11Query> query;
};

D3D11FrameComparer::D3D11FrameComparer(std::unique_ptr<Private> p) : d(std::move(p)) {}
D3D11FrameComparer::~D3D11FrameComparer() = default;

std::unique_ptr<D3D11FrameComparer> D3D11FrameComparer::create(ID3D11Device *device, ID3D11DeviceContext *context)
{
    if (!device || !context) return nullptr;
    auto p = std::make_unique<Private>();
    p->device = device;
    p->context = context;
    const auto vs = compile(kVertexShader, sizeof(kVertexShader) - 1, "vs_4_0");
    const auto ps = compile(kPixelShader, sizeof(kPixelShader) - 1, "ps_4_0");
    if (!vs || !ps) return nullptr;
    if (FAILED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &p->vertexShader))
        || FAILED(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &p->pixelShader))) {
        return nullptr;
    }
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    D3D11_QUERY_DESC queryDesc{};
    queryDesc.Query = D3D11_QUERY_OCCLUSION;
    if (FAILED(device->CreateRasterizerState(&raster, &p->rasterizer))
        || FAILED(device->CreateQuery(&queryDesc, &p->query))) {
        return nullptr;
    }
    return std::unique_ptr<D3D11FrameComparer>(new D3D11FrameComparer(std::move(p)));
}

std::optional<quint64> D3D11FrameComparer::differingPixels(ID3D11Texture2D *a, ID3D11Texture2D *b)
{
    if (!a || !b) return std::nullopt;
    D3D11_TEXTURE2D_DESC descA{};
    D3D11_TEXTURE2D_DESC descB{};
    a->GetDesc(&descA);
    b->GetDesc(&descB);
    if (descA.Width != descB.Width || descA.Height != descB.Height || descA.Format != descB.Format
        || !(descA.BindFlags & D3D11_BIND_SHADER_RESOURCE) || !(descB.BindFlags & D3D11_BIND_SHADER_RESOURCE)) {
        return std::nullopt;
    }
    ComPtr<ID3D11ShaderResourceView> views[2];
    if (FAILED(d->device->CreateShaderResourceView(a, nullptr, &views[0]))
        || FAILED(d->device->CreateShaderResourceView(b, nullptr, &views[1]))) {
        return std::nullopt;
    }

    ID3D11DeviceContext *ctx = d->context.Get();
    // The context is shared with the encoder's conversion: run the pass as
    // one step for other threads, and leave the pipeline state as it was.
    ComPtr<ID3D10Multithread> multithread;
    ctx->QueryInterface(IID_PPV_ARGS(&multithread));
    if (multithread) multithread->Enter();
    ID3D11RenderTargetView *savedTargets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ComPtr<ID3D11DepthStencilView> savedDepth;
    ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedTargets, &savedDepth);
    UINT savedViewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_VIEWPORT savedViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    ctx->RSGetViewports(&savedViewportCount, savedViewports);
    ComPtr<ID3D11RasterizerState> savedRasterizer;
    ctx->RSGetState(&savedRasterizer);

    // No render target: only the query sees the pixels that survive.
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    D3D11_VIEWPORT viewport{0, 0, float(descA.Width), float(descA.Height), 0, 1};
    ctx->RSSetViewports(1, &viewport);
    ctx->RSSetState(d->rasterizer.Get());
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(d->vertexShader.Get(), nullptr, 0);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->PSSetShader(d->pixelShader.Get(), nullptr, 0);
    ID3D11ShaderResourceView *rawViews[2] = {views[0].Get(), views[1].Get()};
    ctx->PSSetShaderResources(0, 2, rawViews);

    ctx->Begin(d->query.Get());
    ctx->Draw(3, 0);
    ctx->End(d->query.Get());

    ID3D11ShaderResourceView *noViews[2] = {};
    ctx->PSSetShaderResources(0, 2, noViews);
    ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedTargets, savedDepth.Get());
    for (ID3D11RenderTargetView *target : savedTargets) {
        if (target) target->Release();
    }
    if (savedViewportCount) ctx->RSSetViewports(savedViewportCount, savedViewports);
    ctx->RSSetState(savedRasterizer.Get());
    if (multithread) multithread->Leave();

    UINT64 samples = 0;
    const auto deadline = std::chrono::steady_clock::now() + kResultTimeout;
    for (;;) {
        const HRESULT hr = ctx->GetData(d->query.Get(), &samples, sizeof(samples), 0);
        if (hr == S_OK) return quint64(samples);
        if (FAILED(hr) || std::chrono::steady_clock::now() >= deadline) return std::nullopt;
        std::this_thread::yield();
    }
}

} // namespace SnapTray
