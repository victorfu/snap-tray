#pragma once

#include <Qt>

namespace SnapTray {
inline Qt::KeyboardModifiers shortcutModifiers(Qt::KeyboardModifiers modifiers)
{
    return modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier);
}

// Physical state on Windows, including when our thread is not receiving keyboard input.
Qt::KeyboardModifiers currentPhysicalKeyboardModifiers();
}
