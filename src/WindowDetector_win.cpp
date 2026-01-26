#include "WindowDetector.h"
#include "utils/CoordinateHelper.h"
#include <QScreen>
#include <QDebug>
#include <QFileInfo>
#include <QtConcurrent>
#include <QMutexLocker>

#include <unordered_set>
#include <map>
#include <limits>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#include <psapi.h>
#include <UIAutomation.h>
#include <comdef.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

namespace {

struct EnumWindowsContext {
    std::vector<DetectedElement> *windowCache;
    DWORD currentProcessId;
    qreal devicePixelRatio;
    DetectionFlags detectionFlags;
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
    case ElementType::UIElement:
        return flags.testFlag(DetectionFlag::UIElements);
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
    case ElementType::UIElement:
        return 5;   // UI elements can be small (buttons, icons)
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
    // Use per-window DPI for accurate multi-monitor mixed-DPI support
    UINT windowDpi = GetDpiForWindow(hwnd);
    qreal dpr = windowDpi > 0 ? windowDpi / 96.0 : context->devicePixelRatio;
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

    // If ChildControls flag is set, enumerate child controls within this window
    if (context->detectionFlags.testFlag(DetectionFlag::ChildControls)) {
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

    // Skip certain control types that aren't useful for selection
    if (wcscmp(className, L"msctls_statusbar32") == 0) {
        return TRUE;  // Skip status bars
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

    // Per-window DPI for accurate coordinates
    UINT windowDpi = GetDpiForWindow(hwnd);
    qreal dpr = windowDpi > 0 ? windowDpi / 96.0 : context->devicePixelRatio;

    // Convert to logical coordinates
    QRect physicalBounds(rect.left, rect.top, width, height);
    QRect logicalBounds = CoordinateHelper::toLogical(physicalBounds, dpr);

    // Create element for this control
    DetectedElement element;
    element.bounds = logicalBounds;
    element.windowTitle = QString();  // Skip for performance
    element.ownerApp = QString();
    element.windowLayer = 1;  // Child elements have layer 1 (vs 0 for windows)
    element.windowId = reinterpret_cast<uintptr_t>(hwnd) & 0xFFFFFFFF;
    element.elementType = elementType;

    context->windowCache->push_back(element);

    return TRUE;  // Continue enumeration
}

// Map UI Automation control type to role string
QString controlTypeToRole(CONTROLTYPEID controlType)
{
    static const std::map<CONTROLTYPEID, QString> controlTypeMap = {
        {UIA_ButtonControlTypeId, "button"},
        {UIA_CalendarControlTypeId, "calendar"},
        {UIA_CheckBoxControlTypeId, "checkbox"},
        {UIA_ComboBoxControlTypeId, "combobox"},
        {UIA_EditControlTypeId, "textfield"},
        {UIA_HyperlinkControlTypeId, "link"},
        {UIA_ImageControlTypeId, "image"},
        {UIA_ListItemControlTypeId, "listitem"},
        {UIA_ListControlTypeId, "list"},
        {UIA_MenuControlTypeId, "menu"},
        {UIA_MenuBarControlTypeId, "menubar"},
        {UIA_MenuItemControlTypeId, "menuitem"},
        {UIA_ProgressBarControlTypeId, "progressbar"},
        {UIA_RadioButtonControlTypeId, "radiobutton"},
        {UIA_ScrollBarControlTypeId, "scrollbar"},
        {UIA_SliderControlTypeId, "slider"},
        {UIA_SpinnerControlTypeId, "spinner"},
        {UIA_StatusBarControlTypeId, "statusbar"},
        {UIA_TabControlTypeId, "tablist"},
        {UIA_TabItemControlTypeId, "tab"},
        {UIA_TextControlTypeId, "text"},
        {UIA_ToolBarControlTypeId, "toolbar"},
        {UIA_ToolTipControlTypeId, "tooltip"},
        {UIA_TreeControlTypeId, "tree"},
        {UIA_TreeItemControlTypeId, "treeitem"},
        {UIA_CustomControlTypeId, "custom"},
        {UIA_GroupControlTypeId, "group"},
        {UIA_ThumbControlTypeId, "thumb"},
        {UIA_DataGridControlTypeId, "table"},
        {UIA_DataItemControlTypeId, "row"},
        {UIA_DocumentControlTypeId, "document"},
        {UIA_SplitButtonControlTypeId, "splitbutton"},
        {UIA_WindowControlTypeId, "window"},
        {UIA_PaneControlTypeId, "pane"},
        {UIA_HeaderControlTypeId, "header"},
        {UIA_HeaderItemControlTypeId, "columnheader"},
        {UIA_TableControlTypeId, "table"},
        {UIA_TitleBarControlTypeId, "titlebar"},
        {UIA_SeparatorControlTypeId, "separator"},
    };

    auto it = controlTypeMap.find(controlType);
    if (it != controlTypeMap.end()) {
        return it->second;
    }
    return "element";
}

// Thread-local UI Automation instance
thread_local IUIAutomation* g_pAutomation = nullptr;

IUIAutomation* getUIAutomation()
{
    if (!g_pAutomation) {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
            return nullptr;
        }

        hr = CoCreateInstance(
            __uuidof(CUIAutomation),
            nullptr,
            CLSCTX_INPROC_SERVER,
            __uuidof(IUIAutomation),
            reinterpret_cast<void**>(&g_pAutomation)
        );

        if (FAILED(hr)) {
            g_pAutomation = nullptr;
        }
    }
    return g_pAutomation;
}

bool getElementRect(IUIAutomationElement* element, RECT& rect)
{
    if (!element) {
        return false;
    }
    HRESULT hr = element->get_CurrentBoundingRectangle(&rect);
    return SUCCEEDED(hr);
}

bool pointInRect(const POINT& pt, const RECT& rect)
{
    return pt.x >= rect.left && pt.x <= rect.right &&
           pt.y >= rect.top && pt.y <= rect.bottom;
}

int rectWidth(const RECT& rect)
{
    return rect.right - rect.left;
}

int rectHeight(const RECT& rect)
{
    return rect.bottom - rect.top;
}

int rectArea(const RECT& rect)
{
    return rectWidth(rect) * rectHeight(rect);
}

HWND findTopmostWindowAtPointExcludingProcess(const POINT& pt, DWORD excludedPid)
{
    HWND hwnd = WindowFromPoint(pt);
    if (!hwnd) {
        return nullptr;
    }

    HWND topLevel = GetAncestor(hwnd, GA_ROOT);
    if (!topLevel) {
        topLevel = hwnd;
    }

    HWND current = topLevel;
    while (current) {
        if (IsWindowVisible(current) && !isWindowCloaked(current)) {
            RECT rect;
            if (GetWindowRect(current, &rect) && pointInRect(pt, rect)) {
                DWORD pid = 0;
                GetWindowThreadProcessId(current, &pid);
                if (pid != excludedPid) {
                    return current;
                }
            }
        }
        current = GetWindow(current, GW_HWNDNEXT);
    }

    return nullptr;
}

bool isTextControlType(CONTROLTYPEID controlType)
{
    return controlType == UIA_TextControlTypeId;
}

bool isWebContext(IUIAutomation* automation, IUIAutomationElement* element)
{
    if (!automation || !element) {
        return false;
    }

    IUIAutomationTreeWalker* walker = nullptr;
    HRESULT hr = automation->get_ControlViewWalker(&walker);
    if (FAILED(hr) || !walker) {
        return false;
    }

    IUIAutomationElement* current = element;
    current->AddRef();
    for (int i = 0; i < 10 && current; ++i) {
        CONTROLTYPEID controlType = 0;
        if (SUCCEEDED(current->get_CurrentControlType(&controlType)) &&
            controlType == UIA_DocumentControlTypeId) {
            current->Release();
            walker->Release();
            return true;
        }

        IUIAutomationElement* parent = nullptr;
        hr = walker->GetParentElement(current, &parent);
        current->Release();
        if (FAILED(hr) || !parent) {
            current = nullptr;
            break;
        }
        current = parent;
    }

    walker->Release();
    return false;
}

IUIAutomationElement* findSmallestElementAtPoint(IUIAutomationTreeWalker* walker,
                                                 IUIAutomationElement* parent,
                                                 const POINT& pt,
                                                 RECT& outRect,
                                                 int depth,
                                                 int maxDepth,
                                                 int minSize,
                                                 int maxSize,
                                                 bool skipText)
{
    if (!walker || !parent || depth > maxDepth) {
        return nullptr;
    }

    IUIAutomationElement* smallest = nullptr;
    int smallestArea = std::numeric_limits<int>::max();

    IUIAutomationElement* child = nullptr;
    HRESULT hr = walker->GetFirstChildElement(parent, &child);
    while (SUCCEEDED(hr) && child) {
        RECT childRect;
        if (getElementRect(child, childRect) && pointInRect(pt, childRect)) {
            int w = rectWidth(childRect);
            int h = rectHeight(childRect);
            if (w >= minSize && h >= minSize && w <= maxSize && h <= maxSize) {
                RECT candidateRect = {};
                IUIAutomationElement* candidate = nullptr;

                RECT deeperRect;
                IUIAutomationElement* deeper = findSmallestElementAtPoint(
                    walker, child, pt, deeperRect, depth + 1, maxDepth, minSize, maxSize, skipText);
                if (deeper) {
                    candidate = deeper;
                    candidateRect = deeperRect;
                }

                bool childEligible = true;
                if (skipText) {
                    CONTROLTYPEID controlType = 0;
                    if (SUCCEEDED(child->get_CurrentControlType(&controlType)) &&
                        isTextControlType(controlType)) {
                        childEligible = false;
                    }
                }

                if (childEligible) {
                    if (!candidate) {
                        candidate = child;
                        candidateRect = childRect;
                        candidate->AddRef();
                    } else {
                        int candidateArea = rectArea(candidateRect);
                        int childArea = rectArea(childRect);
                        if (childArea < candidateArea) {
                            candidate->Release();
                            candidate = child;
                            candidateRect = childRect;
                            candidate->AddRef();
                        }
                    }
                }

                if (candidate) {
                    int area = rectArea(candidateRect);
                    if (area < smallestArea) {
                        if (smallest) {
                            smallest->Release();
                        }
                        smallest = candidate;
                        outRect = candidateRect;
                        smallestArea = area;
                    } else {
                        candidate->Release();
                    }
                }
            }
        }

        IUIAutomationElement* next = nullptr;
        hr = walker->GetNextSiblingElement(child, &next);
        child->Release();
        child = next;
    }

    return smallest;
}

IUIAutomationElement* promoteCandidate(IUIAutomation* automation,
                                       IUIAutomationElement* candidate,
                                       RECT& rect,
                                       CONTROLTYPEID& controlType,
                                       int minSize,
                                       bool skipText)
{
    if (!automation || !candidate) {
        return candidate;
    }

    const bool needsPromotion =
        (skipText && isTextControlType(controlType)) ||
        rectWidth(rect) < minSize ||
        rectHeight(rect) < minSize;

    if (!needsPromotion) {
        return candidate;
    }

    IUIAutomationTreeWalker* walker = nullptr;
    HRESULT hr = automation->get_ControlViewWalker(&walker);
    if (FAILED(hr) || !walker) {
        return candidate;
    }

    IUIAutomationElement* current = candidate;
    current->AddRef();
    while (current) {
        IUIAutomationElement* parent = nullptr;
        hr = walker->GetParentElement(current, &parent);
        current->Release();
        if (FAILED(hr) || !parent) {
            break;
        }

        RECT parentRect;
        if (!getElementRect(parent, parentRect)) {
            parent->Release();
            break;
        }

        CONTROLTYPEID parentType = 0;
        parent->get_CurrentControlType(&parentType);
        bool parentOk = (!skipText || !isTextControlType(parentType)) &&
            rectWidth(parentRect) >= minSize &&
            rectHeight(parentRect) >= minSize;
        if (parentOk) {
            candidate->Release();
            candidate = parent;
            rect = parentRect;
            controlType = parentType;
            walker->Release();
            return candidate;
        }

        current = parent;
    }

    walker->Release();
    return candidate;
}

} // anonymous namespace

WindowDetector::WindowDetector(QObject *parent)
    : QObject(parent)
    , m_currentScreen(nullptr)
    , m_enabled(true)
    , m_uiElementDetectionEnabled(true)
    , m_detectionFlags(DetectionFlag::All | DetectionFlag::UIElements)
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
                            element.bounds = CoordinateHelper::toLogical(physicalBounds, dpr);
                            element.windowTitle = QString();
                            element.ownerApp = QString();
                            element.windowLayer = 0;
                            element.windowId = windowId;
                            element.elementType = ElementType::ContextMenu;
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

std::optional<DetectedElement> WindowDetector::detectUIElementAt(const QPoint &screenPos) const
{
    if (!m_enabled || !m_uiElementDetectionEnabled) {
        return std::nullopt;
    }

    if (!m_detectionFlags.testFlag(DetectionFlag::UIElements)) {
        return std::nullopt;
    }

    const int kMinUiElementSize = 12;
    const int kMinWebElementSize = 24;
    const int kMaxElementSize = 10000;
    const int kMaxDepth = 20;

    // Convert logical to physical coordinates for UI Automation
    qreal dpr = m_currentScreen ? m_currentScreen->devicePixelRatio() : 1.0;
    QPoint physicalPos = CoordinateHelper::toPhysical(QRect(screenPos, QSize(1, 1)), dpr).topLeft();

    IUIAutomation* pAutomation = getUIAutomation();
    if (!pAutomation) {
        qWarning() << "WindowDetector: Failed to get UI Automation instance";
        return std::nullopt;
    }

    POINT pt = { physicalPos.x(), physicalPos.y() };
    IUIAutomationElement* pElement = nullptr;

    HRESULT hr = pAutomation->ElementFromPoint(pt, &pElement);
    if (FAILED(hr) || !pElement) {
        return std::nullopt;
    }

    DWORD myPid = GetCurrentProcessId();
    int elementPid = 0;
    if (SUCCEEDED(pElement->get_CurrentProcessId(&elementPid)) &&
        static_cast<DWORD>(elementPid) == myPid) {
        pElement->Release();
        pElement = nullptr;

        HWND targetWindow = findTopmostWindowAtPointExcludingProcess(pt, myPid);
        if (!targetWindow) {
            return std::nullopt;
        }

        hr = pAutomation->ElementFromHandle(targetWindow, &pElement);
        if (FAILED(hr) || !pElement) {
            return std::nullopt;
        }
    }

    const bool webContext = isWebContext(pAutomation, pElement);
    const bool skipText = webContext;
    const int minSize = webContext ? kMinWebElementSize : kMinUiElementSize;

    // Get element bounding rectangle
    RECT rect;
    hr = pElement->get_CurrentBoundingRectangle(&rect);
    if (FAILED(hr)) {
        pElement->Release();
        return std::nullopt;
    }

    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    // Skip elements that are too small or too large (likely container/root elements)
    if (width < minSize || height < minSize || width > kMaxElementSize || height > kMaxElementSize) {
        pElement->Release();
        return std::nullopt;
    }

    // Get control type
    CONTROLTYPEID controlType = 0;
    pElement->get_CurrentControlType(&controlType);

    pElement = promoteCandidate(pAutomation, pElement, rect, controlType, minSize, skipText);
    width = rectWidth(rect);
    height = rectHeight(rect);

    if ((skipText && isTextControlType(controlType)) ||
        width < minSize || height < minSize) {
        pElement->Release();
        return std::nullopt;
    }

    // Try to refine to the smallest element under the cursor
    const int currentArea = width * height;
    int bestArea = currentArea;
    RECT bestRect = rect;
    IUIAutomationElement* bestElement = nullptr;

    auto considerCandidate = [&](IUIAutomationElement* candidate, const RECT& candidateRect) {
        const int area = rectArea(candidateRect);
        if (area < bestArea) {
            if (bestElement) {
                bestElement->Release();
            }
            bestElement = candidate;
            bestRect = candidateRect;
            bestArea = area;
        } else {
            candidate->Release();
        }
    };

    IUIAutomationTreeWalker* pWalker = nullptr;
    hr = pAutomation->get_RawViewWalker(&pWalker);
    if (SUCCEEDED(hr) && pWalker) {
        RECT smallestRect;
        IUIAutomationElement* smallest = findSmallestElementAtPoint(
            pWalker, pElement, pt, smallestRect, 0, kMaxDepth, minSize, kMaxElementSize, skipText);
        pWalker->Release();
        if (smallest) {
            considerCandidate(smallest, smallestRect);
        }
    }

    pWalker = nullptr;
    hr = pAutomation->get_ControlViewWalker(&pWalker);
    if (SUCCEEDED(hr) && pWalker) {
        RECT smallestRect;
        IUIAutomationElement* smallest = findSmallestElementAtPoint(
            pWalker, pElement, pt, smallestRect, 0, kMaxDepth, minSize, kMaxElementSize, skipText);
        pWalker->Release();
        if (smallest) {
            considerCandidate(smallest, smallestRect);
        }
    }

    pWalker = nullptr;
    hr = pAutomation->get_ContentViewWalker(&pWalker);
    if (SUCCEEDED(hr) && pWalker) {
        RECT smallestRect;
        IUIAutomationElement* smallest = findSmallestElementAtPoint(
            pWalker, pElement, pt, smallestRect, 0, kMaxDepth, minSize, kMaxElementSize, skipText);
        pWalker->Release();
        if (smallest) {
            considerCandidate(smallest, smallestRect);
        }
    }

    if (bestElement) {
        pElement->Release();
        pElement = bestElement;
        rect = bestRect;
        width = rectWidth(rect);
        height = rectHeight(rect);
        pElement->get_CurrentControlType(&controlType);
    }

    // Get element name
    BSTR name = nullptr;
    pElement->get_CurrentName(&name);
    QString elementName;
    if (name) {
        elementName = QString::fromWCharArray(name);
        SysFreeString(name);
    }

    // Get accessible description
    BSTR helpText = nullptr;
    pElement->get_CurrentHelpText(&helpText);
    QString description;
    if (helpText) {
        description = QString::fromWCharArray(helpText);
        SysFreeString(helpText);
    }

    pElement->Release();

    // Convert physical to logical coordinates
    QRect physicalBounds(rect.left, rect.top, width, height);
    QRect logicalBounds = CoordinateHelper::toLogical(physicalBounds, dpr);

    // Create the detected element
    DetectedElement element;
    element.bounds = logicalBounds;
    element.windowTitle = elementName;
    element.ownerApp = QString();
    element.windowLayer = 1;  // UI elements have higher layer than windows
    element.windowId = 0;
    element.elementType = ElementType::UIElement;
    element.role = controlTypeToRole(controlType);
    element.description = description;

    return element;
}

void WindowDetector::setUIElementDetectionEnabled(bool enabled)
{
    m_uiElementDetectionEnabled = enabled;
}

bool WindowDetector::isUIElementDetectionEnabled() const
{
    return m_uiElementDetectionEnabled;
}

void WindowDetector::setDetectionFlags(DetectionFlags flags)
{
    m_detectionFlags = flags;
}
