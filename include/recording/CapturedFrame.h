#ifndef CAPTUREDFRAME_H
#define CAPTUREDFRAME_H

#include <QImage>
#include <QMetaType>
#include <QSize>

#include <chrono>
#include <memory>

namespace SnapTray {

// Which clock a frame's sourceTimeNs is measured on. Every capture backend
// converts to the steady (monotonic) clock before handing a frame on, so the
// session clock never mixes domains; the field lets a backend say so.
enum class CaptureClockDomain {
    Steady,  // std::chrono::steady_clock (CLOCK_MONOTONIC / mach_absolute_time / QPC)
};

// Monotonic "now" on the capture clock, in nanoseconds.
inline qint64 captureClockNowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// One captured screen frame and when it was taken.
//
// `image` is the CPU copy every encoder understands today. `nativeBuffer`
// optionally keeps a platform surface (CVPixelBuffer, ID3D11Texture2D, ...)
// alive: its deleter releases it, so the capture pool cannot reuse it while
// any copy of this frame (for example one queued for an encoder) exists.
struct CapturedFrame {
    QImage image;
    std::shared_ptr<void> nativeBuffer;
    qint64 sourceTimeNs = -1;  // when the pixels were captured; -1 = unknown
    CaptureClockDomain clockDomain = CaptureClockDomain::Steady;
    quint64 sequence = 0;      // per capture session, increasing
    quint64 generation = 0;    // capture session that produced it
    QSize pixelSize;           // physical pixels

    bool isValid() const { return !image.isNull() || nativeBuffer != nullptr; }
};

} // namespace SnapTray

Q_DECLARE_METATYPE(SnapTray::CapturedFrame)

#endif // CAPTUREDFRAME_H
