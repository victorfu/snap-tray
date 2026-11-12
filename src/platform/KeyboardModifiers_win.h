#pragma once

#include <Qt>
#include <windows.h>

namespace SnapTray::detail {
template<typename KeyState>
Qt::KeyboardModifiers windowsKeyboardModifiers(KeyState keyState)
{
    const auto pressed = [&](int key) { return (keyState(key) & 0x8000) != 0; };
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    if (pressed(VK_CONTROL) || pressed(VK_LCONTROL) || pressed(VK_RCONTROL)) modifiers |= Qt::ControlModifier;
    if (pressed(VK_MENU) || pressed(VK_LMENU) || pressed(VK_RMENU)) modifiers |= Qt::AltModifier;
    if (pressed(VK_SHIFT) || pressed(VK_LSHIFT) || pressed(VK_RSHIFT)) modifiers |= Qt::ShiftModifier;
    if (pressed(VK_LWIN) || pressed(VK_RWIN)) modifiers |= Qt::MetaModifier;
    return modifiers;
}
}
