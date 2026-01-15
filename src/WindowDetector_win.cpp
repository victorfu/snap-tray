#include "WindowDetector.h"
#include "utils/CoordinateHelper.h"
#include <QScreen>
#include <QDebug>
#include <QFileInfo>
#include <QtConcurrent>
#include <QMutexLocker>

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
    DetectionFlags detectionFlags;
};

// Check if element type should be included based on detection flags
bool shouldIncludeElementType(ElementType type, DetectionFlags flags)
{
    switch (type) {
    case ElementType::Window:
        return flags.testFlag(DetectionFlag::Windows);
    case ElementType::ContextMenu:
        return flags.testFlag(DetectionFlag::ContextMenus);
    case ElementType::PopupMenu:
        return flags.testFlag(DetectionFlag::PopupMenus);
    case ElementType::Dialog:
        return flags.testFlag(DetectionFlag::Dialogs);
    case ElementType::StatusBarItem:
        return flags.testFlag(DetectionFlag::StatusBarItems);
    case ElementType::Unknown:
        return false;
    }
    return false;
}

// Get minimum size for element type
int getMinimumSize(ElementType type)
{
    switch (type) {
    case ElementType::Window:
        return 50;
    case ElementType::ContextMenu:
    case ElementType::PopupMenu:
    case ElementType::StatusBarItem:
        return 20;
    case ElementType::Dialog:
        return 30;
    case ElementType::Unknown:
        return 50;
    }
    return 50;
}

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

    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);

    // Determine element type based on window characteristics
    ElementType elementType = ElementType::Window;

    // Check window class name for special window types
    WCHAR className[256] = {0};
    GetClassNameW(hwnd, className, 256);

    // Menu windows have class "#32768"
    if (wcscmp(className, L"#32768") == 0) {
        elementType = ElementType::ContextMenu;
    }
    // System dialog windows have class "#32770"
    else if (wcscmp(className, L"#32770") == 0) {
        elementType = ElementType::Dialog;
    }
    // Check for tool windows (tooltips, floating toolbars, menus)
    else if (exStyle & WS_EX_TOOLWINDOW) {
        // Tool windows with topmost flag near taskbar might be tray popups
        if (exStyle & WS_EX_TOPMOST) {
            RECT rect;
            if (GetWindowRect(hwnd, &rect)) {
                int screenHeight = GetSystemMetrics(SM_CYSCREEN);
                // If near bottom of screen, likely a system tray popup
                if (rect.bottom >= screenHeight - 100) {
                    elementType = ElementType::StatusBarItem;
                } else {
                    elementType = ElementType::PopupMenu;
                }
            } else {
                elementType = ElementType::PopupMenu;
            }
        } else {
            // Skip non-topmost tool windows unless detecting system UI
            if (!(context->detectionFlags & DetectionFlag::AllSystemUI)) {
                return TRUE;
            }
            elementType = ElementType::PopupMenu;
        }
    }
    // Check for dialog windows
    else if ((style & DS_MODALFRAME) || (exStyle & WS_EX_DLGMODALFRAME)) {
        elementType = ElementType::Dialog;
    }
    // Check for owned popup windows (child dialogs, dropdowns)
    else if (!(exStyle & WS_EX_APPWINDOW) && GetWindow(hwnd, GW_OWNER) != nullptr) {
        // Check if it's a combo box dropdown or similar
        if (wcscmp(className, L"ComboLBox") == 0 ||
            wcsstr(className, L"DropDown") != nullptr) {
            elementType = ElementType::PopupMenu;
        } else {
            elementType = ElementType::Dialog;
        }
    }

    // Check if this element type should be included
    if (!shouldIncludeElementType(elementType, context->detectionFlags)) {
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

    // Get minimum size based on element type
    int minSize = getMinimumSize(elementType);
    if (width < minSize || height < minSize) {
        return TRUE;
    }

    // Skip windows positioned off-screen (minimized or hidden)
    if (rect.left <= -32000 || rect.top <= -32000) {
        return TRUE;
    }

    // Skip expensive API calls for window title and process name
    // These are only used for debug/hint display and not critical for window detection
    // This significantly improves performance, especially on high-DPI screens with many windows
    QString windowTitle;  // Empty - not needed for detection
    QString ownerApp;     // Empty - not needed for detection

    // Convert physical pixels to logical pixels for Qt coordinate system
    qreal dpr = context->devicePixelRatio;
    QRect physicalBounds(rect.left, rect.top, width, height);
    QRect logicalBounds = CoordinateHelper::toLogical(physicalBounds, dpr);

    // Create DetectedElement
    DetectedElement element;
    element.bounds = logicalBounds;
    element.windowTitle = windowTitle;
    element.ownerApp = ownerApp;
    element.windowLayer = 0;  // Windows doesn't have explicit layers like macOS
    element.windowId = reinterpret_cast<uintptr_t>(hwnd) & 0xFFFFFFFF;
    element.elementType = elementType;

    context->windowCache->push_back(element);

    return TRUE;
}

} // anonymous namespace

WindowDetector::WindowDetector(QObject *parent)
    : QObject(parent)
    , m_currentScreen(nullptr)
    , m_enabled(true)
    , m_detectionFlags(DetectionFlag::All)
{
}

