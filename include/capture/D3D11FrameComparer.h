#pragma once

#include <QtGlobal>

#include <memory>
#include <optional>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;

namespace SnapTray {

// Counts the pixels that differ between two textures of one device, on the
// GPU: a compute pass compares every pixel and counts the differing ones.
// Exact (a single changed pixel counts) and nothing is read back but the
// count. Needs Direct3D feature level 11.0.
//
// The textures must have the same size and format and be bindable as shader
// resources. Calls use the device's immediate context; the caller serializes
// them the way it serializes its other uses of that context.
class D3D11FrameComparer
{
public:
    // nullptr when the shaders cannot be built on this device.
    static std::unique_ptr<D3D11FrameComparer> create(ID3D11Device *device, ID3D11DeviceContext *context);
    ~D3D11FrameComparer();

    // The number of differing pixels, or nullopt when the comparison could
    // not be made (different sizes, no shader view, no result in time, or
    // the device was lost).
    std::optional<quint64> differingPixels(ID3D11Texture2D *a, ID3D11Texture2D *b);

private:
    struct Private;
    explicit D3D11FrameComparer(std::unique_ptr<Private> d);
    std::unique_ptr<Private> d;
};

} // namespace SnapTray
