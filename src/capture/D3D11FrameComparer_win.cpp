#include "capture/D3D11FrameComparer.h"

#include <QDebug>
#include <QScopeGuard>

#include <d3d11.h>
#include <d3dcompiler.h>
// After d3dcompiler.h, which brings in d3d10_1.h.
#include <d3d10.h>
#include <wrl/client.h>

#include <chrono>
#include <cstring>
#include <thread>

#pragma comment(lib, "d3dcompiler")

using Microsoft::WRL::ComPtr;

namespace SnapTray {

namespace {

// numthreads in the shader below.
constexpr UINT kGroupSize = 16;

// Each group counts its differing pixels in shared memory and adds the
// total to the result once. Only the two textures are read and only the
// counter is written: no rasterization, no render target. The earlier pixel
// shader pass hung Intel UHD 620 as soon as one pixel survived its discard,
// likely its output written with no render target bound (#420).
constexpr char kComputeShader[] = R"(
Texture2D<float4> first : register(t0);
Texture2D<float4> second : register(t1);
RWByteAddressBuffer result : register(u0);
groupshared uint groupCount;
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID, uint index : SV_GroupIndex)
{
    if (index == 0) groupCount = 0;
    GroupMemoryBarrierWithGroupSync();
    uint width, height;
    first.GetDimensions(width, height);
    if (id.x < width && id.y < height) {
        int3 texel = int3(id.xy, 0);
        if (any(first.Load(texel) != second.Load(texel))) InterlockedAdd(groupCount, 1);
    }
    GroupMemoryBarrierWithGroupSync();
    if (index == 0 && groupCount != 0) result.InterlockedAdd(0, groupCount);
}
)";

// A 4K comparison finishes in well under a millisecond; past this the
// caller treats the frame as changed rather than wait on the GPU.
constexpr auto kResultTimeout = std::chrono::milliseconds(50);

bool mapForRead(ID3D11DeviceContext *context, ID3D11Resource *resource, D3D11_MAPPED_SUBRESOURCE &mapped,
                std::chrono::steady_clock::time_point deadline)
{
    for (;;) {
        const HRESULT hr = context->Map(resource, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (SUCCEEDED(hr)) return true;
        if (hr != DXGI_ERROR_WAS_STILL_DRAWING || std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::yield();
    }
}

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
    ComPtr<ID3D11ComputeShader> shader;
    ComPtr<ID3D11Buffer> counter;
    ComPtr<ID3D11UnorderedAccessView> counterView;
    ComPtr<ID3D11Buffer> readback;
    ComPtr<ID3D11Texture2D> staging[2];

    // Feature level 10.x has no shader model 5 atomics. Use two reusable
    // staging textures to compare the BGRA capture pixels on the CPU.
    std::optional<quint64> compareBgra(ID3D11Texture2D *a, ID3D11Texture2D *b, const D3D11_TEXTURE2D_DESC &desc) {
        if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM || desc.SampleDesc.Count != 1) return std::nullopt;
        D3D11_TEXTURE2D_DESC current{};
        if (staging[0]) staging[0]->GetDesc(&current);
        if (!staging[1] || current.Width != desc.Width || current.Height != desc.Height) {
            D3D11_TEXTURE2D_DESC target{};
            target.Width = desc.Width;
            target.Height = desc.Height;
            target.Format = desc.Format;
            target.MipLevels = target.ArraySize = target.SampleDesc.Count = 1;
            target.Usage = D3D11_USAGE_STAGING;
            target.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            for (auto &texture : staging) {
                if (FAILED(device->CreateTexture2D(&target, nullptr, texture.ReleaseAndGetAddressOf()))) return std::nullopt;
            }
        }
        context->CopySubresourceRegion(staging[0].Get(), 0, 0, 0, 0, a, 0, nullptr);
        context->CopySubresourceRegion(staging[1].Get(), 0, 0, 0, 0, b, 0, nullptr);
        const auto deadline = std::chrono::steady_clock::now() + kResultTimeout;
        D3D11_MAPPED_SUBRESOURCE first{}, second{};
        if (!mapForRead(context.Get(), staging[0].Get(), first, deadline)) return std::nullopt;
        const auto unmapFirst = qScopeGuard([&] { context->Unmap(staging[0].Get(), 0); });
        if (!mapForRead(context.Get(), staging[1].Get(), second, deadline)) return std::nullopt;
        const auto unmapSecond = qScopeGuard([&] { context->Unmap(staging[1].Get(), 0); });
        quint64 count = 0;
        for (UINT y = 0; y < desc.Height; ++y) {
            const auto *left = reinterpret_cast<const quint32 *>(static_cast<const uchar *>(first.pData) + y * first.RowPitch);
            const auto *right = reinterpret_cast<const quint32 *>(static_cast<const uchar *>(second.pData) + y * second.RowPitch);
            if (std::memcmp(left, right, desc.Width * sizeof(*left)) == 0) continue;
            for (UINT x = 0; x < desc.Width; ++x) count += left[x] != right[x];
        }
        return count;
    }
};

