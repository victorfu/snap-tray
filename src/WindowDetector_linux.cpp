#include "WindowDetector.h"

#include <QByteArray>
#include <QGuiApplication>
#include <QMutexLocker>
#include <QScreen>
#include <QWindow>
#include <QtGui/qguiapplication_platform.h>

#include <algorithm>
#include <unistd.h>
#include <unordered_set>
#include <X11/Xatom.h>
#include <X11/Xlib.h>

namespace {

struct X11Atoms
{
    Atom cardinal = None;
    Atom utf8String = None;
    Atom wmClass = None;
    Atom netClientList = None;
    Atom netClientListStacking = None;
    Atom netFrameExtents = None;
    Atom netWmName = None;
    Atom netWmPid = None;
    Atom netWmWindowType = None;
    Atom typeCombo = None;
    Atom typeDesktop = None;
    Atom typeDialog = None;
    Atom typeDock = None;
    Atom typeDropdownMenu = None;
    Atom typeMenu = None;
    Atom typeNotification = None;
    Atom typePopupMenu = None;
    Atom typeSplash = None;
    Atom typeToolbar = None;
    Atom typeTooltip = None;
    Atom typeUtility = None;
};

Atom internAtom(Display* display, const char* name)
{
    return display ? XInternAtom(display, name, False) : None;
}

X11Atoms makeAtoms(Display* display)
{
    X11Atoms atoms;
    atoms.cardinal = XA_CARDINAL;
    atoms.utf8String = internAtom(display, "UTF8_STRING");
    atoms.wmClass = internAtom(display, "WM_CLASS");
    atoms.netClientList = internAtom(display, "_NET_CLIENT_LIST");
    atoms.netClientListStacking = internAtom(display, "_NET_CLIENT_LIST_STACKING");
    atoms.netFrameExtents = internAtom(display, "_NET_FRAME_EXTENTS");
    atoms.netWmName = internAtom(display, "_NET_WM_NAME");
    atoms.netWmPid = internAtom(display, "_NET_WM_PID");
    atoms.netWmWindowType = internAtom(display, "_NET_WM_WINDOW_TYPE");
    atoms.typeCombo = internAtom(display, "_NET_WM_WINDOW_TYPE_COMBO");
    atoms.typeDesktop = internAtom(display, "_NET_WM_WINDOW_TYPE_DESKTOP");
    atoms.typeDialog = internAtom(display, "_NET_WM_WINDOW_TYPE_DIALOG");
    atoms.typeDock = internAtom(display, "_NET_WM_WINDOW_TYPE_DOCK");
    atoms.typeDropdownMenu = internAtom(display, "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU");
    atoms.typeMenu = internAtom(display, "_NET_WM_WINDOW_TYPE_MENU");
    atoms.typeNotification = internAtom(display, "_NET_WM_WINDOW_TYPE_NOTIFICATION");
    atoms.typePopupMenu = internAtom(display, "_NET_WM_WINDOW_TYPE_POPUP_MENU");
    atoms.typeSplash = internAtom(display, "_NET_WM_WINDOW_TYPE_SPLASH");
    atoms.typeToolbar = internAtom(display, "_NET_WM_WINDOW_TYPE_TOOLBAR");
    atoms.typeTooltip = internAtom(display, "_NET_WM_WINDOW_TYPE_TOOLTIP");
    atoms.typeUtility = internAtom(display, "_NET_WM_WINDOW_TYPE_UTILITY");
    return atoms;
}

Display* qtX11Display()
{
    if (!qGuiApp || QGuiApplication::platformName() != QStringLiteral("xcb")) {
        return nullptr;
    }

    auto* x11Application = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    return x11Application ? x11Application->display() : nullptr;
}

std::vector<unsigned long> readCardinalListProperty(Display* display,
                                                    ::Window window,
                                                    Atom property,
                                                    Atom expectedType,
                                                    long maxItems = 1024)
{
    std::vector<unsigned long> values;
    if (!display || property == None) {
        return values;
    }

    Atom actualType = None;
    int actualFormat = 0;
    unsigned long itemCount = 0;
    unsigned long bytesAfter = 0;
    unsigned char* data = nullptr;

    const int status = XGetWindowProperty(
        display,
        window,
        property,
        0,
        maxItems,
        False,
        expectedType,
        &actualType,
        &actualFormat,
        &itemCount,
        &bytesAfter,
        &data);

    if (status == Success && data && actualType == expectedType && actualFormat == 32) {
        auto* rawValues = reinterpret_cast<unsigned long*>(data);
        values.reserve(itemCount);
        for (unsigned long i = 0; i < itemCount; ++i) {
            values.push_back(rawValues[i]);
        }
    }

    if (data) {
        XFree(data);
    }
    return values;
}

QByteArray readByteProperty(Display* display,
                            ::Window window,
                            Atom property,
                            Atom expectedType,
                            long maxItems = 4096)
{
    if (!display || property == None) {
        return {};
    }

    Atom actualType = None;
    int actualFormat = 0;
    unsigned long itemCount = 0;
    unsigned long bytesAfter = 0;
    unsigned char* data = nullptr;

    const int status = XGetWindowProperty(
        display,
        window,
        property,
        0,
        maxItems,
        False,
        expectedType,
        &actualType,
        &actualFormat,
        &itemCount,
        &bytesAfter,
        &data);

    QByteArray bytes;
    if (status == Success && data && actualType == expectedType && actualFormat == 8) {
        bytes = QByteArray(reinterpret_cast<const char*>(data), static_cast<int>(itemCount));
    }

    if (data) {
        XFree(data);
    }
    return bytes;
}

QString readWindowTitle(Display* display, ::Window window, const X11Atoms& atoms)
{
    const QByteArray netWmName = readByteProperty(display, window, atoms.netWmName, atoms.utf8String);
    if (!netWmName.isEmpty()) {
        return QString::fromUtf8(netWmName);
    }

    char* wmName = nullptr;
    if (XFetchName(display, window, &wmName) > 0 && wmName) {
        const QString title = QString::fromLocal8Bit(wmName);
        XFree(wmName);
        return title;
    }
    return QString();
}

QString readOwnerApp(Display* display, ::Window window, const X11Atoms& atoms)
{
    const QByteArray wmClass = readByteProperty(display, window, atoms.wmClass, XA_STRING);
    if (wmClass.isEmpty()) {
        return QString();
    }

    const QList<QByteArray> parts = wmClass.split('\0');
    for (auto it = parts.crbegin(); it != parts.crend(); ++it) {
        if (!it->isEmpty()) {
            return QString::fromLocal8Bit(*it);
        }
    }
    return QString();
}

qint64 readOwnerPid(Display* display, ::Window window, const X11Atoms& atoms)
{
    const auto values =
        readCardinalListProperty(display, window, atoms.netWmPid, atoms.cardinal, 1);
    return values.empty() ? 0 : static_cast<qint64>(values.front());
}

std::vector<Atom> readAtomListProperty(Display* display,
                                       ::Window window,
                                       Atom property,
                                       long maxItems = 32)
{
    std::vector<Atom> atoms;
    const auto values = readCardinalListProperty(display, window, property, XA_ATOM, maxItems);
    atoms.reserve(values.size());
    for (unsigned long value : values) {
        atoms.push_back(static_cast<Atom>(value));
    }
    return atoms;
}

bool containsAtom(const std::vector<Atom>& atoms, Atom needle)
{
    return needle != None && std::find(atoms.cbegin(), atoms.cend(), needle) != atoms.cend();
}

ElementType classifyElementType(Display* display, ::Window window, const X11Atoms& atoms)
{
    const auto windowTypes = readAtomListProperty(display, window, atoms.netWmWindowType);

    if (containsAtom(windowTypes, atoms.typeDesktop)) {
        return ElementType::Unknown;
    }

    if (containsAtom(windowTypes, atoms.typeDock)) {
        return ElementType::StatusBarItem;
    }

    if (containsAtom(windowTypes, atoms.typeDialog) ||
        containsAtom(windowTypes, atoms.typeSplash) ||
        containsAtom(windowTypes, atoms.typeUtility)) {
        return ElementType::Dialog;
    }

    if (containsAtom(windowTypes, atoms.typeMenu) ||
        containsAtom(windowTypes, atoms.typeDropdownMenu) ||
        containsAtom(windowTypes, atoms.typePopupMenu)) {
        return ElementType::PopupMenu;
    }

    if (containsAtom(windowTypes, atoms.typeCombo) ||
        containsAtom(windowTypes, atoms.typeNotification) ||
        containsAtom(windowTypes, atoms.typeToolbar) ||
        containsAtom(windowTypes, atoms.typeTooltip)) {
        return ElementType::PopupMenu;
    }

    return ElementType::Window;
}

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

bool shouldPreferTopLevelBoundsForElementType(ElementType elementType)
{
    switch (elementType) {
    case ElementType::ContextMenu:
    case ElementType::PopupMenu:
    case ElementType::StatusBarItem:
        return true;
    case ElementType::Window:
    case ElementType::Dialog:
    case ElementType::Unknown:
        return false;
    }

    return false;
}

DetectionFlags flagsForQueryMode(DetectionFlags baseFlags, WindowDetector::QueryMode queryMode)
{
    if (queryMode == WindowDetector::QueryMode::TopLevelOnly) {
        baseFlags &= ~DetectionFlags(DetectionFlag::ChildControls);
    }
    return baseFlags;
}

int getMinimumSize(ElementType type)
{
    switch (type) {
    case ElementType::Window:
        return 50;
    case ElementType::ContextMenu:
    case ElementType::PopupMenu:
    case ElementType::StatusBarItem:
        return 10;
    case ElementType::Dialog:
        return 20;
    case ElementType::Unknown:
        return 50;
    }
    return 50;
}

QRect physicalToLogicalRect(const QRect& physicalRect, qreal dpr)
{
    if (qFuzzyIsNull(dpr) || qFuzzyCompare(dpr, 1.0)) {
        return physicalRect;
    }

    return QRect(
        qRound(static_cast<qreal>(physicalRect.x()) / dpr),
        qRound(static_cast<qreal>(physicalRect.y()) / dpr),
        qMax(1, qRound(static_cast<qreal>(physicalRect.width()) / dpr)),
        qMax(1, qRound(static_cast<qreal>(physicalRect.height()) / dpr)));
}

std::optional<QRect> readWindowBounds(Display* display,
                                      ::Window rootWindow,
                                      ::Window window,
                                      const X11Atoms& atoms)
{
    XWindowAttributes attributes;
    if (XGetWindowAttributes(display, window, &attributes) == 0 ||
        attributes.map_state != IsViewable ||
        attributes.width <= 0 ||
        attributes.height <= 0) {
        return std::nullopt;
    }

    int rootX = 0;
    int rootY = 0;
    ::Window child = 0;
    if (XTranslateCoordinates(
            display,
            window,
            rootWindow,
            0,
            0,
            &rootX,
            &rootY,
            &child) == 0) {
        return std::nullopt;
    }

    int leftExtent = 0;
    int rightExtent = 0;
    int topExtent = 0;
    int bottomExtent = 0;
    const auto frameExtents =
        readCardinalListProperty(display, window, atoms.netFrameExtents, atoms.cardinal, 4);
    if (frameExtents.size() >= 4) {
        leftExtent = static_cast<int>(frameExtents[0]);
        rightExtent = static_cast<int>(frameExtents[1]);
        topExtent = static_cast<int>(frameExtents[2]);
        bottomExtent = static_cast<int>(frameExtents[3]);
    }

    return QRect(
        rootX - leftExtent,
        rootY - topExtent,
        attributes.width + leftExtent + rightExtent,
        attributes.height + topExtent + bottomExtent);
}

std::vector<::Window> readRootWindowList(Display* display,
                                         ::Window rootWindow,
                                         const X11Atoms& atoms)
{
    std::vector<::Window> windows;
    const auto stackingValues = readCardinalListProperty(
        display,
        rootWindow,
        atoms.netClientListStacking,
        XA_WINDOW,
        16384);
    auto sourceValues = stackingValues;
    if (sourceValues.empty()) {
        sourceValues =
            readCardinalListProperty(display, rootWindow, atoms.netClientList, XA_WINDOW, 16384);
    }

    windows.reserve(sourceValues.size());
    for (unsigned long value : sourceValues) {
        windows.push_back(static_cast<::Window>(value));
    }

    if (windows.empty()) {
        ::Window rootReturn = 0;
        ::Window parentReturn = 0;
        ::Window* children = nullptr;
        unsigned int childCount = 0;
        if (XQueryTree(
                display,
                rootWindow,
                &rootReturn,
                &parentReturn,
                &children,
                &childCount) != 0 && children) {
            windows.reserve(childCount);
            for (unsigned int i = 0; i < childCount; ++i) {
                windows.push_back(children[i]);
            }
        }
        if (children) {
            XFree(children);
        }
    }

    std::vector<::Window> deduplicated;
    deduplicated.reserve(windows.size());
    std::unordered_set<unsigned long> seen;
    for (const ::Window window : windows) {
        if (window == 0) {
            continue;
        }
        const auto key = static_cast<unsigned long>(window);
        if (seen.insert(key).second) {
            deduplicated.push_back(window);
        }
    }
    return deduplicated;
}

std::unordered_set<unsigned long> ownQtTopLevelWindowIds()
{
    std::unordered_set<unsigned long> ids;
    if (!qGuiApp) {
        return ids;
    }

    const auto windows = QGuiApplication::topLevelWindows();
    ids.reserve(static_cast<size_t>(windows.size()));
    for (QWindow* window : windows) {
        if (!window || window->winId() == 0) {
            continue;
        }
        ids.insert(static_cast<unsigned long>(window->winId()));
    }
    return ids;
}

std::vector<DetectedElement> enumerateWindowsSnapshot(qreal dpr, DetectionFlags flags)
{
    std::vector<DetectedElement> cache;
    Display* display = qtX11Display();
    if (!display) {
        return cache;
    }

    const X11Atoms atoms = makeAtoms(display);
    const ::Window rootWindow = DefaultRootWindow(display);
    const std::vector<::Window> windows = readRootWindowList(display, rootWindow, atoms);
    const std::unordered_set<unsigned long> ownWindowIds = ownQtTopLevelWindowIds();
    const qint64 currentPid = static_cast<qint64>(::getpid());

    cache.reserve(windows.size());
    for (auto it = windows.crbegin(); it != windows.crend(); ++it) {
        const ::Window window = *it;
        if (ownWindowIds.find(static_cast<unsigned long>(window)) != ownWindowIds.cend()) {
            continue;
        }

        const qint64 ownerPid = readOwnerPid(display, window, atoms);
        if (ownerPid == currentPid) {
            continue;
        }

        const ElementType elementType = classifyElementType(display, window, atoms);
        if (!shouldIncludeElementType(elementType, flags)) {
            continue;
        }

        const auto physicalBounds = readWindowBounds(display, rootWindow, window, atoms);
        if (!physicalBounds.has_value()) {
            continue;
        }

        const int minSize = getMinimumSize(elementType);
        if (physicalBounds->width() < minSize || physicalBounds->height() < minSize) {
            continue;
        }

        DetectedElement element;
        element.bounds = physicalToLogicalRect(*physicalBounds, dpr);
        element.windowTitle = readWindowTitle(display, window, atoms);
        element.ownerApp = readOwnerApp(display, window, atoms);
        element.windowLayer = 0;
        element.windowId = static_cast<uint32_t>(window);
        element.elementType = elementType;
        element.ownerPid = ownerPid;
        cache.push_back(element);
    }

    return cache;
}

} // namespace

