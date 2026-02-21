#ifndef RECORDINGREGIONNORMALIZER_H
#define RECORDINGREGIONNORMALIZER_H

#include <QRect>

/**
 * Normalize a logical recording region so its physical pixel size is even.
 *
 * Strategy by axis:
 * 1) Prefer expanding right/bottom by 1 logical pixel.
 * 2) If blocked by screen edge, shift left/up while keeping right/bottom.
 * 3) If the axis already spans full bounds and remains odd, crop right/bottom as needed.
 */
QRect normalizeToEvenPhysicalRegion(const QRect& logicalRegion,
                                    const QRect& logicalScreenBounds,
                                    qreal dpr);

#endif // RECORDINGREGIONNORMALIZER_H
