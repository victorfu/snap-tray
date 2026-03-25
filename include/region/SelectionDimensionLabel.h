#ifndef SELECTIONDIMENSIONLABEL_H
#define SELECTIONDIMENSIONLABEL_H

#include <QRect>
#include <QSize>
#include <QString>

class SelectionDimensionLabel
{
public:
    struct DisplayMetrics {
        QSize size;
        QString unit;
    };

    SelectionDimensionLabel() = delete;

    static DisplayMetrics displayMetrics(const QSize& physicalSize, qreal dpr);
    static DisplayMetrics displayMetrics(const QRect& logicalRect, qreal dpr);
    static QString label(const QSize& physicalSize, qreal dpr);
    static QString label(const QRect& logicalRect, qreal dpr);
    static QString sampleLabel();
};

#endif // SELECTIONDIMENSIONLABEL_H
