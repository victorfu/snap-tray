#ifndef TABLEDETECTOR_H
#define TABLEDETECTOR_H

#include <QString>
#include <QVector>

#include "detection/OCRTypes.h"

/**
 * @brief Result for table structure detection from OCR blocks.
 */
struct TableDetectionResult {
    bool isTable = false;
    QString tsv;
    int rowCount = 0;
    int columnCount = 0;
};

/**
 * @brief Detects tabular layout from OCR text blocks and exports TSV.
 */
class TableDetector
{
public:
    static TableDetectionResult detect(const QVector<OCRTextBlock> &blocks);
};

#endif // TABLEDETECTOR_H