WindowDetector::~WindowDetector()
{
    if (m_refreshFuture.isValid() && m_refreshFuture.isRunning()) {
        m_refreshFuture.waitForFinished();
    }
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

void WindowDetector::setDetectionFlags(DetectionFlags flags)
{
    m_detectionFlags = flags;
}

DetectionFlags WindowDetector::detectionFlags() const
{
    return m_detectionFlags;
}

void WindowDetector::refreshWindowList()
{
    QMutexLocker locker(&m_cacheMutex);
    m_windowCache.clear();
    enumerateWindows();
}

void WindowDetector::refreshWindowListAsync()
{
    uint64_t requestId = ++m_refreshRequestId;

    m_refreshComplete = false;
    qreal dpr = m_currentScreen ? m_currentScreen->devicePixelRatio() : 1.0;

    m_refreshFuture = QtConcurrent::run([this, dpr, requestId]() {
        std::vector<DetectedElement> newCache;
        enumerateWindowsInternal(newCache, dpr);

        if (m_refreshRequestId.load() != requestId) {
            qDebug() << "WindowDetector: Discarding stale refresh result";
            return;
        }

        {
            QMutexLocker locker(&m_cacheMutex);
            m_windowCache = std::move(newCache);
        }

        m_refreshComplete = true;
        QMetaObject::invokeMethod(this, "windowListReady", Qt::QueuedConnection);
    });
}

bool WindowDetector::isRefreshComplete() const
{
    return m_refreshComplete.load();
}

void WindowDetector::enumerateWindows()
{
    EnumWindowsContext context;
    context.windowCache = &m_windowCache;
    context.currentProcessId = GetCurrentProcessId();
    context.devicePixelRatio = m_currentScreen ? m_currentScreen->devicePixelRatio() : 1.0;
    context.detectionFlags = m_detectionFlags;

    // EnumWindows returns windows in z-order (topmost first)
    EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&context));

    // Additionally enumerate menu windows directly if detecting context menus
    // Menu windows sometimes don't appear in EnumWindows
    if (m_detectionFlags.testFlag(DetectionFlag::ContextMenus)) {
        HWND menuWnd = FindWindowExW(nullptr, nullptr, L"#32768", nullptr);
        while (menuWnd) {
            if (IsWindowVisible(menuWnd)) {
                RECT rect;
                if (GetWindowRect(menuWnd, &rect)) {
                    int width = rect.right - rect.left;
                    int height = rect.bottom - rect.top;
                    int minSize = getMinimumSize(ElementType::ContextMenu);

                    if (width >= minSize && height >= minSize) {
                        // Check if not already in cache
                        uint32_t windowId = reinterpret_cast<uintptr_t>(menuWnd) & 0xFFFFFFFF;
                        bool alreadyExists = false;
                        for (const auto &elem : m_windowCache) {
                            if (elem.windowId == windowId) {
                                alreadyExists = true;
                                break;
                            }
                        }

                        if (!alreadyExists) {
                            qreal dpr = context.devicePixelRatio;
                            QRect physicalBounds(rect.left, rect.top, width, height);
                            DetectedElement element;
                            element.bounds = CoordinateHelper::toLogical(physicalBounds, dpr);
                            element.windowTitle = QString();
                            element.ownerApp = QString();
                            element.windowLayer = 0;
                            element.windowId = windowId;
                            element.elementType = ElementType::ContextMenu;

                            // Insert at beginning since menus are typically topmost
                            m_windowCache.insert(m_windowCache.begin(), element);
                        }
                    }
                }
            }
            menuWnd = FindWindowExW(nullptr, menuWnd, L"#32768", nullptr);
        }
    }

    qDebug() << "WindowDetector: Enumerated" << m_windowCache.size() << "windows";
}

void WindowDetector::enumerateWindowsInternal(std::vector<DetectedElement>& cache, qreal dpr)
{
    EnumWindowsContext context;
    context.windowCache = &cache;
    context.currentProcessId = GetCurrentProcessId();
    context.devicePixelRatio = dpr;
    context.detectionFlags = m_detectionFlags;

    // EnumWindows returns windows in z-order (topmost first)
    EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&context));

    // Additionally enumerate menu windows directly if detecting context menus
    if (m_detectionFlags.testFlag(DetectionFlag::ContextMenus)) {
        HWND menuWnd = FindWindowExW(nullptr, nullptr, L"#32768", nullptr);
        while (menuWnd) {
            if (IsWindowVisible(menuWnd)) {
                RECT rect;
                if (GetWindowRect(menuWnd, &rect)) {
                    int width = rect.right - rect.left;
                    int height = rect.bottom - rect.top;
                    int minSize = getMinimumSize(ElementType::ContextMenu);

                    if (width >= minSize && height >= minSize) {
                        uint32_t windowId = reinterpret_cast<uintptr_t>(menuWnd) & 0xFFFFFFFF;
                        bool alreadyExists = false;
                        for (const auto &elem : cache) {
                            if (elem.windowId == windowId) {
                                alreadyExists = true;
                                break;
                            }
                        }

                        if (!alreadyExists) {
                            QRect physicalBounds(rect.left, rect.top, width, height);
                            DetectedElement element;
                            element.bounds = CoordinateHelper::toLogical(physicalBounds, dpr);
                            element.windowTitle = QString();
                            element.ownerApp = QString();
                            element.windowLayer = 0;
                            element.windowId = windowId;
                            element.elementType = ElementType::ContextMenu;
                            cache.insert(cache.begin(), element);
                        }
                    }
                }
            }
            menuWnd = FindWindowExW(nullptr, menuWnd, L"#32768", nullptr);
        }
    }

    qDebug() << "WindowDetector: Enumerated" << cache.size() << "windows (async)";
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

    // Lock mutex for thread-safe cache access
    QMutexLocker locker(&m_cacheMutex);

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
