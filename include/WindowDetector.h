#ifndef WINDOWDETECTOR_H
#define WINDOWDETECTOR_H

#include <QObject>
#include <QRect>
#include <QPoint>
#include <QString>
#include <QFlags>
#include <QMutex>
#include <QFuture>
#include <QPointer>
#include <QElapsedTimer>
#include <algorithm>
#include <vector>
#include <optional>

class QScreen;

// Element type classification for detected UI elements
enum class ElementType {
    Window,         // Normal application window
    ContextMenu,    // Right-click context menu
    PopupMenu,      // Application menu dropdown
    Dialog,         // Dialog/modal window
    StatusBarItem,  // Menu bar popup (macOS) / System tray popup (Windows)
    Unknown
};

// Detection mode flags for controlling what types of elements to detect
enum class DetectionFlag {
    None           = 0,
    Windows        = 1 << 0,   // Normal application windows (default)
    ContextMenus   = 1 << 1,   // Right-click context menus
    PopupMenus     = 1 << 2,   // Application menu dropdowns
    Dialogs        = 1 << 3,   // Dialog/modal windows
    StatusBarItems = 1 << 4,   // Menu bar popups (macOS) / System tray popups (Windows)
    ModernUI       = 1 << 5,   // Modern UI elements (IME, tooltips, notifications)
    ChildControls  = 1 << 6,   // Child controls within windows (buttons, text boxes, panels)
    AllSystemUI    = ContextMenus | PopupMenus | Dialogs | StatusBarItems | ModernUI,
    All            = Windows | AllSystemUI | ChildControls
};
Q_DECLARE_FLAGS(DetectionFlags, DetectionFlag)
Q_DECLARE_OPERATORS_FOR_FLAGS(DetectionFlags)

// Represents a detected window or UI element
struct DetectedElement {
    QRect bounds;           // Element bounds in screen coordinates (logical pixels)
    QString windowTitle;    // Window/element title for hint display
    QString ownerApp;       // Owning application name
    int windowLayer;        // Window layer/level for z-ordering
    uint32_t windowId;      // Window ID for identification
    ElementType elementType = ElementType::Window;  // Type of detected element
    qint64 ownerPid = 0;   // Process ID of owning application
};

class WindowDetector : public QObject
{
    Q_OBJECT

public:
    enum class QueryMode {
        TopLevelOnly,
        IncludeChildControls
    };

    explicit WindowDetector(QObject *parent = nullptr);
    ~WindowDetector();

    // Permission management
    static bool hasAccessibilityPermission(bool promptIfMissing = false);

    // Detection control
    void setScreen(QScreen *screen);
    void setEnabled(bool enabled);
    bool isEnabled() const;

    // Refresh window list (call before capture session starts)
    void refreshWindowList(QueryMode queryMode = QueryMode::IncludeChildControls);

    // Async version - enumerate windows in background thread
    void refreshWindowListAsync(QueryMode queryMode = QueryMode::IncludeChildControls);

    // Check if async refresh is complete
    bool isRefreshComplete() const;

    // Check whether the current screen already has a usable cache snapshot,
    // even if a newer async refresh is still in flight.
    bool isWindowCacheReady(QueryMode queryMode = QueryMode::TopLevelOnly) const;

    // Find window at point (screen coordinates)
    std::optional<DetectedElement> detectWindowAt(
        const QPoint &screenPos,
        QueryMode queryMode = QueryMode::IncludeChildControls) const;

    // Detection mode control
    DetectionFlags detectionFlags() const;

signals:
    void windowListReady();

protected:
    virtual std::optional<DetectedElement> detectChildElementAt(
        const QPoint &screenPos,
        const DetectedElement &topWindow,
        size_t topLevelIndex) const;

private:
    void enumerateWindows();
    void enumerateWindowsInternal(std::vector<DetectedElement>& cache, qreal dpr, DetectionFlags flags);
    static void mergePreservedTopLevelElements(
        std::vector<DetectedElement>& newCache,
        const std::vector<DetectedElement>& previousCache,
        QueryMode previousQueryMode,
        QScreen* previousScreen,
        QueryMode newQueryMode,
        QScreen* newScreen)
    {
        if (previousQueryMode != QueryMode::TopLevelOnly ||
            newQueryMode != QueryMode::IncludeChildControls ||
            !newScreen ||
            previousScreen != newScreen) {
            return;
        }

        for (const auto& previousElement : previousCache) {
            const bool alreadyPresent = std::any_of(
                newCache.cbegin(),
                newCache.cend(),
                [&previousElement](const DetectedElement& currentElement) {
                    if (previousElement.windowId != 0 && currentElement.windowId != 0) {
                        return previousElement.windowId == currentElement.windowId;
                    }

                    if (previousElement.ownerPid > 0 &&
                        currentElement.ownerPid > 0 &&
                        previousElement.ownerPid != currentElement.ownerPid) {
                        return false;
                    }

                    const QRect previousSlack =
                        previousElement.bounds.adjusted(-6, -6, 6, 6);
                    const QRect currentSlack =
                        currentElement.bounds.adjusted(-6, -6, 6, 6);
                    return previousSlack.intersects(currentElement.bounds) ||
                           currentSlack.intersects(previousElement.bounds) ||
                           previousSlack.contains(currentElement.bounds.center()) ||
                           currentSlack.contains(previousElement.bounds.center());
                });
            if (!alreadyPresent) {
                newCache.push_back(previousElement);
            }
        }
    }

    std::vector<DetectedElement> m_windowCache;
    mutable QMutex m_cacheMutex;
    QPointer<QScreen> m_currentScreen;
    bool m_enabled;
    DetectionFlags m_detectionFlags = DetectionFlag::All;
    QFuture<void> m_refreshFuture;
    std::atomic<bool> m_refreshComplete{true};
    std::atomic<uint64_t> m_refreshRequestId{0};
    bool m_cacheReady = false;
    QScreen* m_cacheScreen = nullptr;
    QueryMode m_cacheQueryMode = QueryMode::TopLevelOnly;

#ifdef Q_OS_MACOS
    std::optional<DetectedElement> detectChildElementAt(const QPoint &screenPos, qint64 targetPid, const QRect &windowBounds) const;
    mutable DetectedElement m_axCache;
    mutable bool m_axCacheValid{false};
    mutable QElapsedTimer m_axCacheTimer;
    mutable bool m_accessibilityPromptRequested{false};
#endif

    friend class tst_WindowDetectorQueryMode;
    friend class tst_RegionSelectorDeferredInitialization;
};

#endif // WINDOWDETECTOR_H
