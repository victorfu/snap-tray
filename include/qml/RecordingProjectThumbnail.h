#pragma once

#include "recording/RecordingProject.h"

class IVideoFrameReader;
namespace SnapTray {
// Decode and compose a committed snapshot off-thread. A null image means that
// no safe preview is available; callers must never substitute a raw frame.
QImage renderProjectThumbnail(const RecordingProject& project, IVideoFrameReader& reader);
// Application thread only. Deduplicates requests; completion is published on
// this same thread, serialized with project updates and deletion.
void requestProjectThumbnail(const RecordingProject& project);
} // namespace SnapTray
