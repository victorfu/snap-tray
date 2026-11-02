#pragma once

#include "capture/D3D11Frame.h"

#include <QImage>
#include <QSize>
#include <QString>

#include <d3d11.h>
#include <mfobjects.h>
#include <wrl/client.h>

#include <memory>

namespace SnapTray {

class Nv12TexturePool;

// Converts BGRA textures on the capture device into NV12 Media Foundation
// samples on the GPU (D3D11 video processor), for a sink writer that shares
// the device through an IMFDXGIDeviceManager (#155).
//
// Color: full-range sRGB in, BT.709 limited range out; the encoder's media
// types carry the same tags. Sources of any size are scaled to the output.
// Not thread-safe: the encoder calls it from its worker thread only.
class D3D11Nv12Converter
{
public:
    D3D11Nv12Converter();
    ~D3D11Nv12Converter();
    D3D11Nv12Converter(const D3D11Nv12Converter&) = delete;
    D3D11Nv12Converter& operator=(const D3D11Nv12Converter&) = delete;

    bool initialize(std::shared_ptr<D3D11FrameDevice> device, const QSize& outputSize, int frameRate,
                    QString* error);

    // A new sample holding an NV12 surface of the output size. The surface
    // returns to the converter's pool once Media Foundation releases the
    // sample, so the converter never writes into a surface still encoding.
    HRESULT convert(ID3D11Texture2D* source, const QSize& sourceSize, IMFSample** sample);
    // CPU frames (watermark, capture fallbacks): uploaded, then converted.
    HRESULT convertImage(const QImage& image, IMFSample** sample);

    const std::shared_ptr<D3D11FrameDevice>& device() const { return m_device; }

private:
    HRESULT ensureProcessor(const QSize& sourceSize);

    std::shared_ptr<D3D11FrameDevice> m_device;
    QSize m_outputSize;
    int m_frameRate = 30;
    Microsoft::WRL::ComPtr<ID3D11VideoDevice> m_videoDevice;
    Microsoft::WRL::ComPtr<ID3D11VideoContext> m_videoContext;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> m_enumerator;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessor> m_processor;
    QSize m_processorInputSize;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_uploadTexture;
    QSize m_uploadSize;
    std::shared_ptr<Nv12TexturePool> m_pool;
};

// A CPU copy of a GPU frame, read through a staging texture on the frame's
// own device. For frames the encoder cannot take as surfaces (another device).
QImage readBackD3D11Frame(const D3D11Frame& frame);

} // namespace SnapTray
