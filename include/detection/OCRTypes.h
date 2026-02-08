#ifndef OCRTYPES_H
#define OCRTYPES_H

#include <QRectF>
#include <QString>
#include <QVector>
#include <QMetaType>

/**
 * @brief OCR text block with geometric metadata.
 */
struct OCRTextBlock {
    QString text;       ///< Recognized text content
    QRectF boundingRect;///< Normalized coordinates in [0,1], top-left origin
    float confidence = -1.0f; ///< Confidence score, -1 when unavailable
};

/**
 * @brief Structured OCR result used by OCR callbacks.
 */
struct OCRResult {
    bool success = false;
    QString text;                  ///< Plain text output for legacy behavior
    QString error;                 ///< Error message on failure
    QVector<OCRTextBlock> blocks;  ///< Structured block output for layout analysis
};

Q_DECLARE_METATYPE(OCRTextBlock)
Q_DECLARE_METATYPE(OCRResult)

#endif // OCRTYPES_H
