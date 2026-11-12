#include "platform/KeyboardModifiers.h"
#ifdef Q_OS_WIN
#include "KeyboardModifiers_win.h"
#else
#include <QGuiApplication>
#endif

namespace SnapTray {
Qt::KeyboardModifiers currentPhysicalKeyboardModifiers()
{
#ifdef Q_OS_WIN
    return detail::windowsKeyboardModifiers(GetAsyncKeyState);
#else
    return shortcutModifiers(QGuiApplication::queryKeyboardModifiers());
#endif
}
}
