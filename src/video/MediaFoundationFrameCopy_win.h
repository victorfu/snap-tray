#pragma once

#include <QImage>
#include <QSize>
#include <cstring>

// scanline0 is the displayed top row returned by IMF2DBuffer::Lock2D,
// not the allocation start returned by IMFMediaBuffer::Lock.
inline QImage copyMediaFoundationRgb32Frame(const uchar* scanline0, qsizetype pitch,
                                           const QSize& size)
{
    QImage frame(size, QImage::Format_RGB32);
    if (frame.isNull()) {
        return frame;
    }

    const qsizetype rowBytes = qsizetype(size.width()) * sizeof(QRgb);
    for (int y = 0; y < size.height(); ++y) {
        // Lock2D already points at the displayed top row. A negative pitch
        // advances to the next displayed row by moving backwards in memory.
        const uchar* sourceRow = scanline0 + qsizetype(y) * pitch;
        std::memcpy(frame.scanLine(y), sourceRow, rowBytes);
    }
    return frame;
}
