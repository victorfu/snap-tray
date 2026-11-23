#ifndef SELECTIONDIMENSIONLABEL_H
#define SELECTIONDIMENSIONLABEL_H

#include <QFont>
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

    struct AttachmentLayout {
        QRect panelRect;
        bool compactRegion = false;
    };

    SelectionDimensionLabel() = delete;

    static DisplayMetrics displayMetrics(const QSize& physicalSize, qreal dpr);
    static DisplayMetrics displayMetrics(const QRect& logicalRect, qreal dpr);
    static QString label(const QSize& physicalSize, qreal dpr);
    static QString label(const QRect& logicalRect, qreal dpr);
    static QString sampleLabel();
    static QString widgetLabel(const QSize& physicalSize, qreal dpr);
    static QString widgetLabel(const QRect& logicalRect, qreal dpr);
    static QString widgetSampleLabel();
    static QSize panelSize(const QString& label, const QFont& baseFont);
    static AttachmentLayout selectionPanelLayout(const QRect& selectionRect,
                                                 const QString& label,
                                                 const QFont& baseFont,
                                                 const QSize& viewportSize,
                                                 const QSize& controlAnchorSize);
    // Places a panel of the given size next to the selection without letting
    // viewport clamping push a compact-region label back over the selection.
    static AttachmentLayout placePanel(const QRect& selectionRect,
                                       const QSize& panelSize,
                                       const QSize& viewportSize);
    static QSize controlAnchorSize(bool ratioLocked);
};

#endif // SELECTIONDIMENSIONLABEL_H
