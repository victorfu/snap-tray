#ifndef CURSORPLATFORMAPPLIER_H
#define CURSORPLATFORMAPPLIER_H

#include <QCursor>

#include "cursor/CursorTypes.h"

class QWidget;
class QWindow;

class CursorPlatformApplier
{
public:
    static void applyWidgetCursor(QWidget* widget, const QCursor& cursor);
    static void applyWidgetCursor(QWidget* widget, const CursorStyleSpec& spec);
    static void applyWindowCursor(QWindow* window, const CursorStyleSpec& spec);
    static void reassertNativeStyle(const CursorStyleSpec& spec, QWidget* widget = nullptr,
                                    QWindow* window = nullptr);
};

#endif // CURSORPLATFORMAPPLIER_H