D3D11FrameComparer::D3D11FrameComparer(std::unique_ptr<Private> p) : d(std::move(p)) {}
D3D11FrameComparer::~D3D11FrameComparer() = default;

std::unique_ptr<D3D11FrameComparer> D3D11FrameComparer::create(ID3D11Device *device, ID3D11DeviceContext *context)
{
    if (!device || !context) return nullptr;
    auto p = std::make_unique<Private>();
    p->device = device;
    p->context = context;
    if (device->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0) {
        return std::unique_ptr<D3D11FrameComparer>(new D3D11FrameComparer(std::move(p)));
    }
    const auto cs = compile(kComputeShader, sizeof(kComputeShader) - 1, "cs_5_0");
    if (!cs || FAILED(device->CreateComputeShader(cs->GetBufferPointer(), cs->GetBufferSize(), nullptr, &p->shader))) {
        return nullptr;
    }

    D3D11_BUFFER_DESC counterDesc{};
    counterDesc.ByteWidth = sizeof(UINT);
    counterDesc.Usage = D3D11_USAGE_DEFAULT;
    counterDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    counterDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    D3D11_UNORDERED_ACCESS_VIEW_DESC viewDesc{};
    viewDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    viewDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    viewDesc.Buffer.NumElements = 1;
    viewDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    D3D11_BUFFER_DESC readbackDesc{};
    readbackDesc.ByteWidth = sizeof(UINT);
    readbackDesc.Usage = D3D11_USAGE_STAGING;
    readbackDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(device->CreateBuffer(&counterDesc, nullptr, &p->counter))
        || FAILED(device->CreateUnorderedAccessView(p->counter.Get(), &viewDesc, &p->counterView))
        || FAILED(device->CreateBuffer(&readbackDesc, nullptr, &p->readback))) {
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
    if (!d->shader) {
        if (descB.SampleDesc.Count != 1) return std::nullopt;
        return d->compareBgra(a, b, descA);
    }
    ComPtr<ID3D11ShaderResourceView> views[2];
    if (FAILED(d->device->CreateShaderResourceView(a, nullptr, &views[0]))
        || FAILED(d->device->CreateShaderResourceView(b, nullptr, &views[1]))) {
        return std::nullopt;
    }

    ID3D11DeviceContext *ctx = d->context.Get();
    // The context is shared with the encoder's conversion: run the pass as
    // one step for other threads, and leave the compute state as it was.
    ComPtr<ID3D10Multithread> multithread;
    ctx->QueryInterface(IID_PPV_ARGS(&multithread));
    if (multithread) multithread->Enter();
    ComPtr<ID3D11ComputeShader> savedShader;
    ctx->CSGetShader(&savedShader, nullptr, nullptr);
    ID3D11ShaderResourceView *savedViews[2] = {};
    ctx->CSGetShaderResources(0, 2, savedViews);
    ComPtr<ID3D11UnorderedAccessView> savedCounterView;
    ctx->CSGetUnorderedAccessViews(0, 1, &savedCounterView);

    const UINT zero[4] = {};
    ctx->ClearUnorderedAccessViewUint(d->counterView.Get(), zero);
    ctx->CSSetShader(d->shader.Get(), nullptr, 0);
    ID3D11ShaderResourceView *rawViews[2] = {views[0].Get(), views[1].Get()};
    ctx->CSSetShaderResources(0, 2, rawViews);
    ID3D11UnorderedAccessView *counterView = d->counterView.Get();
    ctx->CSSetUnorderedAccessViews(0, 1, &counterView, nullptr);
    ctx->Dispatch((descA.Width + kGroupSize - 1) / kGroupSize, (descA.Height + kGroupSize - 1) / kGroupSize, 1);
    ctx->CopyResource(d->readback.Get(), d->counter.Get());

    ctx->CSSetShader(savedShader.Get(), nullptr, 0);
    ctx->CSSetShaderResources(0, 2, savedViews);
    for (ID3D11ShaderResourceView *view : savedViews) {
        if (view) view->Release();
    }
    ID3D11UnorderedAccessView *restoredCounterView = savedCounterView.Get();
    ctx->CSSetUnorderedAccessViews(0, 1, &restoredCounterView, nullptr);
    if (multithread) multithread->Leave();

    const auto deadline = std::chrono::steady_clock::now() + kResultTimeout;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (!mapForRead(ctx, d->readback.Get(), mapped, deadline)) return std::nullopt;
    const quint64 count = *static_cast<const UINT *>(mapped.pData);
    ctx->Unmap(d->readback.Get(), 0);
    return count;
}

} // namespace SnapTray
