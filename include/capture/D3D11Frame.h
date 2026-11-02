#ifndef D3D11FRAME_H
#define D3D11FRAME_H

#include <QtGlobal>

#ifdef Q_OS_WIN

#include <QRect>
#include <QSize>
#include <QString>

#include <d3d11.h>
#include <wrl/client.h>

#include <memory>
#include <mutex>
#include <vector>

namespace SnapTray {

// The D3D11 device a Windows capture engine renders frames on. The MP4
// encoder receives the same device (IVideoEncoder::setNativeFrameDevice), so
// frames reach it as textures without a CPU readback (#155).
//
// The device is multithread protected: the capture thread copies frames and
// the encoder thread converts them on the same immediate context.
struct D3D11FrameDevice
{
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    LUID adapterLuid{};
    QString adapterName;
    // Created with D3D11_CREATE_DEVICE_VIDEO_SUPPORT; the encoder's video
    // processor and hardware MFT need it.
    bool videoSupport = false;

    // A hardware device on `adapter` (nullptr = the default adapter). Video
    // support is requested and dropped only when the driver refuses it.
    static std::shared_ptr<D3D11FrameDevice> create(IDXGIAdapter *adapter = nullptr);

    bool isLost() const;
};

class D3D11TexturePool;

// One captured frame kept on the GPU: a BGRA texture of exactly the frame
// size. The texture returns to its pool when the last copy of the frame (for
// example the one queued for the encoder) is released, so the capture side
// never overwrites a frame the encoder has not converted yet.
struct D3D11Frame
{
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    QSize size;
    std::shared_ptr<D3D11FrameDevice> device;
};

// Reusable BGRA textures for captured frames, all of one size on one device.
// acquire() never blocks: when every texture is in use (the encoder is behind)
// a new one is made; the encoding queue's byte budget bounds how many exist.
class D3D11TexturePool : public std::enable_shared_from_this<D3D11TexturePool>
{
public:
    static std::shared_ptr<D3D11TexturePool> create(std::shared_ptr<D3D11FrameDevice> device, const QSize &size);

    // Null on failure (device lost or out of memory).
    std::shared_ptr<D3D11Frame> acquire();

    QSize size() const { return m_size; }
    const std::shared_ptr<D3D11FrameDevice> &device() const { return m_device; }

    // Idle textures kept for reuse; more are released on return.
    static constexpr size_t kMaxIdleTextures = 4;

private:
    D3D11TexturePool(std::shared_ptr<D3D11FrameDevice> device, const QSize &size);
    void giveBack(Microsoft::WRL::ComPtr<ID3D11Texture2D> texture);

    std::shared_ptr<D3D11FrameDevice> m_device;
    QSize m_size;
    std::mutex m_mutex;
    std::vector<Microsoft::WRL::ComPtr<ID3D11Texture2D>> m_idle;
};

// The D3D11Frame carried in CapturedFrame::nativeBuffer. On Windows every
// native buffer is one (only the D3D11 capture engines produce them).
inline std::shared_ptr<D3D11Frame> d3d11FrameFrom(const std::shared_ptr<void> &nativeBuffer)
{
    return std::static_pointer_cast<D3D11Frame>(nativeBuffer);
}

} // namespace SnapTray

#endif // Q_OS_WIN
#endif // D3D11FRAME_H
