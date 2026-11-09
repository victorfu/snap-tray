#pragma once

#include <windows.h>
#include <mfidl.h>
#include <mfreadwrite.h>

namespace SnapTray {

// A sink writer fed RGB32 inserts a video processor in front of the encoder.
// Left alone it resamples to the stream's nominal frame rate: variable-rate
// pictures are duplicated or dropped and lose their own presentation times.
// Turns that frame-rate conversion off on every video processor of `stream`.
// Call after SetInputMediaType and before BeginWriting.
HRESULT keepSampleTimesThroughVideoProcessor(IMFSinkWriter* writer, DWORD stream);

} // namespace SnapTray
