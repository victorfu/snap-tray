#include "encoding/MFSampleTimes_win.h"

#include <mfapi.h>
#include <mftransform.h>
#include <wrl/client.h>

namespace SnapTray {

HRESULT keepSampleTimesThroughVideoProcessor(IMFSinkWriter* writer, DWORD stream)
{
    if (!writer) return E_POINTER;
    Microsoft::WRL::ComPtr<IMFSinkWriterEx> extended;
    HRESULT hr = writer->QueryInterface(IID_PPV_ARGS(&extended));
    if (FAILED(hr)) return hr;
    for (DWORD index = 0;; ++index) {
        GUID category = GUID_NULL;
        Microsoft::WRL::ComPtr<IMFTransform> transform;
        hr = extended->GetTransformForStream(stream, index, &category, &transform);
        if (FAILED(hr)) return index > 0 ? S_OK : hr;
        if (category != MFT_CATEGORY_VIDEO_PROCESSOR) continue;
        Microsoft::WRL::ComPtr<IMFAttributes> attributes;
        hr = transform->GetAttributes(&attributes);
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_XVP_DISABLE_FRC, TRUE);
        if (FAILED(hr)) return hr;
    }
}

} // namespace SnapTray
