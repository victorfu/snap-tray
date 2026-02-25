#ifndef CREDENTIALDETECTOR_H
#define CREDENTIALDETECTOR_H

#include "detection/OCRTypes.h"

#include <QRect>
#include <QSize>
#include <QVector>

/**
 * @brief Detects credential-like OCR blocks for mosaic redaction.
 *
 * Uses fixed built-in keyword and token patterns (for example API keys,
 * passwords, bearer tokens, and JWT-like values). Input OCR blocks are in
 * normalized coordinates and output rectangles are in image pixel coordinates.
 */
class CredentialDetector
{
public:
    /**
     * @brief Detect credential regions from OCR output.
     * @param blocks OCR text blocks with normalized geometry.
     * @param imageSize Source image size in pixels.
     * @return Pixel-space rectangles relative to the input image.
     */
    static QVector<QRect> detect(const QVector<OCRTextBlock>& blocks, const QSize& imageSize);
};

#endif // CREDENTIALDETECTOR_H
