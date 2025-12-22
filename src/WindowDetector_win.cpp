#include "WindowDetector.h"
#include <QScreen>
#include <QDebug>
#include <QFileInfo>

#include <windows.h>
#include <dwmapi.h>
#include <psapi.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "psapi.lib")

namespace {

struct EnumWindowsContext {
    std::vector<DetectedElement> *windowCache;
    DWORD currentProcessId;
    qreal devicePixelRatio;
};

QString getProcessName(DWORD processId)
{
    QString processName;
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
                                   FALSE, processId);
    if (hProcess) {
        WCHAR path[MAX_PATH];
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(hProcess, 0, path, &size)) {
            QString fullPath = QString::fromWCharArray(path);
            processName = QFileInfo(fullPath).baseName();
        }
        CloseHandle(hProcess);
    }
    return processName;
}

bool isWindowCloaked(HWND hwnd)
{
    BOOL cloaked = FALSE;
    HRESULT hr = DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    return SUCCEEDED(hr) && cloaked;
}

BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM lParam)
{
    auto *context = reinterpret_cast<EnumWindowsContext *>(lParam);

    // Skip invisible windows
    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }

    // Skip cloaked windows (hidden UWP apps, virtual desktops)
    if (isWindowCloaked(hwnd)) {
        return TRUE;
    }

    // Skip our own process windows
    DWORD windowProcessId = 0;
    GetWindowThreadProcessId(hwnd, &windowProcessId);
    if (windowProcessId == context->currentProcessId) {
        return TRUE;
    }

    // Skip tool windows (tooltips, floating toolbars, etc.)
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW) {
        return TRUE;
    }

    // Skip windows without a parent that are not app windows
    // (filters out some system UI elements)
    if (!(exStyle & WS_EX_APPWINDOW) && GetWindow(hwnd, GW_OWNER) != nullptr) {
        return TRUE;
    }

    // Get window bounds - prefer extended frame bounds for accurate visible area
    // This excludes hidden borders on maximized windows, drop shadows, etc.
    RECT rect;
    HRESULT hr = DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect));
    if (FAILED(hr)) {
        // Fallback to GetWindowRect if DWM fails
        if (!GetWindowRect(hwnd, &rect)) {
            return TRUE;
        }
    }

    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    // Skip windows that are too small (in physical pixels)
    if (width < 50 || height < 50) {
        return TRUE;
    }

    // Skip windows positioned off-screen (minimized or hidden)
    if (rect.left <= -32000 || rect.top <= -32000) {
        return TRUE;
    }

    // Get window title
    WCHAR titleBuffer[256];
    int titleLen = GetWindowTextW(hwnd, titleBuffer, 256);
    QString windowTitle = QString::fromWCharArray(titleBuffer, titleLen);

    // Get owner application name
    QString ownerApp = getProcessName(windowProcessId);

    // Convert physical pixels to logical pixels for Qt coordinate system
    qreal dpr = context->devicePixelRatio;
    int logicalLeft = static_cast<int>(rect.left / dpr);
    int logicalTop = static_cast<int>(rect.top / dpr);
    int logicalWidth = static_cast<int>(width / dpr);
    int logicalHeight = static_cast<int>(height / dpr);

    // Create DetectedElement
    DetectedElement element;
    element.bounds = QRect(logicalLeft, logicalTop, logicalWidth, logicalHeight);
    element.windowTitle = windowTitle;
    element.ownerApp = ownerApp;
    element.windowLayer = 0;  // Windows doesn't have explicit layers like macOS
    element.windowId = reinterpret_cast<uintptr_t>(hwnd) & 0xFFFFFFFF;

    context->windowCache->push_back(element);

    return TRUE;
}

} // anonymous namespace

WindowDetector::WindowDetector(QObject *parent)
    : QObject(parent)
    , m_currentScreen(nullptr)
    , m_enabled(true)
{
}

WindowDetector::~WindowDetector()
{
}

bool WindowDetector::hasAccessibilityPermission()
{
    // No special permissions needed on Windows for window enumeration
    return true;
}

void WindowDetector::requestAccessibilityPermission()
{
    // No-op on Windows
}

void WindowDetector::setScreen(QScreen *screen)
{
    m_currentScreen = screen;
}

void WindowDetector::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

bool WindowDetector::isEnabled() const
{
    return m_enabled;
}

void WindowDetector::refreshWindowList()
{
    m_windowCache.clear();
    enumerateWindows();
}

void WindowDetector::enumerateWindows()
{
    EnumWindowsContext context;
    context.windowCache = &m_windowCache;
    context.currentProcessId = GetCurrentProcessId();
    context.devicePixelRatio = m_currentScreen ? m_currentScreen->devicePixelRatio() : 1.0;

    // EnumWindows returns windows in z-order (topmost first)
    EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&context));

    qDebug() << "WindowDetector: Enumerated" << m_windowCache.size() << "windows";
}

std::optional<DetectedElement> WindowDetector::detectWindowAt(const QPoint &screenPos) const
{
    if (!m_enabled) {
        return std::nullopt;
    }

    // If a screen is set, check if the point is within that screen
    if (m_currentScreen) {
        QRect screenGeometry = m_currentScreen->geometry();
        if (!screenGeometry.contains(screenPos)) {
            return std::nullopt;
        }
    }

    // Iterate through cached windows in z-order (topmost first)
    for (const auto &element : m_windowCache) {
        if (element.bounds.contains(screenPos)) {
            // Optionally filter to current screen
            if (m_currentScreen) {
                QRect screenGeometry = m_currentScreen->geometry();
                if (!element.bounds.intersects(screenGeometry)) {
                    continue;
                }
            }
            return element;
        }
    }

    return std::nullopt;
}
