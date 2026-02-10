#ifndef AUTOSCROLLER_H
#define AUTOSCROLLER_H

#include "scrollcapture/ScrollCaptureTypes.h"

#include <QPoint>
#include <QRect>
#include <QString>

#include <QtGlobal>

class AutoScroller
{
public:
    struct Request {
        QRect contentRectGlobal;
        ScrollDirection direction = ScrollDirection::Down;
        int stepPx = 120;
        quintptr nativeHandle = 0;
        bool preferNoWarp = true;
        bool allowCursorWarpFallback = false;
    };

    AutoScroller() = default;
    ~AutoScroller();

    bool scroll(const Request& request, QString* errorMessage);
    void restoreCursor();

private:
    bool m_cursorMoved = false;
    QPoint m_originalCursorPos;
};

#endif // AUTOSCROLLER_H