WindowDetector::WindowDetector(QObject* parent)
    : QObject(parent)
    , m_currentScreen(nullptr)
    , m_enabled(true)
    , m_detectionFlags(DetectionFlag::All)
{
}

WindowDetector::~WindowDetector() = default;

bool WindowDetector::hasAccessibilityPermission(bool promptIfMissing)
{
    Q_UNUSED(promptIfMissing);
    return true;
}

void WindowDetector::setScreen(QScreen* screen)
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

void WindowDetector::refreshWindowList(QueryMode queryMode)
{
    ++m_refreshRequestId;
    m_refreshComplete.store(false);
    const DetectionFlags flags = flagsForQueryMode(m_detectionFlags, queryMode);

    std::vector<DetectedElement> previousCache;
    QScreen* previousScreen = nullptr;
    QueryMode previousQueryMode = QueryMode::TopLevelOnly;

    {
        QMutexLocker locker(&m_cacheMutex);
        previousCache = m_windowCache;
        previousScreen = m_cacheScreen;
        previousQueryMode = m_cacheQueryMode;
        m_windowCache.clear();
        m_cacheReady = false;
        m_cacheScreen = nullptr;
    }

    std::vector<DetectedElement> newCache;
    if (m_enabled) {
        newCache = enumerateWindowsSnapshot(
            m_currentScreen ? m_currentScreen->devicePixelRatio() : 1.0,
            flags);
        mergePreservedTopLevelElements(
            newCache,
            previousCache,
            previousQueryMode,
            previousScreen,
            queryMode,
            m_currentScreen.data());
    }

    {
        QMutexLocker locker(&m_cacheMutex);
        m_windowCache = std::move(newCache);
        m_cacheScreen = m_currentScreen.data();
        m_cacheQueryMode = queryMode;
        m_cacheReady = true;
    }
    m_refreshComplete.store(true);
    emit windowListReady();
}

