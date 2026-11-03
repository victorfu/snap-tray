#pragma once

#include "recording/AudioTracks.h"

#ifdef Q_OS_LINUX
#include <unistd.h>

namespace SnapTray {

struct AudioTrackWriterTestAccess {
    // Leave QFile and its write buffer intact, but make the underlying
    // descriptor reject writes. The next buffered flush gets a real I/O error.
    static bool makeReadOnly(AudioTrackWriter& writer)
    {
        QFile readOnly(writer.m_file.fileName());
        if (!readOnly.open(QIODevice::ReadOnly)) return false;
        const int descriptor = writer.m_file.handle();
        return descriptor >= 0 && ::dup2(readOnly.handle(), descriptor) == descriptor;
    }
};

} // namespace SnapTray
#endif
