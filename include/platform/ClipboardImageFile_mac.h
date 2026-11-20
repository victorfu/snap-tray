#pragma once

#include "PlatformFeatures.h"

@class NSPasteboard;

namespace SnapTray {
PlatformFeatures::ClipboardCopyResult writeClipboardImageFile(NSPasteboard* board,
    ClipboardImageFile& file, bool includePixels);
}
