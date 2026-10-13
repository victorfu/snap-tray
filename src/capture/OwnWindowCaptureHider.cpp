#include "capture/OwnWindowCaptureHider.h"

#include <QGuiApplication>

#include <algorithm>

namespace snaptray::capture {

OwnWindowCaptureHider::~OwnWindowCaptureHider()
{
    restore();
}

void OwnWindowCaptureHider::hide()
{
    const auto windows = QGuiApplication::topLevelWindows();
    for (QWindow* window : windows) {
        if (!window || !window->isVisible() || window->opacity() <= 0.0) {
            continue;
        }

        const bool alreadyHidden = std::any_of(
            m_hiddenWindows.cbegin(), m_hiddenWindows.cend(),
            [window](const HiddenWindow& hidden) { return hidden.window == window; });
        if (alreadyHidden) {
            continue;
        }

        m_hiddenWindows.push_back({window, window->opacity()});
        window->setOpacity(0.0);
    }
}

void OwnWindowCaptureHider::restore()
{
    for (const HiddenWindow& hidden : m_hiddenWindows) {
        if (hidden.window) {
            hidden.window->setOpacity(hidden.opacity);
        }
    }
    m_hiddenWindows.clear();
}

} // namespace snaptray::capture