void WindowDetector::refreshWindowListAsync(QueryMode queryMode)
{
    refreshWindowList(queryMode);
}

bool WindowDetector::isRefreshComplete() const
{
    return true;
}

bool WindowDetector::isWindowCacheReady(QueryMode queryMode) const
{
    QMutexLocker locker(&m_cacheMutex);
    return m_cacheReady &&
           m_cacheScreen == m_currentScreen.data() &&
           static_cast<int>(m_cacheQueryMode) >= static_cast<int>(queryMode);
}

std::optional<DetectedElement> WindowDetector::detectWindowAt(const QPoint& screenPos,
                                                              QueryMode queryMode) const
{
    if (!m_enabled) {
        return std::nullopt;
    }

    const QRect screenGeometry = m_currentScreen ? m_currentScreen->geometry() : QRect();
    if (!screenGeometry.isNull() && !screenGeometry.contains(screenPos)) {
        return std::nullopt;
    }

    QMutexLocker locker(&m_cacheMutex);
    if (!m_cacheReady || m_cacheScreen != m_currentScreen.data()) {
        return std::nullopt;
    }

    const bool detectChildren =
        queryMode == QueryMode::IncludeChildControls &&
        m_cacheQueryMode == QueryMode::IncludeChildControls &&
        m_detectionFlags.testFlag(DetectionFlag::ChildControls);

    for (size_t i = 0; i < m_windowCache.size(); ++i) {
        const auto& element = m_windowCache[i];
        if (element.windowLayer == 1) {
            continue;
        }

        if (!element.bounds.contains(screenPos)) {
            continue;
        }

        if (!screenGeometry.isNull()) {
            const QRect clipped = element.bounds.intersected(screenGeometry);
            if (!clipped.contains(screenPos)) {
                continue;
            }
        }

        if (shouldPreferTopLevelBoundsForElementType(element.elementType)) {
            return element;
        }

        if (detectChildren) {
            const auto childElement = detectChildElementAt(screenPos, element, i);
            if (childElement.has_value()) {
                return childElement;
            }
        }

        return element;
    }

    return std::nullopt;
}

DetectionFlags WindowDetector::detectionFlags() const
{
    return m_detectionFlags;
}

std::optional<DetectedElement> WindowDetector::detectChildElementAt(
    const QPoint& screenPos,
    const DetectedElement& topWindow,
    size_t topLevelIndex) const
{
    Q_UNUSED(screenPos);
    Q_UNUSED(topWindow);
    Q_UNUSED(topLevelIndex);
    return std::nullopt;
}

void WindowDetector::enumerateWindows()
{
    m_windowCache = enumerateWindowsSnapshot(
        m_currentScreen ? m_currentScreen->devicePixelRatio() : 1.0,
        m_detectionFlags);
}

void WindowDetector::enumerateWindowsInternal(std::vector<DetectedElement>& cache,
                                              qreal dpr,
                                              DetectionFlags flags)
{
    cache = enumerateWindowsSnapshot(dpr, flags);
}
