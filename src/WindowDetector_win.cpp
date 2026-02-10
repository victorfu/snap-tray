#include "WindowDetector.h"
#include "utils/CoordinateHelper.h"
#include <QScreen>
#include <QDebug>
#include <QFileInfo>
#include <QtConcurrent>
#include <QMutexLocker>

#include <limits>
#include <unordered_set>

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
    DWORD parentProcessId;  // Set per top-level window before EnumChildWindows
};

// Forward declaration
BOOL CALLBACK enumChildWindowsProc(HWND hwnd, LPARAM lParam);

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
        return 10;  // Reduced from 20 for compact UI elements
    case ElementType::Dialog:
        return 20;  // Reduced from 30 for small dialogs
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

    // Skip fully transparent layered windows (invisible overlays)
    if (exStyle & WS_EX_LAYERED) {
        BYTE alpha = 255;
        if (GetLayeredWindowAttributes(hwnd, nullptr, &alpha, nullptr)) {
            if (alpha < 10) {  // Effectively invisible
                return TRUE;
            }
        }
    }

    // Determine element type based on window characteristics
    ElementType elementType = ElementType::Window;

    // Check window class name for special window types
    WCHAR className[256] = {0};
    GetClassNameW(hwnd, className, 256);

    // Skip desktop shell windows — they span the entire virtual desktop
    // and are never useful detection targets on multi-monitor setups
    if (wcscmp(className, L"Progman") == 0 || wcscmp(className, L"WorkerW") == 0) {
        return TRUE;
    }

    // Menu windows have class "#32768"
    if (wcscmp(className, L"#32768") == 0) {
        elementType = ElementType::ContextMenu;
    }
    // System dialog windows have class "#32770"
    else if (wcscmp(className, L"#32770") == 0) {
        elementType = ElementType::Dialog;
    }
    // Modern UI elements (IME, tooltips) - only if ModernUI flag is set
    else if (context->detectionFlags.testFlag(DetectionFlag::ModernUI)) {
        // IME candidate windows
        if (wcscmp(className, L"IME") == 0 ||
            wcsstr(className, L"MSCTFIME") != nullptr) {
            elementType = ElementType::PopupMenu;
        }
        // Tooltips
        else if (wcscmp(className, L"tooltips_class32") == 0) {
            elementType = ElementType::PopupMenu;
        }
    }
    // Check for tool windows (tooltips, floating toolbars, menus)
    else if (exStyle & WS_EX_TOOLWINDOW) {
        // Tool windows with topmost flag near taskbar might be tray popups
        if (exStyle & WS_EX_TOPMOST) {
            RECT rect;
            if (GetWindowRect(hwnd, &rect)) {
                // Use per-monitor metrics instead of primary monitor only
                HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi = { sizeof(MONITORINFO) };
                if (GetMonitorInfo(hMonitor, &mi)) {
                    int monitorBottom = mi.rcMonitor.bottom;
                    // If near bottom of monitor, likely a system tray popup
                    if (rect.bottom >= monitorBottom - 100) {
                        elementType = ElementType::StatusBarItem;
                    } else {
                        elementType = ElementType::PopupMenu;
                    }
                } else {
                    // Fallback to popup menu if monitor info unavailable
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

    // Skip minimized windows using proper Windows API
    if (IsIconic(hwnd)) {
        return TRUE;
    }

    // Additional safety: skip windows with extreme off-screen coordinates
    // (handles edge cases where IsIconic may not catch everything)
    if (rect.left <= -32000 || rect.top <= -32000) {
        return TRUE;
    }

    // Skip expensive API calls for window title and process name
    // These are only used for debug/hint display and not critical for window detection
    // This significantly improves performance, especially on high-DPI screens with many windows
    QString windowTitle;  // Empty - not needed for detection
    QString ownerApp;     // Empty - not needed for detection

    // Convert physical pixels to logical pixels for Qt coordinate system
    // Use monitor-aware conversion for accurate multi-monitor mixed-DPI support
    QRect physicalBounds(rect.left, rect.top, width, height);
    QRect logicalBounds = CoordinateHelper::physicalToQtLogical(physicalBounds, hwnd);

    // Create DetectedElement
    DetectedElement element;
    element.bounds = logicalBounds;
    element.windowTitle = windowTitle;
    element.ownerApp = ownerApp;
    element.windowLayer = 0;  // Windows doesn't have explicit layers like macOS
    element.windowId = reinterpret_cast<uintptr_t>(hwnd) & 0xFFFFFFFF;
    element.elementType = elementType;
    element.ownerPid = static_cast<qint64>(windowProcessId);

    context->windowCache->push_back(element);

    // If ChildControls flag is set, enumerate child controls within this window
    if (context->detectionFlags.testFlag(DetectionFlag::ChildControls)) {
        context->parentProcessId = windowProcessId;
        EnumChildWindows(hwnd, enumChildWindowsProc, lParam);
    }

    return TRUE;
}

// Callback for enumerating child controls within a window
BOOL CALLBACK enumChildWindowsProc(HWND hwnd, LPARAM lParam)
{
    auto *context = reinterpret_cast<EnumWindowsContext *>(lParam);

    // Skip invisible controls
    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }

    // Get control bounds
    RECT rect;
    if (!GetWindowRect(hwnd, &rect)) {
        return TRUE;
    }

    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    // Filter out tiny controls (noise) and giant containers
    const int MIN_CONTROL_SIZE = 5;
    const int MAX_REASONABLE_SIZE = 10000;
    if (width < MIN_CONTROL_SIZE || height < MIN_CONTROL_SIZE ||
        width > MAX_REASONABLE_SIZE || height > MAX_REASONABLE_SIZE) {
        return TRUE;
    }

    // Get control class name to identify type
    WCHAR className[256] = {0};
    GetClassNameW(hwnd, className, 256);

    // Skip desktop shell child windows and control types that aren't useful for selection
    if (wcscmp(className, L"WorkerW") == 0 ||
        wcscmp(className, L"msctls_statusbar32") == 0) {
        return TRUE;
    }

    // Classify control type based on class name
    ElementType elementType = ElementType::Window;  // Default

    // Common Win32 control classes
    if (wcscmp(className, L"Button") == 0) {
        elementType = ElementType::PopupMenu;  // Reuse for buttons
    }
    else if (wcscmp(className, L"Edit") == 0 ||
             wcscmp(className, L"RichEdit20W") == 0 ||
             wcscmp(className, L"RichEdit20A") == 0) {
        elementType = ElementType::PopupMenu;  // Reuse for text boxes
    }
    else if (wcscmp(className, L"Static") == 0) {
        elementType = ElementType::PopupMenu;  // Labels
    }
    else if (wcsstr(className, L"Pane") != nullptr ||
             wcsstr(className, L"Panel") != nullptr) {
        elementType = ElementType::Dialog;  // Panels/containers
    }

    // Convert to logical coordinates using monitor-aware conversion
    QRect physicalBounds(rect.left, rect.top, width, height);
    QRect logicalBounds = CoordinateHelper::physicalToQtLogical(physicalBounds, hwnd);

    DetectedElement element;
    element.bounds = logicalBounds;
    element.windowLayer = 1;  // Child elements have layer 1 (vs 0 for windows)
    element.windowId = reinterpret_cast<uintptr_t>(hwnd) & 0xFFFFFFFF;
    element.elementType = elementType;
    element.ownerPid = static_cast<qint64>(context->parentProcessId);

    context->windowCache->push_back(element);

    return TRUE;  // Continue enumeration
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

    // Snapshot detection flags to avoid data race with main thread
    const DetectionFlags flags = m_detectionFlags;

    m_refreshFuture = QtConcurrent::run([this, dpr, requestId, flags]() {
        std::vector<DetectedElement> newCache;
        enumerateWindowsInternal(newCache, dpr, flags);

        if (m_refreshRequestId.load() != requestId) {
            qDebug() << "WindowDetector: Discarding stale refresh result";
            return;
        }

        {
            QMutexLocker locker(&m_cacheMutex);
            m_windowCache = std::move(newCache);
        }

        m_refreshComplete = true;
        QMetaObject::invokeMethod(this, [this]() {
            emit windowListReady();
        }, Qt::QueuedConnection);
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
        // Build hash set of existing window IDs for O(1) duplicate checking
        std::unordered_set<uint32_t> existingIds;
        existingIds.reserve(m_windowCache.size());
        for (const auto &elem : m_windowCache) {
            existingIds.insert(elem.windowId);
        }

        HWND menuWnd = FindWindowExW(nullptr, nullptr, L"#32768", nullptr);
        while (menuWnd) {
            if (IsWindowVisible(menuWnd)) {
                RECT rect;
                if (GetWindowRect(menuWnd, &rect)) {
                    int width = rect.right - rect.left;
                    int height = rect.bottom - rect.top;
                    int minSize = getMinimumSize(ElementType::ContextMenu);

                    if (width >= minSize && height >= minSize) {
                        // Check if not already in cache using hash set
                        uint32_t windowId = reinterpret_cast<uintptr_t>(menuWnd) & 0xFFFFFFFF;
                        if (existingIds.find(windowId) == existingIds.end()) {
                            QRect physicalBounds(rect.left, rect.top, width, height);
                            DetectedElement element;
                            element.bounds = CoordinateHelper::physicalToQtLogical(physicalBounds, menuWnd);
                            element.windowTitle = QString();
                            element.ownerApp = QString();
                            element.windowLayer = 0;
                            element.windowId = windowId;
                            element.elementType = ElementType::ContextMenu;
                            DWORD menuPid = 0;
                            GetWindowThreadProcessId(menuWnd, &menuPid);
                            element.ownerPid = static_cast<qint64>(menuPid);

                            // Insert at beginning since menus are typically topmost
                            m_windowCache.insert(m_windowCache.begin(), element);
                            existingIds.insert(windowId);  // Update hash set
                        }
                    }
                }
            }
            menuWnd = FindWindowExW(nullptr, menuWnd, L"#32768", nullptr);
        }
    }

    qDebug() << "WindowDetector: Enumerated" << m_windowCache.size() << "windows";
}

void WindowDetector::enumerateWindowsInternal(std::vector<DetectedElement>& cache, qreal dpr, DetectionFlags flags)
{
    EnumWindowsContext context;
    context.windowCache = &cache;
    context.currentProcessId = GetCurrentProcessId();
    context.devicePixelRatio = dpr;
    context.detectionFlags = flags;

    // EnumWindows returns windows in z-order (topmost first)
    EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&context));

    // Additionally enumerate menu windows directly if detecting context menus
    if (flags.testFlag(DetectionFlag::ContextMenus)) {
        // Build hash set of existing window IDs for O(1) duplicate checking
        std::unordered_set<uint32_t> existingIds;
        existingIds.reserve(cache.size());
        for (const auto &elem : cache) {
            existingIds.insert(elem.windowId);
        }

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
                        if (existingIds.find(windowId) == existingIds.end()) {
                            QRect physicalBounds(rect.left, rect.top, width, height);
                            DetectedElement element;
                            element.bounds = CoordinateHelper::physicalToQtLogical(physicalBounds, menuWnd);
                            element.windowTitle = QString();
                            element.ownerApp = QString();
                            element.windowLayer = 0;
                            element.windowId = windowId;
                            element.elementType = ElementType::ContextMenu;
                            DWORD menuPid = 0;
                            GetWindowThreadProcessId(menuWnd, &menuPid);
                            element.ownerPid = static_cast<qint64>(menuPid);
                            cache.insert(cache.begin(), element);
                            existingIds.insert(windowId);  // Update hash set
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

    // Cache screen geometry once for both the early bail-out and per-element clipping
    const QRect screenGeometry = m_currentScreen ? m_currentScreen->geometry() : QRect();

    if (!screenGeometry.isNull() && !screenGeometry.contains(screenPos)) {
        return std::nullopt;
    }

    // Lock mutex for thread-safe cache access
    QMutexLocker locker(&m_cacheMutex);

    const bool detectChildren = m_detectionFlags.testFlag(DetectionFlag::ChildControls);

    // Iterate through cached elements in z-order (topmost first).
    // Layout: [Window A, Child A1, Child A2, Window B, Child B1, ...]
    // Child elements (windowLayer == 1) always follow their parent window
    // (windowLayer == 0) because EnumChildWindows runs inside enumWindowsProc.
    for (size_t i = 0; i < m_windowCache.size(); ++i) {
        const auto &element = m_windowCache[i];

        // Skip child elements at the top of the iteration -- they belong to a
        // window that didn't match the cursor. We only process children after
        // finding a matching parent window below.
        if (element.windowLayer == 1) {
            continue;
        }

        if (!element.bounds.contains(screenPos)) {
            continue;
        }

        // Filter to current screen: only return windows whose on-screen
        // portion actually contains the cursor. This prevents cross-monitor
        // false positives from windows that span the virtual desktop.
        if (!screenGeometry.isNull()) {
            QRect clipped = element.bounds.intersected(screenGeometry);
            if (!clipped.contains(screenPos)) {
                continue;
            }
        }

        // Found the topmost window containing the cursor.
        // If child detection is enabled, scan its child elements for a better match.
        if (detectChildren) {
            std::optional<DetectedElement> bestChild;
            qint64 bestChildArea = std::numeric_limits<qint64>::max();
            const qint64 windowArea = static_cast<qint64>(element.bounds.width()) * element.bounds.height();

            for (size_t j = i + 1; j < m_windowCache.size(); ++j) {
                const auto &child = m_windowCache[j];

                // Stop when we reach the next top-level window
                if (child.windowLayer == 0) {
                    break;
                }

                if (!child.bounds.contains(screenPos)) {
                    continue;
                }

                // Skip child elements that cover >= 90% of the parent window area
                // (they provide no additional precision over whole-window detection)
                const qint64 childArea = static_cast<qint64>(child.bounds.width()) * child.bounds.height();
                if (windowArea > 0 && childArea * 100 / windowArea >= 90) {
                    continue;
                }

                if (childArea < bestChildArea) {
                    bestChildArea = childArea;
                    bestChild = child;
                }
            }

            if (bestChild.has_value()) {
                return bestChild;
            }
        }

        // No suitable child found; return the window itself.
        return element;
    }

    return std::nullopt;
}
