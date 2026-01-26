#include "scrolling/ScrollSender.h"

#ifdef Q_OS_WIN
#include <Windows.h>

/**
 * @brief Windows implementation of ScrollSender
 *
 * Supports all four scroll methods:
 * - MouseWheel: SendInput with MOUSEEVENTF_WHEEL
 * - SendMessage: WM_VSCROLL messages
 * - KeyDown: VK_DOWN key presses
 * - PageDown: VK_NEXT (Page Down) key
 */
class WindowsScrollSender : public ScrollSender
{
public:
    void setMethod(ScrollMethod method) override
    {
        m_method = method;
    }

    void setAmount(int amount) override
    {
        m_amount = amount;
    }

    bool sendScroll(WId window) override
    {
        HWND hwnd = reinterpret_cast<HWND>(window);

        switch (m_method) {
        case ScrollMethod::MouseWheel: {
            // Bring window to foreground
            SetForegroundWindow(hwnd);

            // Send mouse wheel event
            // WHEEL_DELTA = 120, negative = scroll down
            INPUT input = {};
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = MOUSEEVENTF_WHEEL;
            input.mi.mouseData = static_cast<DWORD>(-WHEEL_DELTA * m_amount);

            return SendInput(1, &input, sizeof(INPUT)) == 1;
        }

        case ScrollMethod::SendMessage:
            // Send WM_VSCROLL messages
            for (int i = 0; i < m_amount; i++) {
                SendMessage(hwnd, WM_VSCROLL, SB_LINEDOWN, 0);
            }
            return true;

        case ScrollMethod::KeyDown:
            SetForegroundWindow(hwnd);
            for (int i = 0; i < m_amount; i++) {
                // Send Down arrow key
                keybd_event(VK_DOWN, 0, 0, 0);
                keybd_event(VK_DOWN, 0, KEYEVENTF_KEYUP, 0);
            }
            return true;

        case ScrollMethod::PageDown:
            SetForegroundWindow(hwnd);
            // Send Page Down key
            keybd_event(VK_NEXT, 0, 0, 0);
            keybd_event(VK_NEXT, 0, KEYEVENTF_KEYUP, 0);
            return true;
        }

        return false;
    }

    bool scrollToTop(WId window) override
    {
        HWND hwnd = reinterpret_cast<HWND>(window);
        SetForegroundWindow(hwnd);

        // Method 1: Send HOME key
        keybd_event(VK_HOME, 0, 0, 0);
        keybd_event(VK_HOME, 0, KEYEVENTF_KEYUP, 0);

        // Method 2: Send SB_TOP via WM_VSCROLL
        SendMessage(hwnd, WM_VSCROLL, SB_TOP, 0);

        return true;
    }

    bool isAtBottom(WId window) override
    {
        HWND hwnd = reinterpret_cast<HWND>(window);

        SCROLLINFO si = {};
        si.cbSize = sizeof(SCROLLINFO);
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;

        if (GetScrollInfo(hwnd, SB_VERT, &si)) {
            // ShareX formula: maxPosition == currentPosition + pageSize - 1
            int maxPosition = si.nMax - qMax(static_cast<int>(si.nPage) - 1, 0);
            return si.nPos >= maxPosition;
        }

        // API failed, caller should use image comparison
        return false;
    }

private:
    ScrollMethod m_method = ScrollMethod::MouseWheel;
    int m_amount = 2;
};

std::unique_ptr<ScrollSender> ScrollSender::create()
{
    return std::make_unique<WindowsScrollSender>();
}

#else
// Stub for non-Windows builds during development
std::unique_ptr<ScrollSender> ScrollSender::create()
{
    return nullptr;
}
#endif
