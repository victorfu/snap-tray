#ifndef OWNWINDOWCAPTUREHIDER_H
#define OWNWINDOWCAPTUREHIDER_H

#include <QPointer>
#include <QWindow>

#include <vector>

namespace snaptray::capture {

// Keeps this application's own windows (pins, toasts, menus, dialogs) out of a
// screen grab by making every visible top-level window fully transparent, then
// restores each window's previous opacity.
//
// Opacity is used instead of the platform capture-exclusion APIs because it
// behaves the same on macOS, Windows and Linux X11 (which has no exclusion
// API), never moves or re-activates windows, and cannot clear an exclusion
// another feature set on purpose. On X11 it relies on a compositing window
// manager, which the supported Ubuntu GNOME session always runs.
class OwnWindowCaptureHider
{
public:
    // Time for the compositor to present a frame without the hidden windows.
    static constexpr int kSettleDelayMs = 100;

    OwnWindowCaptureHider() = default;
    ~OwnWindowCaptureHider();

    OwnWindowCaptureHider(const OwnWindowCaptureHider&) = delete;
    OwnWindowCaptureHider& operator=(const OwnWindowCaptureHider&) = delete;

    void hide();
    void restore();
    bool hasHiddenWindows() const { return !m_hiddenWindows.empty(); }

private:
    struct HiddenWindow {
        QPointer<QWindow> window;
        qreal opacity = 1.0;
    };

    std::vector<HiddenWindow> m_hiddenWindows;
};

} // namespace snaptray::capture

#endif // OWNWINDOWCAPTUREHIDER_H
